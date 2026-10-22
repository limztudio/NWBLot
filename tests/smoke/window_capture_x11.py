"""Linux X11 window-capture backend: Xlib structures, window lookup and raising, XGetImage readback, and synthetic input."""


import ctypes
import time
from window_capture_outcome import SmokeSkip, WindowsDesktopUnavailable, SmokeFailure, write_status  # noqa: E402
from window_capture_image import TransparentCsgAnalysis, TextureSmokeAnalysis, write_bmp_24, capture_result_from_rgb_rows, write_capture_rows, read_bmp_24, read_bmp_24_rows, validate_surfel_coverage_probe, analyze_rgb_rows, transparent_csg_regions, analyze_transparent_csg_rows, analyze_texture_smoke_rows, estimate_background_rgb, count_foreground_pixels_in_region, ximage_rgb_rows, bgra_rgb_rows  # noqa: E402


LIT_WINDOW_POLL_SECONDS = 0.1
LIT_FOCUS_SETTLE_SECONDS = 0.1
LIT_KEY_PRESS_SECONDS = 0.05
LIT_CAPTURE_RAISE_SETTLE_SECONDS = 0.2
LIT_LITTLE = "little"
LIT_BIG = "big"
LIT_X = "x"
LIT_Y = "y"
LIT_UTF_8 = "utf-8"
LIT_REPLACE = "replace"
LIT_WIDTH = "width"
LIT_HEIGHT = "height"
LIT_DEPTH = "depth"
LIT_ROOT = "root"
LIT_TYPE = "type"
LIT_SAME_SCREEN = "same_screen"
LINUX_X_INPUT_EVENT_PREFIX = [(LIT_TYPE, ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int), ("display", ctypes.c_void_p), ("window", ctypes.c_ulong), (LIT_ROOT, ctypes.c_ulong), ("subwindow", ctypes.c_ulong), ("time", ctypes.c_ulong),
    (LIT_X, ctypes.c_int), (LIT_Y, ctypes.c_int), ("x_root", ctypes.c_int), ("y_root", ctypes.c_int), ("state", ctypes.c_uint),]


class LinuxXWindowAttributes(ctypes.Structure):
    _fields_ = [(LIT_X, ctypes.c_int), (LIT_Y, ctypes.c_int), (LIT_WIDTH, ctypes.c_int), (LIT_HEIGHT, ctypes.c_int), ("border_width", ctypes.c_int), (LIT_DEPTH, ctypes.c_int), ("visual", ctypes.c_void_p), (LIT_ROOT, ctypes.c_ulong), ("class", ctypes.c_int),
        ("bit_gravity", ctypes.c_int), ("win_gravity", ctypes.c_int), ("backing_store", ctypes.c_int), ("backing_planes", ctypes.c_ulong), ("backing_pixel", ctypes.c_ulong), ("save_under", ctypes.c_int), ("colormap", ctypes.c_ulong),
        ("map_installed", ctypes.c_int), ("map_state", ctypes.c_int), ("all_event_masks", ctypes.c_long), ("your_event_mask", ctypes.c_long), ("do_not_propagate_mask", ctypes.c_long), ("override_redirect", ctypes.c_int), ("screen", ctypes.c_void_p),]


class LinuxXImageFuncs(ctypes.Structure):
    _fields_ = [("create_image", ctypes.c_void_p), ("destroy_image", ctypes.c_void_p), ("get_pixel", ctypes.c_void_p), ("put_pixel", ctypes.c_void_p), ("sub_image", ctypes.c_void_p), ("add_pixel", ctypes.c_void_p),]


class LinuxXImage(ctypes.Structure):
    pass


class LinuxXKeyEvent(ctypes.Structure):
    _fields_ = LINUX_X_INPUT_EVENT_PREFIX + [("keycode", ctypes.c_uint), (LIT_SAME_SCREEN, ctypes.c_int),]


class LinuxXButtonEvent(ctypes.Structure):
    _fields_ = LINUX_X_INPUT_EVENT_PREFIX + [("button", ctypes.c_uint), (LIT_SAME_SCREEN, ctypes.c_int),]


class LinuxXMotionEvent(ctypes.Structure):
    _fields_ = LINUX_X_INPUT_EVENT_PREFIX + [("is_hint", ctypes.c_char), (LIT_SAME_SCREEN, ctypes.c_int),]


class LinuxXEvent(ctypes.Union):
    _fields_ = [(LIT_TYPE, ctypes.c_int), ("xkey", LinuxXKeyEvent), ("xbutton", LinuxXButtonEvent), ("xmotion", LinuxXMotionEvent), ("pad", ctypes.c_long * 24),]


class LinuxX11Capture:
    KEY_PRESS = 2
    KEY_RELEASE = 3
    BUTTON_PRESS = 4
    BUTTON_RELEASE = 5
    MOTION_NOTIFY = 6
    KEY_PRESS_MASK = 1 << 0
    KEY_RELEASE_MASK = 1 << 1
    BUTTON_PRESS_MASK = 1 << 2
    BUTTON_RELEASE_MASK = 1 << 3
    BUTTON1_MASK = 1 << 8
    POINTER_MOTION_MASK = 1 << 6
    LSB_FIRST = 0
    IS_VIEWABLE = 2
    CURRENT_TIME = 0
    REVERT_TO_PARENT = 2
    BUTTON_LEFT = 1
    XK_RETURN = 0xFF0D
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
        self.x11.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_char_p),]
        self.x11.XFetchName.restype = ctypes.c_int
        self.x11.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint),]
        self.x11.XQueryTree.restype = ctypes.c_int
        self.x11.XGetWindowProperty.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_long, ctypes.c_long, ctypes.c_int, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)),]
        self.x11.XGetWindowProperty.restype = ctypes.c_int
        self.x11.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(self.XWindowAttributes),]
        self.x11.XGetWindowAttributes.restype = ctypes.c_int
        self.x11.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_int,]
        self.x11.XGetImage.restype = ctypes.POINTER(self.XImage)
        self.x11.XDestroyImage.argtypes = [ctypes.POINTER(self.XImage)]
        self.x11.XDestroyImage.restype = ctypes.c_int
        self.x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.POINTER(LinuxXEvent),]
        self.x11.XSendEvent.restype = ctypes.c_int
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.x11.XFlush.restype = ctypes.c_int
        self.x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XKeysymToKeycode.restype = ctypes.c_uint
        self.x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
        self.x11.XStringToKeysym.restype = ctypes.c_ulong
        self.x11.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
        self.x11.XResizeWindow.restype = ctypes.c_int
        self.x11.XRaiseWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.x11.XRaiseWindow.restype = ctypes.c_int
        self.x11.XSetInputFocus.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
        self.x11.XSetInputFocus.restype = ctypes.c_int
        self.x11.XWarpPointer.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint, ctypes.c_int, ctypes.c_int,]
        self.x11.XWarpPointer.restype = ctypes.c_int
        self.x11.XTranslateCoordinates.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_ulong),]
        self.x11.XTranslateCoordinates.restype = ctypes.c_int

    def get_window_pid(self, window):
        actual_type = ctypes.c_ulong()
        actual_format = ctypes.c_int()
        item_count = ctypes.c_ulong()
        bytes_after = ctypes.c_ulong()
        value = ctypes.POINTER(ctypes.c_ubyte)()

        status = self.x11.XGetWindowProperty(self.display, window, self.pid_atom, 0, 1, False, 0, ctypes.byref(actual_type), ctypes.byref(actual_format), ctypes.byref(item_count), ctypes.byref(bytes_after), ctypes.byref(value),)
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
            return value.value.decode(LIT_UTF_8, LIT_REPLACE) if value.value else None
        finally:
            self.x11.XFree(value)

    @staticmethod
    def title_matches(candidate, expected):
        if not expected:
            return True
        if candidate == expected:
            return True
        return bool(candidate and candidate.startswith(f"{expected} "))

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
        status = self.x11.XQueryTree(self.display, window, ctypes.byref(root), ctypes.byref(parent), ctypes.byref(children), ctypes.byref(child_count),)
        if status == 0:
            return []

        try:
            return [children[i] for i in range(child_count.value)]
        finally:
            if children:
                self.x11.XFree(children)

    def find_window_for_pid(self, pid, title, require_mapped=True):
        stack = [self.root]
        matches = []
        while stack:
            window = stack.pop()
            children = self.query_children(window)
            stack.extend(children)

            window_pid = self.get_window_pid(window)
            if title:
                if not self.title_matches(self.get_window_title(window), title):
                    continue
                if window_pid is not None and window_pid != pid:
                    continue
            elif window_pid != pid:
                continue

            attributes = self.get_attributes(window)
            if not attributes:
                continue
            if require_mapped and (attributes.map_state != self.IS_VIEWABLE or attributes.width <= 0 or attributes.height <= 0):
                continue

            matches.append((attributes.width * attributes.height, window))

        if not matches:
            return None

        matches.sort(reverse=True)
        return matches[0][1]

    def wait_for_window(self, pid, timeout_seconds, title=None, require_mapped=True):
        deadline = time.monotonic() + timeout_seconds
        while time.monotonic() < deadline:
            window = self.find_window_for_pid(pid, title, require_mapped=require_mapped)
            if window:
                return window
            time.sleep(LIT_WINDOW_POLL_SECONDS)

        return None

    def send_motion_event(self, window, x, y):
        event = LinuxXEvent()
        event.xmotion.type = self.MOTION_NOTIFY
        self._fill_input_event_prefix(event.xmotion, window, x, y)
        event.xmotion.is_hint = b"\0"
        if not self.x11.XSendEvent(self.display, window, False, self.POINTER_MOTION_MASK, ctypes.byref(event)):
            raise SmokeFailure("failed to send pointer motion")
        self.x11.XFlush(self.display)

    def send_button_event(self, window, event_type, x, y):
        event = LinuxXEvent()
        event.xbutton.type = event_type
        state = self.BUTTON1_MASK if event_type == self.BUTTON_RELEASE else 0
        self._fill_input_event_prefix(event.xbutton, window, x, y, state)
        event.xbutton.button = self.BUTTON_LEFT
        event_mask = self.BUTTON_PRESS_MASK if event_type == self.BUTTON_PRESS else self.BUTTON_RELEASE_MASK
        if not self.x11.XSendEvent(self.display, window, False, event_mask, ctypes.byref(event)):
            raise SmokeFailure("failed to send mouse button event")
        self.x11.XFlush(self.display)

    def send_key_event(self, window, event_type, keycode):
        event = LinuxXEvent()
        event.xkey.type = event_type
        self._fill_input_event_prefix(event.xkey, window, 0, 0)
        event.xkey.keycode = keycode
        event_mask = self.KEY_PRESS_MASK if event_type == self.KEY_PRESS else self.KEY_RELEASE_MASK
        if not self.x11.XSendEvent(self.display, window, False, event_mask, ctypes.byref(event)):
            raise SmokeFailure("failed to send key event")
        self.x11.XFlush(self.display)

    def focus_window(self, window):
        self.x11.XRaiseWindow(self.display, window)
        self.x11.XSetInputFocus(self.display, window, self.REVERT_TO_PARENT, self.CURRENT_TIME)
        self.x11.XFlush(self.display)

    def prepare_window(self, window):
        self.focus_window(window)

    def client_size(self, window):
        return self._validated_window_size(window)

    def resize_client(self, window, width, height, *, allow_outside_work_area=False):
        if not self.x11.XResizeWindow(self.display, window, width, height):
            raise SmokeFailure(f"XResizeWindow failed for window 0x{window:x}")
        self.x11.XFlush(self.display)

    def send_named_key(self, window, key_name):
        keysym = self.x11.XStringToKeysym(str(key_name).encode("ascii"))
        if keysym == 0:
            raise SmokeFailure(f"could not resolve key symbol '{key_name}'")

        keycode = self.x11.XKeysymToKeycode(self.display, keysym)
        if keycode == 0:
            raise SmokeFailure(f"could not resolve keycode '{key_name}'")

        self.focus_window(window)
        time.sleep(LIT_FOCUS_SETTLE_SECONDS)
        self.send_key_event(window, self.KEY_PRESS, keycode)
        time.sleep(LIT_KEY_PRESS_SECONDS)
        self.send_key_event(window, self.KEY_RELEASE, keycode)

    def _validated_window_size(self, window):
        attributes = self.get_attributes(window)
        if not attributes:
            raise SmokeFailure(f"window 0x{window:x} attributes are unavailable")
        if attributes.width <= 0 or attributes.height <= 0:
            raise SmokeFailure(f"window 0x{window:x} has invalid size {attributes.width}x{attributes.height}")
        return attributes.width, attributes.height

    def _window_root_point(self, window, x, y):
        root_x = ctypes.c_int()
        root_y = ctypes.c_int()
        child = ctypes.c_ulong()
        if self.x11.XTranslateCoordinates(self.display, window, self.root, x, y, ctypes.byref(root_x), ctypes.byref(root_y), ctypes.byref(child),) == 0:
            return x, y

        return root_x.value, root_y.value

    def _fill_input_event_prefix(self, event, window, x, y, state=0):
        root_x, root_y = self._window_root_point(window, x, y)
        event.send_event = True
        event.display = self.display
        event.window = window
        event.root = self.root
        event.subwindow = 0
        event.time = self.CURRENT_TIME
        event.x = x
        event.y = y
        event.x_root = root_x
        event.y_root = root_y
        event.state = state
        event.same_screen = True

    def capture_window(self, window, output_path):
        self.x11.XRaiseWindow(self.display, window)
        self.x11.XFlush(self.display)
        time.sleep(LIT_CAPTURE_RAISE_SETTLE_SECONDS)
        self.x11.XSync(self.display, False)
        attributes = self.get_attributes(window)
        if not attributes:
            raise SmokeFailure(f"window 0x{window:x} attributes are unavailable")
        if attributes.width <= 0 or attributes.height <= 0:
            raise SmokeFailure(f"window 0x{window:x} has invalid size {attributes.width}x{attributes.height}")

        result = self._capture_drawable_region(window, window, 0, 0, attributes.width, attributes.height, output_path)
        if result.appears_empty_or_white:
            return result
        if result.has_pixel_variation:
            return result

        write_status(f"window 0x{window:x}: direct XGetImage capture looked flat or white; retrying from root screen")
        root_region = self._window_root_region(window, attributes)
        if root_region:
            root_x, root_y, width, height = root_region
            result = self._capture_drawable_region(self.root, window, root_x, root_y, width, height, output_path)
            if result.has_pixel_variation and not result.appears_empty_or_white:
                return result

        return result

    def capture_client_window(self, window, output_path):
        # The X11 drawable selected for the application is already its client content; keep the M4 capture API portable while Windows explicitly strips the compositor-owned non-client frame below.
        return self.capture_window(window, output_path)

    def capture_prepared_raw_client_window(self, window, output_path):
        width, height = self._validated_window_size(window)
        return self._capture_drawable_region(window, window, 0, 0, width, height, output_path)

    def sample_client_pixels(self, window, x, y, width, height):
        client_width, client_height = self._validated_window_size(window)
        if x < 0 or y < 0 or width <= 0 or height <= 0 or x + width > client_width or y + height > client_height:
            raise SmokeFailure("X11 pixel sample falls outside the client area")
        image = self.x11.XGetImage(self.display, window, x, y, width, height, ctypes.c_ulong(-1).value, self.Z_PIXMAP)
        if not image:
            raise SmokeFailure(f"XGetImage failed for window 0x{window:x}")
        try:
            if image.contents.bits_per_pixel not in (16, 24, 32) or not image.contents.data:
                raise SmokeFailure("X11 pixel sample has unsupported or missing image data")
            byte_order = LIT_LITTLE if image.contents.byte_order == self.LSB_FIRST else LIT_BIG
            return ximage_rgb_rows(image.contents, byte_order)
        finally:
            self.x11.XDestroyImage(image)

    def _window_root_region(self, window, attributes):
        root_x = ctypes.c_int()
        root_y = ctypes.c_int()
        child = ctypes.c_ulong()
        if self.x11.XTranslateCoordinates(self.display, window, self.root, 0, 0, ctypes.byref(root_x), ctypes.byref(root_y), ctypes.byref(child),) == 0:
            return None

        root_attributes = self.get_attributes(self.root)
        if not root_attributes or root_attributes.width <= 0 or root_attributes.height <= 0:
            return None

        capture_x = max(root_x.value, 0)
        capture_y = max(root_y.value, 0)
        skipped_x = capture_x - root_x.value
        skipped_y = capture_y - root_y.value
        width = min(attributes.width - skipped_x, root_attributes.width - capture_x)
        height = min(attributes.height - skipped_y, root_attributes.height - capture_y)
        if width <= 0 or height <= 0:
            return None

        return capture_x, capture_y, width, height

    def _capture_drawable_region(self, drawable, output_handle, x, y, width, height, output_path):
        image = self.x11.XGetImage(self.display, drawable, x, y, width, height, ctypes.c_ulong(-1).value, self.Z_PIXMAP,)
        if not image:
            raise SmokeFailure(f"XGetImage failed for drawable 0x{drawable:x}")

        try:
            return self._write_ximage_bmp(output_handle, image.contents, output_path)
        finally:
            self.x11.XDestroyImage(image)

    def _write_ximage_bmp(self, output_handle, image, output_path):
        bytes_per_pixel = (image.bits_per_pixel + 7) // 8
        if bytes_per_pixel not in (2, 3, 4):
            raise SmokeFailure(f"unsupported XImage pixel size: {image.bits_per_pixel} bits")
        if not image.data:
            raise SmokeFailure(f"XImage for drawable 0x{output_handle:x} has no pixel data")

        byte_order = LIT_LITTLE if image.byte_order == self.LSB_FIRST else LIT_BIG
        rows = ximage_rgb_rows(image, byte_order)
        return write_capture_rows(output_handle, image.width, image.height, rows, output_path)
