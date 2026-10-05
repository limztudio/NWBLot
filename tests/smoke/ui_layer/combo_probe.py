"""Compare combo source state, accepted geometry, and matching GPU marker pixels."""
from __future__ import annotations

from combo_probe_common import ComboProbe

FIELDS = ("selected", "cursor", "count", "first", "past", "label_reads", "open", "focused", "reversed", "removed",
    "visible", "commits", "underlying", "enabled", "bottom", "source_generation")
RECTANGLES = ("trigger", "popup", "list", "viewport", "track", "thumb", "cursor_row", "counter")

COMBO_PROBE = ComboProbe("UiComboSmoke", FIELDS, RECTANGLES)
