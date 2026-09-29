"""Nested user-popup input uses the fixture-local physical Win32 hover transport."""
from __future__ import annotations

from popup_tools_native import PopupToolsNativeInput


class NestedPopupNativeInput(PopupToolsNativeInput):
    """Ancestor closing is an application command, separate from popup Escape."""

    def close_ancestor(self):
        self.tap("F8")

