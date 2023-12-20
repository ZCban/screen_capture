import cv2
import dxcam
import time
import numpy as np

class ScreenCaptureDXCam:
    def __init__(self, screenshotdim):
        # Get screen resolution dynamically
        self.screen_width = int((win32api.GetSystemMetrics(0) / 2)-(screenshotdim/2)) # 0 corresponds to SM_CXSCREEN
        self.screen_height = int((win32api.GetSystemMetrics(1) / 2)-(screenshotdim/2)) # 1 corresponds to SM_CYSCREEN
        self.width = screenshotdim
        self.height = screenshotdim

        self.left,self.top = self.screen_width,self.screen_height
        self.right,self.bottom = self.left+screenshotdim,self.top-screenshotdim
        self.cam=dxcam.create()
        self.cam.startN(region=region,video_mode=False,target_fps=0)

        self.startTime = None
        self.fps = 0

        # Initialize dxcam instance
        self.dxcam = dxcam.DxCam()

    def calculate_fps(self):
        endTime = time.time()
        elapsedTime = endTime - self.startTime
        self.startTime = endTime
        self.fps = 1 / elapsedTime
        print(self.fps)

    def capture(self):
        if self.startTime is None:
            self.startTime = time.time()

        # Capture frame using dxcam
        img = self.cam.get_frameN()

        return img

    def display_image(self, img):
        cv2.imshow("Captured Image", img)
        cv2.waitKey(1)

# Example usage
screenshotdim = 320
screen_capture = ScreenCaptureDXCam(screenshotdim)

while True:
    img = screen_capture.capture()
    screen_capture.display_image(img)
    screen_capture.calculate_fps()
