"""Native combo input builds on the positioned wheel and held-key list helper."""
from __future__ import annotations

from list_native import ListNativeInput


COMBO_KEYS = {"F4": 0x73, "F10": 0x79, "F11": 0x7A, "F12": 0x7B}


class ComboNativeInput(ListNativeInput):
    def key(self, name, down, *, repeat=False, shift=False, control=False):
        if self.windows and name in COMBO_KEYS:
            flags = 1 | ((1 << 30) if repeat or not down else 0) | ((1 << 31) if not down else 0)
            self._post(0x0100 if down else 0x0101, COMBO_KEYS[name], flags)
            return
        super().key(name, down, repeat=repeat, shift=shift, control=control)
