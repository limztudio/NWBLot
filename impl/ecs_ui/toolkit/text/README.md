# CPU text foundation

`nwb_ui_text` owns font versions, shaping, multiline label layout, cluster hit testing,
and native grayscale coverage plus optional baked SDF atlas images. It depends publicly
on `nwb_ui` and `nwb_assets_font_atlas`, and privately on the pinned FreeType and HarfBuzz
targets. Its public headers contain no native font, graphics, ECS, ImGui, clipboard,
or IME types.

Construct `TextService` with the UI owner's `Core::Alloc::GlobalArena`. That arena must
outlive the service, every returned layout, every published glyph page, and submitted
paint snapshots. Call `setFonts()` on the owning UI thread with an ordered primary and
fallback list of matching loaded `Font` assets, typed `AssetRef<Font>` identities,
nonzero source generations, and optional `FontAtlas` pointers. The service copies font
bytes before native faces borrow them. FreeType allocations use the supplied arena; native font and shaping owners use
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

## Glyph image selection and painting

Each installed `FontSource` may supply a matching `FontAtlas`. Installation verifies
the typed font identity, exact source SHA-256, face index, units per em, glyph count,
and complete original positioning-table bytes. It copies rendering records and compact SDF
images into a strong immutable `BakedFontAtlas` version. Missing or mismatched optional
atlases leave that same face available through native coverage.

At authoring time, `<stem>.nwb` and `<stem>.font` share one stem.
The readable metadata declares independent `font face` and `font_atlas atlas` assets
with face metrics, bake settings, group dimensions/channels and all glyph mappings;
`asset_bunch bunch = [face, atlas];` exports them
under `/face` and `/atlas`, and `atlas.font = face;` selects the typed source. The
`.font` contains original SFNT and compact images in a FON2 version 1 source envelope.
The readable document has no schema-version or hash fields. Each glyph exposes its
ID, group/channel, rectangle, plane bounds, advance and numeric drawable flag (0 or 1),
including nondrawable records. Cooking admits those mappings, checks source metrics
and image integrity, and copies original positioning tables without rasterizing.
UI text receives the ordinary cooked Font/FON1 and FontAtlas/FTA1 assets.

The baker packs independent scalar SDF pages into one to four stored channels,
cropping each group to its guarded bounds without resampling. Each drawable shaped
glyph selects its actual face's group and channel; padded plane bounds combine with
HarfBuzz's baseline position and offsets. Neither atlas inspection advances nor
exported `kern`/GPOS/GDEF bytes are applied again after shaping.

SDF is selected when physical font size is within 0.75..1.5 times its bake ppem. The
shipped 32-ppem atlases therefore serve 24..48 physical pixels per em. Outside the
interval, native coverage uses the already selected shaping face. Zoom and DPI can
change the image source without changing logical layout. Below the interval, native
coverage also preserves the antialiased bottom row of small descenders. Current atlas
quality tests compare Latin A/o/e and Korean Hangul against supersampled native
coverage across zoom and DPI; other fonts require their own visual qualification.
Scalar SDF does not guarantee arbitrary magnification or recover details missing from its bake.

Layouts retain their exact source font and baked image versions. Paint snapshots pin
immutable compact SDF pages after source asset release or font replacement. Warm SDF painting
and supported DPI changes reuse the same pages; native coverage maintains a separate
raster cache. Both image kinds are admitted atomically before quads are emitted.

## Visible glyph preparation

`TextLayout` keeps every source byte, line, cluster, position and caret edge, including
text outside the viewport. Layout and native glyph preparation run on the owning UI
thread. The layout also retains per-glyph shaping ink and separate copied
native coverage bounds when HarfBuzz extents may describe a different image. Ordinary
static TrueType outlines use conservative shaping bounds. Non-tricky CFF and fonts
with bitmap/color extent tables use transformed unscaled FreeType outline control
boxes, including native font/subfont matrices. These bounds do not replace shaping
ink, advance measurement or selection geometry. Tricky faces and unavailable bounds
remain conservative candidates.

Painting reads `PaintBuilder::currentClip()` and validates candidates before atlas
preparation. Coverage bounds include scale rounding and the raster fringe; baked SDF
candidates use the complete padded plane rectangle. Only candidates prepare dynamic
atlas entries. The exact prepared bitmap rectangles then exclude hidden images before
one combined coverage/SDF page admission and ordered quad emission. Empty clips and
zero alpha prepare no glyphs or image bindings after normal parameter validation.

This removes hidden-document atlas exhaustion without changing resource limits. A
conservative candidate can still warm the coverage cache even when its final bitmap
is outside the clip. Cache limits apply cumulatively to glyphs encountered by visible
views; eviction, cache tuning and performance measurement remain later work.

## Native coverage fallback

`TextService::paint()` reads the builder's saved display metrics. It rasterizes at
`ceil(fontSize * max(pixelScaleX, pixelScaleY))`, with hinting disabled, while layout
measurement stays in logical units. Raster bitmap bounds are converted back using
the actual raster scale, then their top-left edges are snapped to the physical pixel
grid before drawing. Shaping positions, caret geometry, and baked SDF quads remain
fractional. Shaped ink bounds and raster pixel bounds can differ by the bitmap's
integer fringe.

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
    { latinRef, latinFont, latinGeneration, &latinAtlas },
    { koreanRef, koreanFont, koreanGeneration, &koreanAtlas },
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

The [offline font-atlas contract](../../../../docs/font_atlas_plan.md) and
[utility README](../../../../utilities/font_builder/README.md) describe generation,
lossless payloads, positioning-table export, qualified scale, and GPU ownership.
`UiFontBinding` in the ECS adapter selects typed font/atlas references explicitly;
the text service itself consumes already loaded CPU assets and owns no loader,
clipboard, or IME service.
