"""Native searchable combo text, clipboard chords, and result navigation."""
from __future__ import annotations

import time

from combo_native import ComboNativeInput
from window_capture_smoke import SmokeFailure


class SearchComboNativeInput(ComboNativeInput):
    def text(self, value, *, settle=True):
        if self.windows:
            super().text(value, settle=settle)
            return
        for character in value:
            if not character.isascii() or not (character.isalnum() or character == " "):
                raise SmokeFailure(f"unsupported synthetic X11 search character '{character}'")
            name = "space" if character == " " else character
            self.key(name, True)
            self.key(name, False)
        if settle:
            time.sleep(0.15)

    def replace_query(self, value):
        self.tap("a", control=True)
        if value:
            self.text(value)
        else:
            self.tap("BackSpace")
