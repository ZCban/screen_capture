import mss
import win32api
import numpy as np
import time
import cv2

class ScreenCaptureMSS:
    def __init__(self,screenshotdim):
        # Get screen resolution dynamically
        self.screen_width = int((win32api.GetSystemMetrics(0) / 2)-(screenshotdim/2)) # 0 corresponds to SM_CXSCREEN
        self.screen_height =int((win32api.GetSystemMetrics(1) / 2)-(screenshotdim/2)) # 1 corresponds to SM_CYSCREEN
        #print(self.screen_width,self.screen_height)

        self.width = screenshotdim
        self.height = screenshotdim

        self.startTime = None
        self.fps = 0

        # Initialize mss instance and monitor settings
        self.sct = mss.mss()
        self.monitor = {"top": self.screen_height , "left": self.screen_width , "width": self.width, "height": self.height}

    def calculate_fps(self):
        endTime = time.time()
        elapsedTime = endTime - self.startTime
        self.startTime = endTime
        self.fps = 1 / elapsedTime
        print(self.fps)

    def capture(self):
        if self.startTime is None:
            self.startTime = time.time()

        img = np.array(self.sct.grab(self.monitor))

        # Calculate and print the FPS
        #self.calculate_fps()

        return img

    def display_image(self, img):
        cv2.imshow("Captured Image", img)
        cv2.waitKey(1)

##exeple
# Create ScreenCaptureMSS instance with desired width and height
screenshotdim=320
screen_capture = ScreenCaptureMSS(screenshotdim)

while True:
    img = screen_capture.capture()
    screen_capture.display_image(img)
    #screen_capture.calculate_fps()
