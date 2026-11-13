"""Windows window-capture backend: user32/gdi32 bindings, interactive-desktop preflight, client-area BitBlt readback, and synthetic input."""


import ctypes
import time
from window_capture_outcome import SmokeSkip, WindowsDesktopUnavailable, SmokeFailure  # noqa: E402
from window_capture_windows_pixels import WindowsScreenPixels, WinBitmapInfo, WinBitmapInfoHeader  # noqa: E402


LIT_WINDOW_POLL_SECONDS = 0.1
LIT_FOCUS_SETTLE_SECONDS = 0.1
LIT_KEY_PRESS_SECONDS = 0.05
LIT_X = "x"
LIT_Y = "y"
LIT_DWMAPI = "dwmapi"
LIT_HWND_CLIENT_RECT_UNAVAILABLE = "HWND 0x{hwnd:x} client rect is unavailable"
WINDOWS_ENUM_CALLBACK = getattr(ctypes, "WINFUNCTYPE", ctypes.CFUNCTYPE)(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p,)


def bind_windows_user32(user32):
    user32.EnumWindows.argtypes = [WINDOWS_ENUM_CALLBACK, ctypes.c_void_p]
    user32.EnumWindows.restype = ctypes.c_int
    user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
    user32.GetWindowThreadProcessId.restype = ctypes.c_uint32
    user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
    user32.PostMessageW.restype = ctypes.c_int
    return user32


def create_windows_user32():
    return bind_windows_user32(ctypes.WinDLL("user32", use_last_error=True))


class WinRect(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long), ("right", ctypes.c_long), ("bottom", ctypes.c_long),]


class WinPoint(ctypes.Structure):
    _fields_ = [(LIT_X, ctypes.c_long), (LIT_Y, ctypes.c_long),]


class WindowsUserObjectFlags(ctypes.Structure):
    _fields_ = [("fInherit", ctypes.c_int32), ("fReserved", ctypes.c_int32), ("dwFlags", ctypes.c_uint32)]


def require_windows_interactive_desktop(user32=None):
    user32 = user32 if user32 is not None else create_windows_user32()
    user32.GetProcessWindowStation.argtypes = []
    user32.GetProcessWindowStation.restype = ctypes.c_void_p
    user32.GetUserObjectInformationW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    user32.GetUserObjectInformationW.restype = ctypes.c_int
    station = user32.GetProcessWindowStation()
    if not station:
        raise SmokeFailure(f"GetProcessWindowStation failed: {getattr(ctypes, 'get_last_error', ctypes.get_errno)()}")
    flags = WindowsUserObjectFlags()
    length_needed = ctypes.c_uint32()
    # Check WSF_VISIBLE on the borrowed process window station handle.
    if not user32.GetUserObjectInformationW(ctypes.c_void_p(station), 1, ctypes.byref(flags), ctypes.sizeof(flags), ctypes.byref(length_needed)):
        raise SmokeFailure(f"GetUserObjectInformationW(UOI_FLAGS) failed: {getattr(ctypes, 'get_last_error', ctypes.get_errno)()}")
    if not flags.dwFlags & 0x0001:
        raise WindowsDesktopUnavailable("the Windows process window station has no visible display surfaces; native window/input smoke requires an interactive desktop")


class WindowsCapture:
    SRCCOPY = 0x00CC0020
    DIB_RGB_COLORS = 0
    S_OK = 0
    E_INVALIDARG = 0x80070057
    DWMWA_WINDOW_CORNER_PREFERENCE = 33
    DWMWCP_DONOTROUND = 1
    HWND_TOPMOST = ctypes.c_void_p(-1)
    SW_SHOW = 5
    SW_RESTORE = 9
    SWP_NOSIZE = 0x0001
    SWP_NOMOVE = 0x0002
    SWP_NOZORDER = 0x0004
    SWP_SHOWWINDOW = 0x0040
    SWP_NOSENDCHANGING = 0x0400
    SWP_ASYNCWINDOWPOS = 0x4000
    VK_RETURN = 0x0D
    VIRTUAL_KEYS = { **{str(index): 0x30 + index for index in range(0, 10)}, **{chr(ord("A") + index): 0x41 + index for index in range(0, 26)}, **{f"F{index}": 0x6F + index for index in range(1, 13)}, }
    WM_KEYDOWN = 0x0100
    WM_KEYUP = 0x0101
    RECT = WinRect
    POINT = WinPoint
    BITMAPINFOHEADER = WinBitmapInfoHeader
    BITMAPINFO = WinBitmapInfo

    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ctypes.c_void_p(-4)

    def __init__(self):
        self.user32 = create_windows_user32()
        require_windows_interactive_desktop(self.user32)
        self._enable_dpi_awareness()
        self.gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
        self.dwmapi = None
        self._bind_functions()

    def _enable_dpi_awareness(self):
        self.user32.GetThreadDpiAwarenessContext.argtypes = []
        self.user32.GetThreadDpiAwarenessContext.restype = ctypes.c_void_p
        self.user32.AreDpiAwarenessContextsEqual.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.user32.AreDpiAwarenessContextsEqual.restype = ctypes.c_int
        self.user32.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
        self.user32.SetProcessDpiAwarenessContext.restype = ctypes.c_int
        context = ctypes.c_void_p(self.user32.GetThreadDpiAwarenessContext())
        if self.user32.AreDpiAwarenessContextsEqual(context, self.DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2):
            return
        if self.user32.SetProcessDpiAwarenessContext(self.DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2):
            return
        self.user32.SetThreadDpiAwarenessContext.argtypes = [ctypes.c_void_p]
        self.user32.SetThreadDpiAwarenessContext.restype = ctypes.c_void_p
        self.user32.SetThreadDpiAwarenessContext(self.DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)
        context = ctypes.c_void_p(self.user32.GetThreadDpiAwarenessContext())
        if not self.user32.AreDpiAwarenessContextsEqual(context, self.DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2):
            raise SmokeFailure(f"SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2) failed: {getattr(ctypes, 'get_last_error', ctypes.get_errno)()}")

    def close(self):
        pass

    def _bind_functions(self):
        self.user32.IsWindowVisible.argtypes = [ctypes.c_void_p]
        self.user32.IsWindowVisible.restype = ctypes.c_int
        self.user32.IsIconic.argtypes = [ctypes.c_void_p]
        self.user32.IsIconic.restype = ctypes.c_int
        self.user32.IsZoomed.argtypes = [ctypes.c_void_p]
        self.user32.IsZoomed.restype = ctypes.c_int
        self.user32.GetWindowRect.argtypes = [ctypes.c_void_p, ctypes.POINTER(self.RECT)]
        self.user32.GetWindowRect.restype = ctypes.c_int
        self.user32.GetClientRect.argtypes = [ctypes.c_void_p, ctypes.POINTER(self.RECT)]
        self.user32.GetClientRect.restype = ctypes.c_int
        self.user32.ClientToScreen.argtypes = [ctypes.c_void_p, ctypes.POINTER(self.POINT)]
        self.user32.ClientToScreen.restype = ctypes.c_int
        self.user32.SetForegroundWindow.argtypes = [ctypes.c_void_p]
        self.user32.SetForegroundWindow.restype = ctypes.c_int
        self.user32.SetWindowPos.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_uint]
        self.user32.SetWindowPos.restype = ctypes.c_int
        self.user32.ShowWindow.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.user32.ShowWindow.restype = ctypes.c_int
        WindowsScreenPixels.bind(self.user32, self.gdi32)

    def _window_rect(self, hwnd):
        rect = self.RECT()
        if not self.user32.GetWindowRect(ctypes.c_void_p(hwnd), ctypes.byref(rect)):
            return None
        return rect

    def _client_rect(self, hwnd):
        client_rect = self.RECT()
        hwnd_ptr = ctypes.c_void_p(hwnd)
        if not self.user32.GetClientRect(hwnd_ptr, ctypes.byref(client_rect)):
            return None

        width = client_rect.right - client_rect.left
        height = client_rect.bottom - client_rect.top
        if width <= 0 or height <= 0:
            return None

        origin = self.POINT(client_rect.left, client_rect.top)
        if not self.user32.ClientToScreen(hwnd_ptr, ctypes.byref(origin)):
            return None

        return self.RECT(origin.x, origin.y, origin.x + width, origin.y + height)

    def find_window_for_pid(self, pid, title=None, require_mapped=True):
        matches = []

        def enum_callback(hwnd, _):
            if require_mapped and not self.user32.IsWindowVisible(hwnd):
                return True

            window_pid = ctypes.c_uint32()
            self.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(window_pid))
            if window_pid.value != pid:
                return True

            rect = self._window_rect(hwnd)
            if not rect:
                return True

            width = rect.right - rect.left
            height = rect.bottom - rect.top
            if width > 0 and height > 0:
                matches.append((width * height, hwnd))
            return True

        callback = WINDOWS_ENUM_CALLBACK(enum_callback)
        self.user32.EnumWindows(callback, None)
        if not matches:
            return None

        matches.sort(reverse=True)
        return int(matches[0][1])

    def wait_for_window(self, pid, timeout_seconds, title=None, require_mapped=True):
        del title
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            hwnd = self.find_window_for_pid(pid, require_mapped=require_mapped)
            if hwnd:
                return hwnd
            time.sleep(LIT_WINDOW_POLL_SECONDS)
        return None

    def _window_size(self, hwnd):
        rect = self._window_rect(hwnd)
        if not rect:
            raise SmokeFailure(f"HWND 0x{hwnd:x} rect is unavailable")

        width = rect.right - rect.left
        height = rect.bottom - rect.top
        if width <= 0 or height <= 0:
            raise SmokeFailure(f"HWND 0x{hwnd:x} has invalid size {width}x{height}")
        return width, height

    def focus_window(self, hwnd):
        self.user32.SetForegroundWindow(ctypes.c_void_p(hwnd))

    def prepare_window(self, hwnd):
        self._prepare_capture_window(hwnd)

    def client_size(self, hwnd):
        rect = self._client_rect(hwnd)
        if not rect:
            raise SmokeFailure(LIT_HWND_CLIENT_RECT_UNAVAILABLE.format(hwnd=hwnd))
        return rect.right - rect.left, rect.bottom - rect.top

    MAX_EXACT_FRAMEBUFFER_EXTENT = 16384

    def resize_client(self, hwnd, width, height, *, allow_outside_work_area=False):
        if allow_outside_work_area and any(value < 1 or value > self.MAX_EXACT_FRAMEBUFFER_EXTENT for value in (width, height)):
            raise SmokeFailure("exact framebuffer extent must remain within 1..16384 pixels")
        client_width, client_height = self.client_size(hwnd)
        window_width, window_height = self._window_size(hwnd)
        # Preserve the current nonclient frame without guessing caption/border dimensions.
        outer_width = width + window_width - client_width
        outer_height = height + window_height - client_height
        if outer_width < width or outer_height < height:
            raise SmokeFailure("window/client rectangles disagree while calculating the requested resize")
        flags = self.SWP_NOMOVE | self.SWP_NOZORDER | self.SWP_SHOWWINDOW | self.SWP_ASYNCWINDOWPOS
        if allow_outside_work_area:
            # Bypass default tracking bounds so large test framebuffers keep their requested size.
            flags |= self.SWP_NOSENDCHANGING
        if not self.user32.SetWindowPos(ctypes.c_void_p(hwnd), None, 0, 0, outer_width, outer_height, flags):
            raise SmokeFailure(f"SetWindowPos failed while resizing HWND 0x{hwnd:x}")

    def send_named_key(self, hwnd, key_name):
        virtual_key = self.VIRTUAL_KEYS.get(str(key_name))
        if virtual_key is None:
            raise SmokeFailure(f"could not resolve virtual key '{key_name}'")

        self.focus_window(hwnd)
        self.user32.PostMessageW(ctypes.c_void_p(hwnd), self.WM_KEYDOWN, virtual_key, 0)
        time.sleep(LIT_KEY_PRESS_SECONDS)
        self.user32.PostMessageW(ctypes.c_void_p(hwnd), self.WM_KEYUP, virtual_key, 0)

    def _prepare_capture_window(self, hwnd):
        hwnd_ptr = ctypes.c_void_p(hwnd)
        # Repeated restore/show requests can animate an already settled client before the screen capture.
        if self.user32.IsIconic(hwnd_ptr) or self.user32.IsZoomed(hwnd_ptr):
            self.user32.ShowWindow(hwnd_ptr, self.SW_RESTORE)
        elif not self.user32.IsWindowVisible(hwnd_ptr):
            self.user32.ShowWindow(hwnd_ptr, self.SW_SHOW)
        self.user32.SetWindowPos(hwnd_ptr, self.HWND_TOPMOST, 0, 0, 0, 0, self.SWP_NOMOVE | self.SWP_NOSIZE)
        self.user32.SetForegroundWindow(hwnd_ptr)
        time.sleep(LIT_FOCUS_SETTLE_SECONDS)

    def _raw_client_dwmapi(self):
        if getattr(self, LIT_DWMAPI, None) is not None:
            return self.dwmapi

        try:
            dwmapi = ctypes.WinDLL(LIT_DWMAPI, use_last_error=True)
        except OSError as error:
            raise SmokeFailure("Windows raw client capture requires DwmSetWindowAttribute from dwmapi") from error

        dwmapi.DwmSetWindowAttribute.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32]
        dwmapi.DwmSetWindowAttribute.restype = ctypes.c_int32
        dwmapi.DwmFlush.argtypes = []
        dwmapi.DwmFlush.restype = ctypes.c_int32
        self.dwmapi = dwmapi
        return dwmapi

    def prepare_raw_client_window(self, hwnd):
        """Prepare the HWND for full raw client-area capture before waiting for readiness."""
        self._prepare_capture_window(hwnd)

        preference = ctypes.c_int(self.DWMWCP_DONOTROUND)
        preference_size = ctypes.sizeof(preference)
        if preference_size != 4:
            raise SmokeFailure(f"Windows raw client capture requires a 4-byte DWM corner preference; got {preference_size} bytes")

        dwmapi = self._raw_client_dwmapi()
        result = dwmapi.DwmSetWindowAttribute(ctypes.c_void_p(hwnd), self.DWMWA_WINDOW_CORNER_PREFERENCE, ctypes.byref(preference), preference_size,)
        unsigned_result = ctypes.c_uint32(result).value
        if unsigned_result == self.E_INVALIDARG:
            raise SmokeSkip("Windows raw client capture requires Windows 11 build 22000 or later with " "DWMWA_WINDOW_CORNER_PREFERENCE; DwmSetWindowAttribute returned HRESULT 0x80070057")
        if result != self.S_OK:
            raise SmokeFailure("Windows raw client capture requires " "DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_DONOTROUND); " f"HRESULT 0x{unsigned_result:08X}")

        result = dwmapi.DwmFlush()
        if result != self.S_OK:
            raise SmokeFailure(f"Windows raw client capture requires DwmFlush; HRESULT 0x{ctypes.c_uint32(result).value:08X}")

    def _capture_screen_rect(self, hwnd, rect, output_path):
        return WindowsScreenPixels(self.user32, self.gdi32).capture(hwnd, rect, output_path)

    def capture_window(self, hwnd, output_path):
        # Both entry points capture renderer-owned client pixels.
        return self.capture_client_window(hwnd, output_path)

    def capture_client_window(self, hwnd, output_path):
        # Capture only the client area to exclude the DWM window frame.
        self._prepare_capture_window(hwnd)
        rect = self._client_rect(hwnd)
        if not rect:
            raise SmokeFailure(LIT_HWND_CLIENT_RECT_UNAVAILABLE.format(hwnd=hwnd))
        return self._capture_screen_rect(hwnd, rect, output_path)

    def capture_prepared_raw_client_window(self, hwnd, output_path):
        """Capture the full raw client area after prepare_raw_client_window."""
        rect = self._client_rect(hwnd)
        if not rect:
            raise SmokeFailure(LIT_HWND_CLIENT_RECT_UNAVAILABLE.format(hwnd=hwnd))
        return self._capture_screen_rect(hwnd, rect, output_path)

    def sample_client_pixels(self, hwnd, x, y, width, height):
        rect = self._client_rect(hwnd)
        if not rect:
            raise SmokeFailure(LIT_HWND_CLIENT_RECT_UNAVAILABLE.format(hwnd=hwnd))
        if x < 0 or y < 0 or width <= 0 or height <= 0 or x + width > rect.right - rect.left or y + height > rect.bottom - rect.top:
            raise SmokeFailure("Windows pixel sample falls outside the client area")
        return WindowsScreenPixels(self.user32, self.gdi32).sample(hwnd, rect, x, y, width, height)
