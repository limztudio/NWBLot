"""X11 synthetic input events and coordinate projection."""


import ctypes

from window_capture_outcome import SmokeFailure


LIT_TYPE = "type"
LIT_ROOT = "root"
LIT_X = "x"
LIT_Y = "y"
LIT_SAME_SCREEN = "same_screen"
LINUX_X_INPUT_EVENT_PREFIX = [(LIT_TYPE, ctypes.c_int), ("serial", ctypes.c_ulong), ("send_event", ctypes.c_int), ("display", ctypes.c_void_p), ("window", ctypes.c_ulong), (LIT_ROOT, ctypes.c_ulong), ("subwindow", ctypes.c_ulong), ("time", ctypes.c_ulong),
    (LIT_X, ctypes.c_int), (LIT_Y, ctypes.c_int), ("x_root", ctypes.c_int), ("y_root", ctypes.c_int), ("state", ctypes.c_uint),]


class LinuxXKeyEvent(ctypes.Structure):
    _fields_ = LINUX_X_INPUT_EVENT_PREFIX + [("keycode", ctypes.c_uint), (LIT_SAME_SCREEN, ctypes.c_int),]


class LinuxXButtonEvent(ctypes.Structure):
    _fields_ = LINUX_X_INPUT_EVENT_PREFIX + [("button", ctypes.c_uint), (LIT_SAME_SCREEN, ctypes.c_int),]


class LinuxXMotionEvent(ctypes.Structure):
    _fields_ = LINUX_X_INPUT_EVENT_PREFIX + [("is_hint", ctypes.c_char), (LIT_SAME_SCREEN, ctypes.c_int),]


class LinuxXEvent(ctypes.Union):
    _fields_ = [(LIT_TYPE, ctypes.c_int), ("xkey", LinuxXKeyEvent), ("xbutton", LinuxXButtonEvent), ("xmotion", LinuxXMotionEvent), ("pad", ctypes.c_long * 24),]


class X11InputSender:
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
    CURRENT_TIME = 0
    BUTTON_LEFT = 1

    def __init__(self, x11, display, root):
        self.x11 = x11
        self.display = display
        self.root = root

    @staticmethod
    def bind(x11):
        x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.POINTER(LinuxXEvent),]
        x11.XSendEvent.restype = ctypes.c_int

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

