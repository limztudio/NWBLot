"""Windows GDI screen-pixel capture and sampling."""


import ctypes

from window_capture_image import write_capture_rows
from window_capture_image_codec import NativePixelCodec
from window_capture_outcome import SmokeFailure


class WinBitmapInfoHeader(ctypes.Structure):
    _fields_ = [("biSize", ctypes.c_uint32), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long), ("biPlanes", ctypes.c_uint16), ("biBitCount", ctypes.c_uint16), ("biCompression", ctypes.c_uint32), ("biSizeImage", ctypes.c_uint32),
        ("biXPelsPerMeter", ctypes.c_long), ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", ctypes.c_uint32), ("biClrImportant", ctypes.c_uint32),]


class WinBitmapInfo(ctypes.Structure):
    _fields_ = [("bmiHeader", WinBitmapInfoHeader), ("bmiColors", ctypes.c_uint32 * 3),]


class WindowsScreenPixels:
    SRCCOPY = 0x00CC0020
    DIB_RGB_COLORS = 0
    BITMAPINFOHEADER = WinBitmapInfoHeader
    BITMAPINFO = WinBitmapInfo

    def __init__(self, user32, gdi32):
        self.user32 = user32
        self.gdi32 = gdi32

    @staticmethod
    def bind(user32, gdi32):
        user32.GetDC.argtypes = [ctypes.c_void_p]
        user32.GetDC.restype = ctypes.c_void_p
        user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        user32.ReleaseDC.restype = ctypes.c_int
        gdi32.CreateCompatibleDC.argtypes = [ctypes.c_void_p]
        gdi32.CreateCompatibleDC.restype = ctypes.c_void_p
        gdi32.CreateCompatibleBitmap.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        gdi32.CreateCompatibleBitmap.restype = ctypes.c_void_p
        gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        gdi32.SelectObject.restype = ctypes.c_void_p
        gdi32.BitBlt.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_uint32,]
        gdi32.BitBlt.restype = ctypes.c_int
        gdi32.GetPixel.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        gdi32.GetPixel.restype = ctypes.c_uint32
        gdi32.GetDIBits.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.POINTER(WinBitmapInfo), ctypes.c_uint,]
        gdi32.GetDIBits.restype = ctypes.c_int
        gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
        gdi32.DeleteObject.restype = ctypes.c_int
        gdi32.DeleteDC.argtypes = [ctypes.c_void_p]
        gdi32.DeleteDC.restype = ctypes.c_int

    def capture(self, hwnd, rect, output_path):
        width = rect.right - rect.left
        height = rect.bottom - rect.top
        if width <= 0 or height <= 0:
            raise SmokeFailure(f"HWND 0x{hwnd:x} has invalid size {width}x{height}")

        screen_dc = self.user32.GetDC(None)
        if not screen_dc:
            raise SmokeFailure(f"GetDC failed while capturing HWND 0x{hwnd:x}")

        mem_dc = None
        bitmap = None
        old_object = None
        try:
            mem_dc = self.gdi32.CreateCompatibleDC(screen_dc)
            if not mem_dc:
                raise SmokeFailure(f"CreateCompatibleDC failed while capturing HWND 0x{hwnd:x}")

            bitmap = self.gdi32.CreateCompatibleBitmap(screen_dc, width, height)
            if not bitmap:
                raise SmokeFailure(f"CreateCompatibleBitmap failed while capturing HWND 0x{hwnd:x}")

            old_object = self.gdi32.SelectObject(mem_dc, bitmap)
            if not old_object:
                raise SmokeFailure(f"SelectObject failed while capturing HWND 0x{hwnd:x}")

            if not self.gdi32.BitBlt(mem_dc, 0, 0, width, height, screen_dc, rect.left, rect.top, self.SRCCOPY):
                raise SmokeFailure(f"BitBlt failed for HWND 0x{hwnd:x}")

            bitmap_info = self.BITMAPINFO()
            bitmap_info.bmiHeader.biSize = ctypes.sizeof(self.BITMAPINFOHEADER)
            bitmap_info.bmiHeader.biWidth = width
            bitmap_info.bmiHeader.biHeight = -height
            bitmap_info.bmiHeader.biPlanes = 1
            bitmap_info.bmiHeader.biBitCount = 32
            bitmap_info.bmiHeader.biCompression = 0
            buffer = (ctypes.c_ubyte * (width * height * 4))()
            if self.gdi32.GetDIBits(mem_dc, bitmap, 0, height, buffer, ctypes.byref(bitmap_info), self.DIB_RGB_COLORS) == 0:
                raise SmokeFailure(f"GetDIBits failed for HWND 0x{hwnd:x}")

            return write_capture_rows(hwnd, width, height, NativePixelCodec.bgra_rgb_rows(buffer, width, height), output_path)
        finally:
            if old_object:
                self.gdi32.SelectObject(mem_dc, old_object)
            if bitmap:
                self.gdi32.DeleteObject(bitmap)
            if mem_dc:
                self.gdi32.DeleteDC(mem_dc)
            if screen_dc:
                self.user32.ReleaseDC(None, screen_dc)

    def sample(self, hwnd, rect, x, y, width, height):
        screen_dc = self.user32.GetDC(None)
        if not screen_dc:
            raise SmokeFailure(f"GetDC failed while sampling HWND 0x{hwnd:x}")
        try:
            rows = []
            for row in range(height):
                pixels = []
                for column in range(width):
                    color = self.gdi32.GetPixel(screen_dc, rect.left + x + column, rect.top + y + row)
                    if color == 0xFFFFFFFF:
                        raise SmokeFailure(f"GetPixel failed while sampling HWND 0x{hwnd:x}")
                    pixels.append((color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF))
                rows.append(pixels)
            return rows
        finally:
            self.user32.ReleaseDC(None, screen_dc)
