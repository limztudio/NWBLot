"""Fixture commands use existing positioned Win32/X11 radio-control input."""
from __future__ import annotations

from popup_tools_native import PopupToolsNativeInput


COMMAND_KEYS = {
    "reverse": "F4",
    "remove_selected": "F5",
    "replace_source": "F6",
    "select_disabled": "F7",
    "toggle_enabled": "F8",
    "close_parent": "F9",
}


class RadioGroupNativeInput(PopupToolsNativeInput):
    def command(self, name):
        """Application commands reach the fixture before the ordinary UI router."""
        if name not in COMMAND_KEYS:
            raise ValueError(f"unknown radio fixture command '{name}'")
        self.tap(COMMAND_KEYS[name])
