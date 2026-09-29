// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "text/glyph_page.h"
#include "text/sdf_page.h"

#include <impl/assets_ui_skin/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Point{
    f32 x = 0.0f;
    f32 y = 0.0f;
};

struct Rect{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
};

struct Insets{
    f32 left = 0.0f;
    f32 top = 0.0f;
    f32 right = 0.0f;
    f32 bottom = 0.0f;
};

// Input colors use linear RGB and straight alpha; frozen vertices use premultiplied alpha.
struct Color{
    f32 r = 1.0f;
    f32 g = 1.0f;
    f32 b = 1.0f;
    f32 a = 1.0f;
};

struct Vertex{
    Point position;
    Point texCoord;
    Color color;
};

struct DisplayMetrics{
    f32 logicalWidth = 0.0f;
    f32 logicalHeight = 0.0f;
    f32 pixelScaleX = 1.0f;
    f32 pixelScaleY = 1.0f;
};

namespace PaintMaterial{
    enum Enum : u8{ Solid, Skin, Glyph, SdfGlyph };
};

struct DrawCommand{
    u32 firstIndex = 0u;
    u32 indexCount = 0u;
    Rect clip;
    PaintMaterial::Enum material = PaintMaterial::Solid;
    u32 glyphPageIndex = Limit<u32>::s_Max;
    u32 sdfPageIndex = Limit<u32>::s_Max;
    u32 sdfChannel = 0u;
    // Base paint is layer zero; higher layers render later without changing owned index or image bindings.
    u32 layer = 0u;
};

// Typed references identify assets; this copy captures atlas metadata, not loaded/GPU resource ownership.
struct SkinBinding{
    Core::Assets::AssetRef<UiSkin> skin;
    Core::Assets::AssetRef<Texture> texture;
    u64 generation = 0u;
    u32 atlasWidth = 0u;
    u32 atlasHeight = 0u;
    f32 referenceDensity = 1.0f;
};

inline constexpr usize s_PaintMaxImages = s_PaintMaxGlyphPages;
inline constexpr usize s_PaintMaxOverlayDepth = 16u;

template<typename T>
using PaintVector = Vector<T, Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The caller's arena must outlive the snapshot and every consumer of it.
class DrawSnapshot final{
    friend class PaintBuilder;


public:
    explicit DrawSnapshot(Core::Alloc::GlobalArena& arena);
    DrawSnapshot(DrawSnapshot&&) = default;
    DrawSnapshot& operator=(DrawSnapshot&&) = default;


public:
    DrawSnapshot(const DrawSnapshot&) = delete;
    DrawSnapshot& operator=(const DrawSnapshot&) = delete;


public:
    [[nodiscard]] u64 generation()const{ return m_generation; }
    [[nodiscard]] const DisplayMetrics& displayMetrics()const{ return m_displayMetrics; }
    [[nodiscard]] const SkinBinding& skinBinding()const{ return m_skinBinding; }
    [[nodiscard]] const PaintVector<Vertex>& vertices()const{ return m_vertices; }
    [[nodiscard]] const PaintVector<u32>& indices()const{ return m_indices; }
    [[nodiscard]] const PaintVector<DrawCommand>& commands()const{ return m_commands; }
    [[nodiscard]] const PaintVector<SharedGlyphPage>& glyphPages()const{ return m_glyphPages; }
    [[nodiscard]] const PaintVector<SharedSdfAtlasPage>& sdfPages()const{ return m_sdfPages; }


private:
    u64 m_generation = 0u;
    DisplayMetrics m_displayMetrics;
    SkinBinding m_skinBinding;
    PaintVector<Vertex> m_vertices;
    PaintVector<u32> m_indices;
    PaintVector<DrawCommand> m_commands;
    PaintVector<SharedGlyphPage> m_glyphPages;
    PaintVector<SharedSdfAtlasPage> m_sdfPages;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PaintBuilder final : NoCopy{
public:
    explicit PaintBuilder(Core::Alloc::GlobalArena& arena);


public:
    // Skin payload validation belongs to loading; begin captures its descriptor for this frame.
    void begin(
        const DisplayMetrics& metrics,
        u64 generation,
        u64 skinGeneration,
        const Core::Assets::AssetRef<UiSkin>& skinRef,
        const UiSkin& skin
    );
    void reserve(usize quadCount, usize clipDepth = 8u);
    void pushClip(const Rect& clip);
    [[nodiscard]] bool popClip();
    // Each bounded overlay escapes parent clipping and restores that clip and layer at its balanced end.
    [[nodiscard]] bool beginOverlay(u32 layer);
    [[nodiscard]] bool endOverlay();
    void fillRect(const Rect& rectangle, const Color& color = {});
    [[nodiscard]] bool drawRegion(const Name& regionName, const Rect& rectangle, const Color& tint = {});
    // Admit all pages before emitting a label. Failure preserves existing bindings and geometry.
    [[nodiscard]] bool prepareGlyphPages(const SharedGlyphPage* pages, usize count);
    [[nodiscard]] bool drawGlyph(const SharedGlyphPage& page, const Rect& rectangle, const Rect& uv, const Color& tint = {});
    [[nodiscard]] bool prepareSdfPages(const SharedSdfAtlasPage* pages, usize count);
    [[nodiscard]] bool prepareImages(
        const SharedGlyphPage* glyphPages, usize glyphCount,
        const SharedSdfAtlasPage* sdfPages, usize sdfCount
    );
    [[nodiscard]] bool drawSdfGlyph(
        const SharedSdfAtlasPage& page, u32 channel, const Rect& rectangle, const Rect& uv, const Color& tint = {}
    );
    [[nodiscard]] DrawSnapshot freeze();
    [[nodiscard]] const DisplayMetrics& displayMetrics()const{ return m_snapshot.displayMetrics(); }
    [[nodiscard]] Rect currentClip()const;


private:
    void emitQuad(
        const Rect& rectangle, const Rect& uv, const Color& color, PaintMaterial::Enum material,
        u32 glyphPageIndex = Limit<u32>::s_Max, u32 sdfPageIndex = Limit<u32>::s_Max, u32 sdfChannel = 0u
    );
    void emitNineSlice(const UiSkinRegion& region, const Rect& rectangle, const Color& tint);


private:
    struct OverlayFrame{
        usize clipDepth = 0u;
        u32 layer = 0u;
    };


private:
    DrawSnapshot m_snapshot;
    PaintVector<UiSkinRegion> m_regions;
    PaintVector<Rect> m_clips;
    OverlayFrame m_overlays[s_PaintMaxOverlayDepth];
    usize m_overlayDepth = 0u;
    u32 m_layer = 0u;
    bool m_recording = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

