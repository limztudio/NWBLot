"""Native multiline input and reads of the actual Win32/X11 clipboard.

Posted Win32 characters bypass live IME. The X11 path uses the existing ASCII
key transport and UTF8_STRING clipboard reader; neither path claims Wayland.
"""
from __future__ import annotations

from numeric_edit_native import NumericEditNativeInput


class TextAreaNativeInput(NumericEditNativeInput):
    def text_down_text(self, first, last):
        """Preserve native event order without an intermediate display wait."""
        self.text(first, settle=False)
        self.key("Down", True)
        self.key("Down", False)
        self.text(last, settle=False)
