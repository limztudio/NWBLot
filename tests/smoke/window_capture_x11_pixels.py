"""X11 drawable pixel readback and image lifetime."""


import ctypes

from window_capture_image import write_capture_rows
from window_capture_image_codec import NativePixelCodec
from window_capture_outcome import SmokeFailure


LIT_LITTLE = "little"
LIT_BIG = "big"


class X11DrawablePixels:
    LSB_FIRST = 0
    Z_PIXMAP = 2

    def __init__(self, x11, display):
        self.x11 = x11
        self.display = display

    @staticmethod
    def bind(x11, image_type):
        x11.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_int,]
        x11.XGetImage.restype = ctypes.POINTER(image_type)
        x11.XDestroyImage.argtypes = [ctypes.POINTER(image_type)]
        x11.XDestroyImage.restype = ctypes.c_int

    def sample(self, window, x, y, width, height):
        image = self.x11.XGetImage(self.display, window, x, y, width, height, ctypes.c_ulong(-1).value, self.Z_PIXMAP)
        if not image:
            raise SmokeFailure(f"XGetImage failed for window 0x{window:x}")
        try:
            if image.contents.bits_per_pixel not in (16, 24, 32) or not image.contents.data:
                raise SmokeFailure("X11 pixel sample has unsupported or missing image data")
            byte_order = LIT_LITTLE if image.contents.byte_order == self.LSB_FIRST else LIT_BIG
            return NativePixelCodec.ximage_rgb_rows(image.contents, byte_order)
        finally:
            self.x11.XDestroyImage(image)


    def capture(self, drawable, output_handle, x, y, width, height, output_path):
        image = self.x11.XGetImage(self.display, drawable, x, y, width, height, ctypes.c_ulong(-1).value, self.Z_PIXMAP,)
        if not image:
            raise SmokeFailure(f"XGetImage failed for drawable 0x{drawable:x}")

        try:
            return self.write_ximage_bmp(output_handle, image.contents, output_path)
        finally:
            self.x11.XDestroyImage(image)


    def write_ximage_bmp(self, output_handle, image, output_path):
        bytes_per_pixel = (image.bits_per_pixel + 7) // 8
        if bytes_per_pixel not in (2, 3, 4):
            raise SmokeFailure(f"unsupported XImage pixel size: {image.bits_per_pixel} bits")
        if not image.data:
            raise SmokeFailure(f"XImage for drawable 0x{output_handle:x} has no pixel data")

        byte_order = LIT_LITTLE if image.byte_order == self.LSB_FIRST else LIT_BIG
        rows = NativePixelCodec.ximage_rgb_rows(image, byte_order)
        return write_capture_rows(output_handle, image.width, image.height, rows, output_path)
