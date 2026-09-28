# CPU text foundation

`nwb_ui_text` owns font versions, shaping, multiline label layout, cluster hit testing,
and the current grayscale coverage atlas. It depends publicly on `nwb_ui` and privately
on the pinned FreeType and HarfBuzz targets. Its public headers contain no native font,
graphics, ECS, ImGui, clipboard, or IME types.

Construct `TextService` with the UI owner's `Core::Alloc::GlobalArena`. That arena must
outlive the service, every returned layout, every published glyph page, and submitted
paint snapshots. Call `setFonts()` on the owning UI thread with an ordered primary and
fallback list of matching loaded `Font` assets, typed `AssetRef<Font>` identities, and
nonzero source generations. The service copies font bytes before native faces borrow
them. FreeType allocations use the supplied arena; native font and shaping owners use
RAII. Layout glyphs retain strong references to the exact CPU font versions needed for
later rasterization. An asset reference alone does not retain loaded font data.

`setFonts()` is failure atomic. A successful call advances `generation()` and starts a
new atlas identity. `identity()` remains unique to the service. Clients that cache
layouts compare both values so replacing fonts or moving a label between services
invalidates its layout. Replacing a source asset or font list cannot change an existing
layout's bytes, glyph IDs, metrics, or retained CPU versions.

## Shaping and supported scope

`ShapeRequest` supplies UTF-8 text, logical font size, horizontal direction, an ISO 15924
script tag, and an ASCII language tag. Defaults are 16 logical units, left-to-right,
`Latn`, and `en`. Korean runs should explicitly select `Hang` and `ko`. Requests describe
one explicit script/direction policy; this increment does not discover mixed paragraph
scripts or implement Unicode paragraph bidi. LF and CRLF produce explicit lines.
Tabs, bare CR, other control characters, and Unicode line/paragraph separators return
`UnsupportedControl`. Text must be valid scalar UTF-8 without embedded NUL.

The concrete `TextShaper` uses HarfBuzz OpenType font functions and its default shaping
features. It preserves monotone source-byte clusters and chooses a covering fallback
face for each missing primary cluster. Adjacent clusters assigned to the same fallback
face are shaped together, preserving same-font ligatures and contextual shaping.
The cluster is indivisible during fallback; combining marks are not independently
assigned to another face. A cluster unavailable in every configured face returns
`MissingGlyph` rather than silently substituting unrelated text.

Deterministic fixtures qualify the bundled Noto Latin and Korean faces, including
Latin kerning, ligatures, combining marks, precomposed Hangul, and decomposed Jamo.
Explicit right-to-left byte mapping is tested, but the bundled font set does not
establish complete Arabic, Indic, emoji, or arbitrary-script support. Loading another
font does not establish paragraph bidi, Unicode line breaking, or native IME support.

## Layout, measurement, and hit testing

`TextLayoutBuilder` consumes `ITextShaper`, using the project's `interface` keyword,
and publishes a move-only `TextLayout`. `layout()` returns `TextLayoutStatus` and leaves
the previous output unchanged on failure. The layout owns copied UTF-8 text, positioned
glyphs, cluster records, and line records in the builder's arena. It has no automatic
wrapping, alignment, editing model, selection, or undo state.

All positions use logical units with a top-left origin. Glyph positions identify their
baseline origin plus shaping offsets. `measure()` reports maximum line advance and
total typographic height. It includes spaces and empty lines. `inkBounds()` reports the
union of actual shaped glyph bounds, including negative bearings and overhangs; it can
extend beyond advance measurement. A trailing newline creates an empty final line.
Empty text has one typographic line and no glyphs or ink.

Clusters map absolute UTF-8 `[byteBegin, byteEnd)` ranges to glyph spans and logical
leading/trailing caret edges. RTL cluster byte ends follow logical source order, even
though glyphs arrive in reversed visual order. `hitTest()` chooses the nearest edge
on the vertically selected line, includes space advances, and clamps outside the line.
Midpoint ties select a leading edge. `caretRect()` accepts available cluster or empty
line boundaries and returns a zero-width logical insertion rectangle; a painter or
OS adapter chooses the caret's visible/physical width. Offsets inside a ligature,
combining cluster, UTF-8 sequence, or CRLF separator are rejected.

These APIs expose shaped cluster edges. They do not implement UAX #29 grapheme-based
editing, intra-ligature caret positions, or complete visual selection/navigation.
Those policies require a separate editing increment.

## Current coverage painting

`TextService::paint()` reads the builder's saved display metrics. It rasterizes at
`ceil(fontSize * max(pixelScaleX, pixelScaleY))`, with hinting disabled, while layout
measurement stays in logical units. Raster bitmap bounds are converted back using
the actual raster scale. Shaped ink bounds and raster pixel bounds can differ by
the bitmap's integer fringe.

Glyph coverage is linear R8 with one mip and a transparent one-pixel packing border.
The cache key includes the exact CPU font version, glyph ID, and physical pixel size.
Each page belongs to one font revision and may contain several pixel sizes. Pixels and
coordinates are append-only within an atlas identity. A dirty page publishes a copied
immutable version after glyph preparation; warm paint calls reuse its exact version.
Old snapshots retain their earlier page bytes. Repacking or clearing uses a new atlas
identity. The GPU owner resolves/uploads exact page versions and retains them through
submission completion.

The service admits every glyph and page binding before emitting any label quads.
Capacity or raster failure leaves existing builder geometry and page bindings intact,
although successful cache preparation may remain available for a retry. Limits are
eight configured faces, 1 MiB per text request, 4096 cached glyph entries, and sixteen
512 x 512 atlas pages. Exceeding a limit returns failure. Input tint is linear straight
RGBA; the paint builder premultiplies vertices, and the glyph shader multiplies their
premultiplied color and alpha by sampled coverage.

This example assumes the matching font/skin assets and typed identities are already
loaded:

```cpp
Core::Alloc::GlobalArena uiArena(Name("runtime/ui"));
Ui::TextService text(uiArena);
const Ui::FontSource fonts[]{
    { latinRef, latinFont, latinGeneration },
    { koreanRef, koreanFont, koreanGeneration },
};
if(!text.setFonts(fonts, 2u))
    return false;

Ui::TextLayout label(uiArena);
Ui::ShapeRequest request{ "Hello, custom UI" };
if(text.layout(request, label) != Ui::TextLayoutStatus::Success)
    return false;

Ui::PaintBuilder paint(uiArena);
paint.begin({ 800.0f, 600.0f, 1.5f, 1.5f }, frameGeneration, skinGeneration, skinRef, skin);
paint.pushClip({ 20.0f, 20.0f, 300.0f, 100.0f });
if(!text.paint(paint, label, { 24.0f, 24.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }))
    return false;
if(!paint.popClip())
    return false;
Ui::DrawSnapshot snapshot = paint.freeze();
// The owner keeps uiArena alive until every submitted consumer of snapshot completes.
```

The proposed next rendering increment is described in the
[offline font-atlas plan](../../../docs/font_atlas_plan.md): a baker that packs
independent scalar SDF pages into RGBA channels and writes glyph/atlas metrics plus
kerning to `.nwb`. That authored representation will replace or complement runtime
coverage rasterization. HarfBuzz shaping and byte-cluster layout remain necessary for
ligatures, combining marks, Korean shaping, and other script behavior beyond pair
kerning. This module currently renders coverage, not signed distance fields.
