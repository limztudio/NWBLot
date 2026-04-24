#!/usr/bin/env python3
import argparse
import ctypes
import os
from pathlib import Path
import platform
import socket
import struct
import subprocess
import sys
import time


SKIP_EXIT_CODE = 77


class SmokeSkip(Exception):
    pass


class SmokeFailure(Exception):
    pass


class CaptureResult:
    def __init__(self, handle, width, height, has_pixel_variation):
        self.handle = handle
        self.width = width
        self.height = height
        self.has_pixel_variation = has_pixel_variation


def write_status(message):
    print(message, file=sys.stderr, flush=True)


def parse_int(value):
    try:
        return int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer value '{value}'") from exc


def choose_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_tcp_port(port, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.2)
            try:
                sock.connect(("127.0.0.1", port))
                return
            except OSError:
                time.sleep(0.05)

    raise SmokeFailure(f"logserver did not open port {port}")


def terminate_process(process, name):
    if process is None or process.poll() is not None:
        return

    process.terminate()
    try:
        process.wait(timeout=5.0)
    except subprocess.TimeoutExpired:
        write_status(f"{name}: did not exit after terminate; killing")
        process.kill()
        process.wait(timeout=5.0)


def read_process_tail(process):
    if process is None:
        return ""

    output = ""
    if process.stdout:
        try:
            output = process.stdout.read()
        except OSError:
            output = ""

    if not output:
        return ""

    lines = output.splitlines()
    return "\n".join(lines[-40:])


def executable_sibling(executable, name):
    suffix = ".exe" if platform.system() == "Windows" else ""
    candidate = executable.parent / f"{name}{suffix}"
    return candidate if candidate.exists() else None


def resolve_lavapipe_icd():
    candidates = (
        Path("/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"),
        Path("/usr/share/vulkan/icd.d/lvp_icd.i686.json"),
        Path("/etc/vulkan/icd.d/lvp_icd.x86_64.json"),
        Path("/etc/vulkan/icd.d/lvp_icd.i686.json"),
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate

    return None


def build_launch_environment(args):
    env = os.environ.copy()
    system = platform.system()

    if system == "Linux":
        if not env.get("DISPLAY"):
            raise SmokeSkip("DISPLAY is not set; X11 window capture is unavailable")

        env.setdefault("NWB_LINUX_BACKEND", "x11")
        if args.software_vulkan != "off":
            lavapipe_icd = resolve_lavapipe_icd()
            if lavapipe_icd:
                env["VK_ICD_FILENAMES"] = str(lavapipe_icd)
            elif args.software_vulkan == "on":
                raise SmokeSkip("Mesa lavapipe Vulkan ICD was requested but is not installed")

    return env


def write_bmp_24(path, width, height, rows_rgb):
    path.parent.mkdir(parents=True, exist_ok=True)

    row_stride = ((width * 3 + 3) // 4) * 4
    image_size = row_stride * height
    file_size = 14 + 40 + image_size
    padding = b"\0" * (row_stride - width * 3)

    with path.open("wb") as out:
        out.write(struct.pack("<2sIHHI", b"BM", file_size, 0, 0, 14 + 40))
        out.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0))
        for row in reversed(rows_rgb):
            encoded = bytearray()
            for red, green, blue in row:
                encoded.extend((blue, green, red))
            out.write(encoded)
            out.write(padding)


def channel_from_mask(pixel, mask):
    if mask == 0:
        return 0

    low_bit = mask & -mask
    shift = low_bit.bit_length() - 1
    bit_count = mask.bit_length() - shift
    value = (pixel & mask) >> shift
    maximum = (1 << bit_count) - 1
    return (value * 255 + maximum // 2) // maximum


class LinuxXWindowAttributes(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_int),
        ("y", ctypes.c_int),
        ("width", ctypes.c_int),
        ("height", ctypes.c_int),
        ("border_width", ctypes.c_int),
        ("depth", ctypes.c_int),
        ("visual", ctypes.c_void_p),
        ("root", ctypes.c_ulong),
        ("class", ctypes.c_int),
        ("bit_gravity", ctypes.c_int),
        ("win_gravity", ctypes.c_int),
        ("backing_store", ctypes.c_int),
        ("backing_planes", ctypes.c_ulong),
        ("backing_pixel", ctypes.c_ulong),
        ("save_under", ctypes.c_int),
        ("colormap", ctypes.c_ulong),
        ("map_installed", ctypes.c_int),
        ("map_state", ctypes.c_int),
        ("all_event_masks", ctypes.c_long),
        ("your_event_mask", ctypes.c_long),
        ("do_not_propagate_mask", ctypes.c_long),
        ("override_redirect", ctypes.c_int),
        ("screen", ctypes.c_void_p),
    ]


class LinuxXImageFuncs(ctypes.Structure):
    _fields_ = [
        ("create_image", ctypes.c_void_p),
        ("destroy_image", ctypes.c_void_p),
        ("get_pixel", ctypes.c_void_p),
        ("put_pixel", ctypes.c_void_p),
        ("sub_image", ctypes.c_void_p),
        ("add_pixel", ctypes.c_void_p),
    ]


class LinuxXImage(ctypes.Structure):
    pass


LinuxXImage._fields_ = [
    ("width", ctypes.c_int),
    ("height", ctypes.c_int),
    ("xoffset", ctypes.c_int),
    ("format", ctypes.c_int),
    ("data", ctypes.c_char_p),
    ("byte_order", ctypes.c_int),
    ("bitmap_unit", ctypes.c_int),
    ("bitmap_bit_order", ctypes.c_int),
    ("bitmap_pad", ctypes.c_int),
    ("depth", ctypes.c_int),
    ("bytes_per_line", ctypes.c_int),
    ("bits_per_pixel", ctypes.c_int),
    ("red_mask", ctypes.c_ulong),
    ("green_mask", ctypes.c_ulong),
    ("blue_mask", ctypes.c_ulong),
    ("obdata", ctypes.c_void_p),
    ("f", LinuxXImageFuncs),
]


class LinuxX11Capture:
    LSB_FIRST = 0
    IS_VIEWABLE = 2
    Z_PIXMAP = 2
    XWindowAttributes = LinuxXWindowAttributes
    XImage = LinuxXImage

    def __init__(self):
        self.x11 = ctypes.CDLL("libX11.so.6")
        self._bind_functions()
        self.display = self.x11.XOpenDisplay(None)
        if not self.display:
            raise SmokeSkip("XOpenDisplay failed; X11 window capture is unavailable")

        self.pid_atom = self.x11.XInternAtom(self.display, b"_NET_WM_PID", False)
        self.root = self.x11.XDefaultRootWindow(self.display)

    def close(self):
        if self.display:
            self.x11.XCloseDisplay(self.display)
            self.display = None

    def _bind_functions(self):
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        self.x11.XDefaultRootWindow.restype = ctypes.c_ulong
        self.x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.x11.XInternAtom.restype = ctypes.c_ulong
        self.x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self.x11.XFree.argtypes = [ctypes.c_void_p]
        self.x11.XFetchName.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_char_p),
        ]
        self.x11.XFetchName.restype = ctypes.c_int
        self.x11.XQueryTree.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
            ctypes.POINTER(ctypes.c_uint),
        ]
        self.x11.XQueryTree.restype = ctypes.c_int
        self.x11.XGetWindowProperty.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_ulong,
            ctypes.c_long,
            ctypes.c_long,
            ctypes.c_int,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)),
        ]
        self.x11.XGetWindowProperty.restype = ctypes.c_int
        self.x11.XGetWindowAttributes.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.POINTER(self.XWindowAttributes),
        ]
        self.x11.XGetWindowAttributes.restype = ctypes.c_int
        self.x11.XGetImage.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ulong,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_ulong,
            ctypes.c_int,
        ]
        self.x11.XGetImage.restype = ctypes.POINTER(self.XImage)
        self.x11.XDestroyImage.argtypes = [ctypes.POINTER(self.XImage)]
        self.x11.XDestroyImage.restype = ctypes.c_int

    def get_window_pid(self, window):
        actual_type = ctypes.c_ulong()
        actual_format = ctypes.c_int()
        item_count = ctypes.c_ulong()
        bytes_after = ctypes.c_ulong()
        value = ctypes.POINTER(ctypes.c_ubyte)()

        status = self.x11.XGetWindowProperty(
            self.display,
            window,
            self.pid_atom,
            0,
            1,
            False,
            0,
            ctypes.byref(actual_type),
            ctypes.byref(actual_format),
            ctypes.byref(item_count),
            ctypes.byref(bytes_after),
            ctypes.byref(value),
        )
        if status != 0 or not value:
            return None

        try:
            if actual_format.value != 32 or item_count.value < 1:
                return None
            return int(ctypes.cast(value, ctypes.POINTER(ctypes.c_ulong))[0])
        finally:
            self.x11.XFree(value)

    def get_window_title(self, window):
        value = ctypes.c_char_p()
        if self.x11.XFetchName(self.display, window, ctypes.byref(value)) == 0 or not value:
            return None

        try:
            return value.value.decode("utf-8", "replace") if value.value else None
        finally:
            self.x11.XFree(value)

    def get_attributes(self, window):
        attributes = self.XWindowAttributes()
        if self.x11.XGetWindowAttributes(self.display, window, ctypes.byref(attributes)) == 0:
            return None
        return attributes

    def query_children(self, window):
        root = ctypes.c_ulong()
        parent = ctypes.c_ulong()
        children = ctypes.POINTER(ctypes.c_ulong)()
        child_count = ctypes.c_uint()
        status = self.x11.XQueryTree(
            self.display,
            window,
            ctypes.byref(root),
            ctypes.byref(parent),
            ctypes.byref(children),
            ctypes.byref(child_count),
        )
        if status == 0:
            return []

        try:
            return [children[i] for i in range(child_count.value)]
        finally:
            if children:
                self.x11.XFree(children)

    def find_window_for_pid(self, pid, title):
        stack = [self.root]
        matches = []
        while stack:
            window = stack.pop()
            children = self.query_children(window)
            stack.extend(children)

            window_pid = self.get_window_pid(window)
            if window_pid != pid and (not title or self.get_window_title(window) != title):
                continue

            attributes = self.get_attributes(window)
            if not attributes:
                continue
            if attributes.map_state != self.IS_VIEWABLE or attributes.width <= 0 or attributes.height <= 0:
                continue

            matches.append((attributes.width * attributes.height, window))

        if not matches:
            return None

        matches.sort(reverse=True)
        return matches[0][1]

    def wait_for_window(self, pid, timeout_seconds, title=None):
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            window = self.find_window_for_pid(pid, title)
            if window:
                return window
            time.sleep(0.1)

        return None

    def capture_window(self, window, output_path):
        self.x11.XSync(self.display, False)
        attributes = self.get_attributes(window)
        if not attributes:
            raise SmokeFailure(f"window 0x{window:x} attributes are unavailable")
        if attributes.width <= 0 or attributes.height <= 0:
            raise SmokeFailure(f"window 0x{window:x} has invalid size {attributes.width}x{attributes.height}")

        image = self.x11.XGetImage(
            self.display,
            window,
            0,
            0,
            attributes.width,
            attributes.height,
            ctypes.c_ulong(-1).value,
            self.Z_PIXMAP,
        )
        if not image:
            raise SmokeFailure(f"XGetImage failed for window 0x{window:x}")

        try:
            return self._write_ximage_bmp(window, image.contents, output_path)
        finally:
            self.x11.XDestroyImage(image)

    def _write_ximage_bmp(self, window, image, output_path):
        bytes_per_pixel = (image.bits_per_pixel + 7) // 8
        if bytes_per_pixel not in (2, 3, 4):
            raise SmokeFailure(f"unsupported XImage pixel size: {image.bits_per_pixel} bits")

        byte_order = "little" if image.byte_order == self.LSB_FIRST else "big"
        raw_size = image.bytes_per_line * image.height
        raw = ctypes.string_at(image.data, raw_size)
        rows = []
        first_pixel = None
        has_variation = False

        for y in range(image.height):
            row_base = y * image.bytes_per_line
            row = []
            for x in range(image.width):
                pixel_base = row_base + x * bytes_per_pixel
                pixel = int.from_bytes(raw[pixel_base:pixel_base + bytes_per_pixel], byte_order)
                red = channel_from_mask(pixel, image.red_mask)
                green = channel_from_mask(pixel, image.green_mask)
                blue = channel_from_mask(pixel, image.blue_mask)
                rgb = (red, green, blue)
                if first_pixel is None:
                    first_pixel = rgb
                elif rgb != first_pixel:
                    has_variation = True
                row.append(rgb)
            rows.append(row)

        write_bmp_24(output_path, image.width, image.height, rows)
        return CaptureResult(window, image.width, image.height, has_variation)


class WinRect(ctypes.Structure):
    _fields_ = [
        ("left", ctypes.c_long),
        ("top", ctypes.c_long),
        ("right", ctypes.c_long),
        ("bottom", ctypes.c_long),
    ]


class WinBitmapInfoHeader(ctypes.Structure):
    _fields_ = [
        ("biSize", ctypes.c_uint32),
        ("biWidth", ctypes.c_long),
        ("biHeight", ctypes.c_long),
        ("biPlanes", ctypes.c_uint16),
        ("biBitCount", ctypes.c_uint16),
        ("biCompression", ctypes.c_uint32),
        ("biSizeImage", ctypes.c_uint32),
        ("biXPelsPerMeter", ctypes.c_long),
        ("biYPelsPerMeter", ctypes.c_long),
        ("biClrUsed", ctypes.c_uint32),
        ("biClrImportant", ctypes.c_uint32),
    ]


class WinBitmapInfo(ctypes.Structure):
    _fields_ = [
        ("bmiHeader", WinBitmapInfoHeader),
        ("bmiColors", ctypes.c_uint32 * 3),
    ]


class WindowsCapture:
    SRCCOPY = 0x00CC0020
    DIB_RGB_COLORS = 0
    RECT = WinRect
    BITMAPINFOHEADER = WinBitmapInfoHeader
    BITMAPINFO = WinBitmapInfo

    def __init__(self):
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
        self._bind_functions()

    def close(self):
        pass

    def _bind_functions(self):
        self.user32.EnumWindows.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.user32.EnumWindows.restype = ctypes.c_int
        self.user32.IsWindowVisible.argtypes = [ctypes.c_void_p]
        self.user32.IsWindowVisible.restype = ctypes.c_int
        self.user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
        self.user32.GetWindowThreadProcessId.restype = ctypes.c_ulong
        self.user32.GetWindowRect.argtypes = [ctypes.c_void_p, ctypes.POINTER(self.RECT)]
        self.user32.GetWindowRect.restype = ctypes.c_int
        self.user32.GetDC.argtypes = [ctypes.c_void_p]
        self.user32.GetDC.restype = ctypes.c_void_p
        self.user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.user32.ReleaseDC.restype = ctypes.c_int
        self.gdi32.CreateCompatibleDC.argtypes = [ctypes.c_void_p]
        self.gdi32.CreateCompatibleDC.restype = ctypes.c_void_p
        self.gdi32.CreateCompatibleBitmap.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
        self.gdi32.CreateCompatibleBitmap.restype = ctypes.c_void_p
        self.gdi32.SelectObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.gdi32.SelectObject.restype = ctypes.c_void_p
        self.gdi32.BitBlt.argtypes = [
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_uint32,
        ]
        self.gdi32.BitBlt.restype = ctypes.c_int
        self.gdi32.GetDIBits.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_void_p,
            ctypes.POINTER(self.BITMAPINFO),
            ctypes.c_uint,
        ]
        self.gdi32.GetDIBits.restype = ctypes.c_int
        self.gdi32.DeleteObject.argtypes = [ctypes.c_void_p]
        self.gdi32.DeleteObject.restype = ctypes.c_int
        self.gdi32.DeleteDC.argtypes = [ctypes.c_void_p]
        self.gdi32.DeleteDC.restype = ctypes.c_int

    def _window_rect(self, hwnd):
        rect = self.RECT()
        if not self.user32.GetWindowRect(ctypes.c_void_p(hwnd), ctypes.byref(rect)):
            return None
        return rect

    def find_window_for_pid(self, pid):
        matches = []

        callback_type = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)

        def enum_callback(hwnd, _):
            if not self.user32.IsWindowVisible(hwnd):
                return True

            window_pid = ctypes.c_ulong()
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

        callback = callback_type(enum_callback)
        self.user32.EnumWindows(callback, None)
        if not matches:
            return None

        matches.sort(reverse=True)
        return int(matches[0][1])

    def wait_for_window(self, pid, timeout_seconds, title=None):
        del title
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            hwnd = self.find_window_for_pid(pid)
            if hwnd:
                return hwnd
            time.sleep(0.1)
        return None

    def capture_window(self, hwnd, output_path):
        rect = self._window_rect(hwnd)
        if not rect:
            raise SmokeFailure(f"HWND 0x{hwnd:x} rect is unavailable")

        width = rect.right - rect.left
        height = rect.bottom - rect.top
        if width <= 0 or height <= 0:
            raise SmokeFailure(f"HWND 0x{hwnd:x} has invalid size {width}x{height}")

        screen_dc = self.user32.GetDC(None)
        mem_dc = self.gdi32.CreateCompatibleDC(screen_dc)
        bitmap = self.gdi32.CreateCompatibleBitmap(screen_dc, width, height)
        old_object = self.gdi32.SelectObject(mem_dc, bitmap)
        try:
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

            rows = []
            first_pixel = None
            has_variation = False
            for y in range(height):
                row = []
                row_base = y * width * 4
                for x in range(width):
                    pixel_base = row_base + x * 4
                    blue = buffer[pixel_base]
                    green = buffer[pixel_base + 1]
                    red = buffer[pixel_base + 2]
                    rgb = (red, green, blue)
                    if first_pixel is None:
                        first_pixel = rgb
                    elif rgb != first_pixel:
                        has_variation = True
                    row.append(rgb)
                rows.append(row)

            write_bmp_24(output_path, width, height, rows)
            return CaptureResult(hwnd, width, height, has_variation)
        finally:
            if old_object:
                self.gdi32.SelectObject(mem_dc, old_object)
            if bitmap:
                self.gdi32.DeleteObject(bitmap)
            if mem_dc:
                self.gdi32.DeleteDC(mem_dc)
            if screen_dc:
                self.user32.ReleaseDC(None, screen_dc)


def create_capture_backend():
    system = platform.system()
    if system == "Linux":
        return LinuxX11Capture()
    if system == "Windows":
        return WindowsCapture()
    raise SmokeSkip(f"window handle capture is not implemented for {system}")


def launch_logserver(args, executable, env):
    if args.no_logserver:
        return None, None

    logserver = Path(args.logserver_executable) if args.logserver_executable else executable_sibling(executable, "logserver")
    if not logserver:
        raise SmokeSkip("logserver executable was not found next to the testbed target")

    port = args.log_port if args.log_port else choose_free_port()
    process = subprocess.Popen(
        [str(logserver), "-p", str(port)],
        cwd=args.working_directory,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        wait_for_tcp_port(port, min(args.timeout, 10.0))
    except Exception:
        terminate_process(process, "logserver")
        raise

    return process, port


def launch_testbed(args, executable, env, log_port):
    command = [str(executable)]
    if log_port:
        command.extend(["-a", "http://localhost", "-p", str(log_port)])
    command.extend(args.application_arg)

    return subprocess.Popen(
        command,
        cwd=args.working_directory,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def capture_existing_handle(args, backend):
    result = backend.capture_window(args.window_handle, args.output)
    if not result.has_pixel_variation:
        raise SmokeFailure(f"captured window 0x{result.handle:x}, but the image appears flat")

    return result


def launch_and_capture(args, backend):
    executable = Path(args.executable).resolve()
    if not executable.exists():
        raise SmokeFailure(f"executable does not exist: {executable}")

    env = build_launch_environment(args)
    logserver_process = None
    testbed_process = None
    try:
        logserver_process, log_port = launch_logserver(args, executable, env)
        testbed_process = launch_testbed(args, executable, env, log_port)
        handle = backend.wait_for_window(testbed_process.pid, args.timeout, args.window_title)
        if not handle:
            exit_code = testbed_process.poll()
            if exit_code is not None:
                tail = read_process_tail(testbed_process)
                raise SmokeFailure(f"testbed exited before a window was visible (exit {exit_code})\n{tail}")
            raise SmokeFailure("timed out waiting for a visible testbed window")

        time.sleep(args.settle_seconds)
        if testbed_process.poll() is not None:
            tail = read_process_tail(testbed_process)
            raise SmokeFailure(f"testbed exited before capture (exit {testbed_process.returncode})\n{tail}")

        result = backend.capture_window(handle, args.output)
        if not result.has_pixel_variation:
            raise SmokeFailure(f"captured window 0x{result.handle:x}, but the image appears flat")
        return result
    finally:
        terminate_process(testbed_process, "testbed")
        terminate_process(logserver_process, "logserver")


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Launch the NWB testbed and capture its native window handle.")
    parser.add_argument("--executable", help="Path to the testbed executable. Optional when --window-handle is used.")
    parser.add_argument("--working-directory", type=Path, default=Path.cwd(), help="Working directory for launched processes.")
    parser.add_argument("--output", type=Path, required=True, help="Screenshot output path. The script writes a 24-bit BMP.")
    parser.add_argument("--window-handle", type=parse_int, help="Capture an existing native window handle instead of launching testbed.")
    parser.add_argument("--window-title", default="NWBLoader", help="Fallback title used when the window does not publish a PID.")
    parser.add_argument("--timeout", type=float, default=45.0, help="Seconds to wait for logserver and the testbed window.")
    parser.add_argument("--settle-seconds", type=float, default=2.0, help="Seconds to wait after the window becomes visible.")
    parser.add_argument("--logserver-executable", help="Path to nwb_logserver/logserver. Defaults to a sibling of --executable.")
    parser.add_argument("--no-logserver", action="store_true", help="Do not start a logserver or pass log CLI options.")
    parser.add_argument("--log-port", type=int, default=0, help="Logserver port. Defaults to an unused localhost port.")
    parser.add_argument(
        "--software-vulkan",
        choices=("auto", "on", "off"),
        default="auto",
        help="On Linux, use Mesa lavapipe when available to keep smoke captures stable.",
    )
    parser.add_argument(
        "--application-arg",
        action="append",
        default=[],
        help="Extra argument to pass to the launched application. Repeat for multiple arguments.",
    )
    args = parser.parse_args(argv)

    if args.window_handle is None and not args.executable:
        parser.error("--executable is required unless --window-handle is provided")
    if args.timeout <= 0.0:
        parser.error("--timeout must be positive")
    if args.settle_seconds < 0.0:
        parser.error("--settle-seconds must not be negative")

    args.working_directory = args.working_directory.resolve()
    args.output = args.output.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    backend = None
    try:
        backend = create_capture_backend()
        if args.window_handle is not None:
            result = capture_existing_handle(args, backend)
        else:
            result = launch_and_capture(args, backend)

        write_status(f"captured window 0x{result.handle:x} ({result.width}x{result.height}) -> {args.output}")
        return 0
    except SmokeSkip as exc:
        write_status(f"SKIP: {exc}")
        return SKIP_EXIT_CODE
    except SmokeFailure as exc:
        write_status(f"FAIL: {exc}")
        return 1
    finally:
        if backend:
            backend.close()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
