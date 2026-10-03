# UI skin asset

One skin references one existing `Texture` asset and supplies named atlas regions. Appearance states use semantic region names such as `panel.normal` and `button.hover`. The example below is a generic partial skin; a skin selected for the stock widget toolkit must satisfy the `widgets` contract described below.

```text
ui_skin asset;

asset.texture = "engine/ui/skins/default/texture";
asset.atlas_extent = [256, 256];
asset.reference_density = 1.0;
asset.regions = [
    {
        "name": "panel.normal",
        "rect": [2, 2, 32, 32],
        "draw_mode": "nine_slice",
        "slice": [4, 4, 4, 4],
        "padding": [8.0, 8.0, 8.0, 8.0],
        "minimum_size": [16.0, 16.0]
    },
    { "name": "combo.arrow", "rect": [36, 2, 12, 12] }
];
asset.colors = [
    {"name": "text.normal", "rgba": [0.92, 0.94, 0.98, 1.0]},
    {"name": "text.disabled", "rgba": [0.48, 0.5, 0.55, 1.0]},
    {"name": "text.tooltip", "rgba": [1.0, 1.0, 1.0, 1.0]},
    {"name": "edit.background", "rgba": [0.08, 0.1, 0.14, 1.0]},
    {"name": "edit.selection", "rgba": [0.2, 0.4, 0.78, 0.75]},
    {"name": "edit.inactive_selection", "rgba": [0.28, 0.31, 0.38, 0.55]},
    {"name": "edit.caret", "rgba": [0.95, 0.97, 1.0, 1.0]},
    {"name": "edit.preedit", "rgba": [0.5, 0.72, 1.0, 1.0]},
    {"name": "scrollbar.track", "rgba": [0.08, 0.1, 0.14, 1.0]},
    {"name": "scrollbar.thumb", "rgba": [0.35, 0.4, 0.48, 1.0]},
    {"name": "scrollbar.disabled", "rgba": [0.25, 0.28, 0.32, 1.0]},
    {"name": "popup.backdrop", "rgba": [0.0, 0.0, 0.0, 0.4]},
    {"name": "control.hover_tint", "rgba": [1.08, 1.08, 1.08, 1.0]},
    {"name": "control.pressed_tint", "rgba": [0.85, 0.85, 0.85, 1.0]},
    {"name": "control.disabled_tint", "rgba": [0.55, 0.55, 0.55, 0.6]},
    {"name": "progress.track_tint", "rgba": [1.0, 1.0, 1.0, 1.0]},
    {"name": "progress.fill_tint", "rgba": [1.0, 1.0, 1.0, 1.0]},
];
asset.typography = {"default_font_size": 16.0};
```

Authored `.nwb` metadata uses the current skin fields without a version, schema-version, or revision field. Every authored skin requires all 17 `asset.colors` roles shown above, each exactly once, and `asset.typography.default_font_size`. Palette records have a unique `name` and four-element `rgba` array. RGB is linear and finite in `[0, 16]`; alpha is finite in `[0, 1]`. Values above 1 in RGB allow the 1.08 hover tint.

The default font size is in logical UI units and must be finite and within `[1/64, 2048]`, matching text shaping. The builder adopts the selected skin's size when its current font size is still the previous skin default; an application-set `Builder::style().fontSize` value that differs from that default is retained across skin changes. Applications can set that style before declaring individual widgets to override the default.

Programmatic skins always own a complete palette and typography. `UiSkinPalette{}` supplies the engine color defaults shown above, and `UiSkinTypography{}` supplies the 16-unit size. `setAtlas()` restores those complete defaults; `setPalette()` and `setTypography()` replace them. There is no absent-palette or absent-typography state.

The atlas extent and rectangle values are unsigned pixel integers. Rectangles use a top-left origin and include `(x, y, width, height)`. Slice and padding arrays use `(left, top, right, bottom)`. Reference density means atlas pixels per logical UI unit; nine-slice border sizes are divided by this density before layout and painting.

`name` and `rect` are required in every region. `draw_mode` defaults to `sprite`; `nine_slice` requires `slice`. Sprites omit `slice`; the field validator rejects it even when all four values are zero. Padding and minimum size default to zero and are logical units independent of slice borders. Every logical metric must be finite and nonnegative. Rectangle bounds and opposing slice sums must fit the atlas or region respectively. Names are canonical engine `Name` identities, so duplicates differing only in case are rejected.

Skins require between 1 and 4096 regions, inclusive. The public constant `NWB::Impl::s_UiSkinMaxRegionCount` defines this limit. Cooking checks the metadata list count before copying region records, binary loading checks the header count before allocating region storage, and shared asset validation applies the same limit to programmatically constructed skins. This bounds duplicate-name validation and the decoded metadata footprint.

Cooking rejects unknown fields (including authoring version/revision fields), wrong list lengths, noninteger pixels, invalid bounds, and invalid metrics. The binary codec writes internal payload version 3: a packed header, fixed-size region records, 17 packed linear RGBA records in `UiSkinColorRole` order, and one packed default font size. Typed texture identity uses the existing `NameHash` transport. Loading rejects every other version, checks counts against available bytes before allocating, rejects reserved flags, malformed palette or typography values and trailing bytes, and activates the candidate payload only after complete validation. A failed load preserves an already loaded skin. Update obsolete authoring fields and recook older payloads; the runtime does not convert them.

The stock widget toolkit requires these base regions: `panel.normal`; `window.normal`, `window.title`, `window.collapse`; `separator`; `button.normal`; `checkbox.normal`, `checkbox.mark`; `edit.normal`; `list.background`, `list.row.normal`, `list.row.hover`, `list.row.selected`, `list.row.disabled`; `scroll.track`, `scroll.thumb`; `scrollbar.track`, `scrollbar.thumb.normal`; `popup.normal`; `tooltip.normal`; `combo.normal`, `combo.arrow`; `radio.normal`, `radio.checked`, `radio.mark`; `slider.track`, `slider.thumb.normal`; `progress.track`, `progress.fill`; and `focus.overlay`. Resizable windows additionally require either `window.resize` or `white`. The engine default and smoke-test alternate atlases satisfy this contract. `window.close`, `scrollbar.arrow.*`, `scrollbar.thumb.hover`, and `list.normal` are artwork aliases that the stock widgets do not currently use, so they are optional.

The fixed `widgets` state fallbacks are within the selected skin: absent `button.hover/pressed/disabled` use `button.normal`; absent `checkbox.hover/checked/disabled` use `checkbox.normal` (the checked mark remains required); absent `edit.hover/focused/disabled` use `edit.normal`; absent `combo.hover/open/focused/disabled` use `button.normal`; absent `radio.hover/pressed/disabled` use `radio.normal`; and absent `slider.thumb.hover/pressed/disabled` use `slider.thumb.normal`. The list scrollbar's hover uses `button.hover` and then `button.normal`; text-area scrollbar interaction uses the corresponding button state and then `button.normal`. These are the painter's existing fallbacks, not permission to mix artwork from the engine-default skin. Custom widget style names and arbitrary `Builder::image(regionName)` references are validated when used, outside this stock contract.

Add `asset.toolkit_contract = "widgets";` to a complete skin's metadata to reject missing required parts during cooking with a named-region diagnostic. This optional authoring field does not change the binary payload. Generic partial skins remain valid assets, but `UiLayerSystem` checks the same contract before selecting any startup or live skin, including generic current-format assets without the optional authoring field. A rejected startup custom skin falls back to the engine default; a rejected live replacement keeps the active skin and generation.

`UiSkin::validateTexture` checks the resolved texture identity, 2D dimension, depth, and dimensions against the atlas. Call it when resolving a skin and its texture together. The texture codec owns texture payload/color/alpha validation; this CPU asset module has no graphics dependency. The asset builder must be given roots containing both skin metadata and the referenced texture metadata/payload. For targeted builds, `pipeline/launch.py --include-skin-dependencies` expands selected skin metadata into its typed texture metadata dependency before cooking. The dependency tool accepts the same opt-in flag with `--repo-root` and repeated `--asset-root` values. It retains the original input list exactly and appends each missing texture metadata provider once. The texture cooker owns the paired encoded payload; it is not added as a separate pipeline input. Provider ownership follows asset-root order, and unresolved, wrong-type or ambiguous texture references fail before the output list is written. Without the flag, dependency computation remains pass-through.

All consumers must retain the selected skin and texture version for the lifetime of their frame snapshot and GPU work. This module stores asset identity and immutable decoded metadata; GPU residency and frame retirement belong to the UI rendering adapter.

Metadata schema reading lives in `cook_metadata.cpp`; cooked asset construction and binary encoding live in `cook.cpp`. Runtime decoding and generic validation stay in `runtime.cpp`, and the build-pipeline registrar stays in `volume_entry.cpp`.

The dependency extractor in `dependencies.cpp` reuses the full skin metadata parser and publishes a typed texture reference only after successful validation. It performs no texture loading or cooking. The pipeline provider catalog resolves metadata identities without reading unrelated asset documents.
