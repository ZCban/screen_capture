import cv2
import numpy as np
import time
import gpu_capture

if not gpu_capture.initialize():
    print("Errore hardware.")
    exit(-1)

print("Engine GPU-Image avviato. Loop attivo...")

# Buffer persistente in RAM per mantenere lo stato se lo schermo è fermo
ultima_immagine_valida = np.zeros((512, 512, 3), dtype=np.uint8)
frame_count = 0
start_time = time.time()

try:
    while True:
        # Se utilizzi_rgba=True la GPU inverte in RGBA, se False mantiene BGRA
        img_gpu = gpu_capture.get_frame_image(utilizza_rgba=True)
        
        if img_gpu is  None:
            continue
            
        # Adesso hai l'immagine OpenCV standard (512x512x3) pulita ed efficiente
        # Puoi passarla direttamente al tuo modello ONNX o a cv2.imshow
        
        #cv2.imshow("Anteprima GPU 512x512", img_gpu )
        #if cv2.waitKey(1) & 0xFF == ord('q'):
        #    break

        # Contatore FPS nel terminale
        frame_count += 1
        if time.time() - start_time >= 1.0:
            print(f"\rFPS Live: {frame_count}    ", end="", flush=True)
            frame_count = 0
            start_time = time.time()

except KeyboardInterrupt:
    pass
finally:
    cv2.destroyAllWindows()
    gpu_capture.cleanup()
