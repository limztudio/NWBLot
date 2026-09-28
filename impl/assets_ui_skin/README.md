# UI skin asset, schema version 1

One skin references one existing `Texture` asset and supplies named atlas regions. Appearance states use semantic region names such as `panel.normal` and `button.hover`; widget-to-state mappings are a later toolkit milestone.

```text
ui_skin asset;

asset.schema_version = 1;
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
```

The atlas extent and rectangle values are unsigned pixel integers. Rectangles use a top-left origin and include `(x, y, width, height)`. Slice and padding arrays use `(left, top, right, bottom)`. Reference density means atlas pixels per logical UI unit; nine-slice border sizes are divided by this density before layout and painting.

`name` and `rect` are required in every region. `draw_mode` defaults to `sprite`; `nine_slice` requires `slice`. Sprites accept an absent or zero slice. Padding and minimum size default to zero and are logical units independent of slice borders. Every logical metric must be finite and nonnegative. Rectangle bounds and opposing slice sums must fit the atlas or region respectively. Names are canonical engine `Name` identities, so duplicates differing only in case are rejected.

Schema version 1 requires between 1 and 4096 regions, inclusive. The public constant `NWB::Impl::s_UiSkinMaxRegionCount` defines this limit. Cooking checks the metadata list count before copying region records, binary loading checks the header count before allocating region storage, and shared asset validation applies the same limit to programmatically constructed skins. This bounds duplicate-name validation and the decoded metadata footprint.

Cooking rejects unknown fields, wrong list lengths, noninteger pixels, unsupported schema versions, invalid bounds, and invalid metrics. The binary codec stores a versioned packed header and fixed-size region records with typed texture identity carried using the existing `NameHash` transport. Loading checks counts against available bytes before allocating, rejects reserved flags and trailing bytes, and activates the candidate payload only after complete validation. A failed load preserves an already loaded skin.

`UiSkin::validateTexture` checks the resolved texture identity, 2D dimension, depth, and dimensions against the atlas. Call it when resolving a skin and its texture together. The texture codec owns texture payload/color/alpha validation; this CPU asset module has no graphics dependency. The asset builder must be given roots containing both skin metadata and the referenced texture metadata/payload. Dependency discovery for targeted builds remains part of the later packaging work.

All consumers must retain the selected skin and texture version for the lifetime of their frame snapshot and GPU work. This module stores asset identity and immutable decoded metadata; GPU residency and frame retirement belong to the UI rendering adapter.

Metadata schema reading lives in `cook_metadata.cpp`; cooked asset construction and binary encoding live in `cook.cpp`. Runtime decoding and generic validation stay in `runtime.cpp`, and the build-pipeline registrar stays in `volume_entry.cpp`.

