"""Check searchable combo source/query snapshots against the composed GPU layer."""
from __future__ import annotations

from combo_probe_common import ComboProbe

FIELDS = ("selected", "cursor", "full_count", "count", "first", "past", "label_reads", "open", "query_bytes",
    "query_hash", "query_anchor", "query_caret", "query_focused", "focused", "commits", "filter_revision", "reversed",
    "removed", "enabled", "source_revision", "underlying", "composing")
RECTANGLES = ("trigger", "popup", "list", "viewport", "track", "thumb", "cursor_row", "counter", "query",
    "query_content", "query_caret", "query_selection")

SEARCH_COMBO_PROBE = ComboProbe("UiSearchComboSmoke", FIELDS, RECTANGLES, searchable=True)
