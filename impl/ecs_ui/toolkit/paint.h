// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "images/image_source.h"
#include "text/glyph_page.h"
#include "text/sdf_page.h"

#include <impl/assets_ui_skin/asset.h>
#include <impl/ecs_ui/toolkit/paint_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Typed references identify assets; this copy captures atlas metadata, not loaded/GPU resource ownership.
struct SkinBinding{
    Core::Assets::AssetRef<UiSkin> skin;
    Core::Assets::AssetRef<Texture> texture;
    u64 generation = 0u;
    u32 atlasWidth = 0u;
    u32 atlasHeight = 0u;
    f32 referenceDensity = 1.0f;
};

template<typename T>
using PaintVector = Vector<T, Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The caller's arena must outlive the snapshot and every consumer of it.
class DrawSnapshot final{
    friend class PaintBuilder;


public:
    explicit DrawSnapshot(Core::Alloc::GlobalArena& arena)noexcept;
    DrawSnapshot(DrawSnapshot&&)noexcept = default;
    DrawSnapshot(const DrawSnapshot&) = delete;


public:
    DrawSnapshot& operator=(DrawSnapshot&&)noexcept = default;

    DrawSnapshot& operator=(const DrawSnapshot&) = delete;


public:
    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }

    [[nodiscard]] const DisplayMetrics& displayMetrics()const noexcept{ return m_displayMetrics; }

    [[nodiscard]] const SkinBinding& skinBinding()const noexcept{ return m_skinBinding; }

    [[nodiscard]] const PaintVector<Vertex>& vertices()const noexcept{ return m_vertices; }

    [[nodiscard]] const PaintVector<u32>& indices()const noexcept{ return m_indices; }

    [[nodiscard]] const PaintVector<DrawCommand>& commands()const noexcept{ return m_commands; }

    [[nodiscard]] const PaintVector<SharedGlyphPage>& glyphPages()const noexcept{ return m_glyphPages; }

    [[nodiscard]] const PaintVector<SharedSdfAtlasPage>& sdfPages()const noexcept{ return m_sdfPages; }

    [[nodiscard]] const PaintVector<SharedImageSource>& textureImages()const noexcept{ return m_textureImages; }


private:
    u64 m_generation = 0u;
    DisplayMetrics m_displayMetrics;
    SkinBinding m_skinBinding;
    PaintVector<Vertex> m_vertices;
    PaintVector<u32> m_indices;
    PaintVector<DrawCommand> m_commands;
    PaintVector<SharedGlyphPage> m_glyphPages;
    PaintVector<SharedSdfAtlasPage> m_sdfPages;
    PaintVector<SharedImageSource> m_textureImages;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PaintBuilder final : NoCopy{
private:
    struct OverlayFrame{
        usize clipDepth = 0u;
        u32 layer = 0u;
    };


public:
    explicit PaintBuilder(Core::Alloc::GlobalArena& arena)noexcept;


public:
    // Skin payload validation belongs to loading; begin captures its descriptor for this frame.
    void begin(const DisplayMetrics& metrics, u64 generation, u64 skinGeneration, const Core::Assets::AssetRef<UiSkin>& skinRef, const UiSkin& skin);
    void reserve(usize quadCount, usize clipDepth = s_PaintDefaultClipDepth);
    void pushClip(const Rect& clip);
    [[nodiscard]] b8 popClip()noexcept;
    // Each bounded overlay escapes parent clipping and restores that clip and layer at its balanced end.
    [[nodiscard]] b8 beginOverlay(u32 layer);
    [[nodiscard]] b8 endOverlay()noexcept;
    void fillRect(const Rect& rectangle, const Color& color = {});
    [[nodiscard]] b8 drawRegion(const Name& regionName, const Rect& rectangle, const Color& tint = {});
    // Admit all pages before emitting a label. Failure preserves existing bindings and geometry.
    [[nodiscard]] b8 prepareGlyphPages(const SharedGlyphPage* pages, usize count);
    [[nodiscard]] b8 drawGlyph(const SharedGlyphPage& page, const Rect& rectangle, const Rect& uv, const Color& tint = {});
    [[nodiscard]] b8 prepareSdfPages(const SharedSdfAtlasPage* pages, usize count);
    [[nodiscard]] b8 prepareImages(const SharedGlyphPage* glyphPages, usize glyphCount, const SharedSdfAtlasPage* sdfPages, usize sdfCount, const SharedImageSource* textureImages = nullptr, usize textureCount = 0u);
    [[nodiscard]] b8 drawSdfGlyph(const SharedSdfAtlasPage& page, u32 channel, const Rect& rectangle, const Rect& uv, const Color& tint = {});
    [[nodiscard]] b8 prepareTextureImages(const SharedImageSource* images, usize count);
    // Invisible valid images consume no binding slot; invalid arguments leave bindings and geometry unchanged.
    [[nodiscard]] b8 drawImage(const SharedImageSource& source, const Rect& rectangle, const Rect& uv = { 0.0f, 0.0f, 1.0f, 1.0f }, const Color& tint = {});
    [[nodiscard]] DrawSnapshot freeze();
    [[nodiscard]] const DisplayMetrics& displayMetrics()const noexcept{ return m_snapshot.displayMetrics(); }
    [[nodiscard]] Rect currentClip()const noexcept;


private:
    void emitQuad(const Rect& rectangle, const Rect& uv, const Color& color, PaintMaterial::Enum material, u32 glyphPageIndex = Limit<u32>::s_Max, u32 sdfPageIndex = Limit<u32>::s_Max, u32 sdfChannel = 0u, u32 textureImageIndex = Limit<u32>::s_Max);
    void emitNineSlice(const UiSkinRegion& region, const Rect& rectangle, const Color& tint);


private:
    DrawSnapshot m_snapshot;
    PaintVector<UiSkinRegion> m_regions;
    PaintVector<Rect> m_clips;
    OverlayFrame m_overlays[s_PaintMaxOverlayDepth];
    usize m_overlayDepth = 0u;
    u32 m_layer = 0u;
    b8 m_commandsOrdered = true;
    b8 m_recording = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
