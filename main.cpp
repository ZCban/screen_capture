#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <iostream>
#include <vector>
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

namespace py = pybind11;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxguid.lib")

// --- VARIABILI HARDWARE GLOBALI PERSISTENTI ---
ID3D11Device* pDevice = nullptr;
ID3D11DeviceContext* pDeviceContext = nullptr;
ID2D1DeviceContext* pD2DContext = nullptr;
IDXGIOutputDuplication* pDeskDupl = nullptr;
ID2D1Effect* pColorMatrixEffect = nullptr;

ID3D11Texture2D* pGpuTargetTexture = nullptr;
ID3D11Texture2D* pCpuStagingTexture = nullptr; 
ID2D1Bitmap1* pD2DTargetBitmap = nullptr;
ID2D1Bitmap1* pLastValidBitmap = nullptr;

// Buffer CPU persistente per l'immagine finale a 3 canali (uint8)
std::vector<BYTE> outputImageBuffer;

UINT cropLeft = 0;
UINT cropTop = 0;
const UINT cropWidth = 512;
const UINT cropHeight = 512;

bool initialize_capture() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    cropLeft = (screenWidth - cropWidth) / 2;
    cropTop = (screenHeight - cropHeight) / 2;

    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };

    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels, 1,
        D3D11_SDK_VERSION, &pDevice, &featureLevel, &pDeviceContext);
    if (FAILED(hr)) return false;

    IDXGIDevice* pDxgiDevice = nullptr;
    pDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDxgiDevice);

    IDXGIAdapter* pDxgiAdapter = nullptr;
    pDxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&pDxgiAdapter);

    ID2D1Factory1* pD2DFactory = nullptr;
    D2D1_FACTORY_OPTIONS factoryOptions = {};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &factoryOptions, (void**)&pD2DFactory);

    ID2D1Device* pD2DDevice = nullptr;
    pD2DFactory->CreateDevice(pDxgiDevice, &pD2DDevice);
    pD2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pD2DContext);

    // Allocazione delle texture Target e Staging 512x512
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = cropWidth;
    desc.Height = cropHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    pDevice->CreateTexture2D(&desc, nullptr, &pGpuTargetTexture);

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    pDevice->CreateTexture2D(&desc, nullptr, &pCpuStagingTexture);

    IDXGISurface* pTargetSurface = nullptr;
    pGpuTargetTexture->QueryInterface(__uuidof(IDXGISurface), (void**)&pTargetSurface);
    pD2DContext->CreateBitmapFromDxgiSurface(pTargetSurface, nullptr, &pD2DTargetBitmap);
    pTargetSurface->Release();

    D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
    );
    pD2DContext->CreateBitmap(D2D1::SizeU(cropWidth, cropHeight), nullptr, 0, &bitmapProps, &pLastValidBitmap);

    IDXGIOutput* pDxgiOutput = nullptr;
    hr = pDxgiAdapter->EnumOutputs(0, &pDxgiOutput);
    pDxgiAdapter->Release();

    IDXGIOutput1* pDxgiOutput1 = nullptr;
    pDxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&pDxgiOutput1);
    pDxgiOutput->Release();

    hr = pDxgiOutput1->DuplicateOutput(pDevice, &pDeskDupl);
    pDxgiOutput1->Release();
    pDxgiDevice->Release();
    pD2DDevice->Release();
    pD2DFactory->Release();

    if (FAILED(hr)) return false;

    // Effetto Hardware della Matrice Colore per invertire i canali
    pD2DContext->CreateEffect(CLSID_D2D1ColorMatrix, &pColorMatrixEffect);
    D2D1_MATRIX_5X4_F bgraToRgbaMatrix = D2D1::Matrix5x4F(
        0.0f, 0.0f, 1.0f, 0.0f, // B diventa R
        0.0f, 1.0f, 0.0f, 0.0f, // G resta G
        1.0f, 0.0f, 0.0f, 0.0f, // R diventa B
        0.0f, 0.0f, 0.0f, 1.0f, 
        0.0f, 0.0f, 0.0f, 0.0f
    );
    pColorMatrixEffect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, bgraToRgbaMatrix);

    // Alloca lo spazio per l'array di byte di output (512 * 512 * 3 canali RGB)
    outputImageBuffer.resize(cropWidth * cropHeight * 3);

    return true;
}

py::object get_frame_image(bool utilizza_rgba) {
    IDXGIResource* pDesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ID3D11Texture2D* pAcquiredTexture = nullptr;
    bool nuovoFrameRilevato = false;

    // Flush loop anti-lag integrato
    while (pDeskDupl->AcquireNextFrame(0, &frameInfo, &pDesktopResource) == S_OK) {
        if (nuovoFrameRilevato && pAcquiredTexture) {
            pAcquiredTexture->Release();
            pDeskDupl->ReleaseFrame();
        }
        if (pDesktopResource) {
            pDesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pAcquiredTexture);
            pDesktopResource->Release();
            nuovoFrameRilevato = true;
        }
    }

    if (!nuovoFrameRilevato) {
        return py::none();
    }

    // Iniezione nel contesto Direct2D
    IDXGISurface* pScanSurface = nullptr;
    pAcquiredTexture->QueryInterface(__uuidof(IDXGISurface), (void**)&pScanSurface);

    ID2D1Bitmap1* pD2DInputBitmap = nullptr;
    pD2DContext->CreateBitmapFromDxgiSurface(pScanSurface, nullptr, &pD2DInputBitmap);

    if (pD2DInputBitmap) {
        pD2DContext->SetTarget(pLastValidBitmap);
        pD2DContext->BeginDraw();

        D2D1_RECT_F srcRect = D2D1::RectF(
            static_cast<float>(cropLeft), static_cast<float>(cropTop),
            static_cast<float>(cropLeft + cropWidth), static_cast<float>(cropTop + cropHeight)
        );
        D2D1_POINT_2F destOffset = D2D1::Point2F(0.0f, 0.0f);

        if (utilizza_rgba) {
            pColorMatrixEffect->SetInput(0, pD2DInputBitmap);
            ID2D1Image* pEffectOutput = nullptr;
            pColorMatrixEffect->GetOutput(&pEffectOutput);
            pD2DContext->DrawImage(pEffectOutput, &destOffset, &srcRect, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_COPY);
            pEffectOutput->Release();
        } else {
            pD2DContext->DrawImage(pD2DInputBitmap, &destOffset, &srcRect, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_COPY);
        }

        pD2DContext->EndDraw();

        pD2DContext->SetTarget(pD2DTargetBitmap);
        pD2DContext->BeginDraw();
        pD2DContext->DrawImage(pLastValidBitmap, &destOffset, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_COPY);
        pD2DContext->EndDraw();

        pD2DInputBitmap->Release();
    }

    pScanSurface->Release();
    pAcquiredTexture->Release();

    // Copia hardware velocissima del risultato pronto verso la CPU
    pDeviceContext->CopyResource(pCpuStagingTexture, pGpuTargetTexture);
    pDeskDupl->ReleaseFrame();

    D3D11_MAPPED_SUBRESOURCE mappedTex;
    if (SUCCEEDED(pDeviceContext->Map(pCpuStagingTexture, 0, D3D11_MAP_READ, 0, &mappedTex))) {
        BYTE* pRawPixels = reinterpret_cast<BYTE*>(mappedTex.pData);
        BYTE* __restrict pDst = outputImageBuffer.data();

        for (UINT y = 0; y < cropHeight; ++y) {
            BYTE* __restrict pSrcRow = pRawPixels + (y * mappedTex.RowPitch);
            UINT pixelOffsetBase = y * cropWidth;

            for (UINT x = 0; x < cropWidth; ++x) {
                UINT srcIdx = x << 2;
                UINT dstIdx = (pixelOffsetBase + x) * 3;

                // Estraiamo l'immagine a 3 canali (uint8) escludendo l'Alpha
                pDst[dstIdx + 0] = pSrcRow[srcIdx + 0]; // Canale 1
                pDst[dstIdx + 1] = pSrcRow[srcIdx + 1]; // Canale 2
                pDst[dstIdx + 2] = pSrcRow[srcIdx + 2]; // Canale 3
            }
        }
        pDeviceContext->Unmap(pCpuStagingTexture, 0);
    }

    // Restituisce un normale array NumPy a 3 dimensioni HWC (512, 512, 3) tipo uint8
    std::vector<size_t> imageShape = { cropHeight, cropWidth, 3 };
    return py::array_t<BYTE>(imageShape, outputImageBuffer.data());
}

void cleanup_capture() {
    if (pLastValidBitmap) pLastValidBitmap->Release();
    if (pD2DTargetBitmap) pD2DTargetBitmap->Release();
    if (pGpuTargetTexture) pGpuTargetTexture->Release();
    if (pCpuStagingTexture) pCpuStagingTexture->Release();
    if (pColorMatrixEffect) pColorMatrixEffect->Release();
    if (pDeskDupl) pDeskDupl->Release();
    if (pD2DContext) pD2DContext->Release();
    if (pDeviceContext) pDeviceContext->Release();
    if (pDevice) pDevice->Release();
    CoUninitialize();
}

PYBIND11_MODULE(gpu_capture, m) {
    m.def("initialize", &initialize_capture);
    m.def("get_frame_image", &get_frame_image, py::arg("utilizza_rgba") = false);
    m.def("cleanup", &cleanup_capture);
}
