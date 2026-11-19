from __future__ import annotations

from popup_tools_native import PopupToolsNativeInput


class NestedPopupNativeInput(PopupToolsNativeInput):
    def close_ancestor(self):
        self.tap("F8")
