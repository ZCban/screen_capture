import win32gui
import win32ui
import win32con
import win32api
import numpy as np
import time
import cv2

class ScreenCaptureGDI:
    def __init__(self, width, height):
        # Get screen resolution dynamically
        self.screen_width = int((win32api.GetSystemMetrics(0) / 2)-(screenshotdim/2)) # 0 corresponds to SM_CXSCREEN
        self.screen_height =int((win32api.GetSystemMetrics(1) / 2)-(screenshotdim/2)) # 1 corresponds to SM_CYSCREEN
        

        self.width = width
        self.height = height
        self.left = self.screen_width 
        self.top = self.screen_height 
        print(self.left,self.top)
        self.cWidth = self.width / 2
        self.cHeight = self.height / 2


        # Create device contexts and bitmap outside of the capture method
        self.hdesktop = win32gui.GetDesktopWindow()
        self.desktop_dc = win32gui.GetWindowDC(self.hdesktop)
        self.img_dc = win32ui.CreateDCFromHandle(self.desktop_dc)
        self.mem_dc = self.img_dc.CreateCompatibleDC()
        self.bitmap = win32ui.CreateBitmap()
        self.bitmap.CreateCompatibleBitmap(self.img_dc, self.width, self.height)
        self.mem_dc.SelectObject(self.bitmap)

        self.startTime = None
        self.fps = 0

    def calculate_fps(self):
        endTime = time.time()
        elapsedTime = endTime - self.startTime
        self.startTime = endTime
        self.fps = 1 / elapsedTime
        print(self.fps)

    def capture(self):
        if self.startTime is None:
            self.startTime = time.time()

        # Capture the specified region
        self.mem_dc.BitBlt((0, 0), (self.width, self.height), self.img_dc, (self.left, self.top), win32con.SRCCOPY)

        # Convert the captured bitmap data to a NumPy array
        img = np.frombuffer(self.bitmap.GetBitmapBits(True), dtype=np.uint8)

        # Reshape the NumPy array to a 3-dimensional image
        img.shape = (self.height, self.width, 4)

        # Calculate and print the FPS
        #self.calculate_fps()

        return img

    def display_image(self, img):
        cv2.imshow("Captured Image", img)
        cv2.waitKey(1)

# Set the desired width, height, and capture FPS
#screenshotdim=320

# Create ScreenCaptureGDI instance with desired width, height, and capture FPS
#screen_capture = ScreenCaptureGDI(screenshotdim, screenshotdim)

#while True:
    #img = screen_capture.capture()
    # Your processing or display code here
    #screen_capture.display_image(img)



