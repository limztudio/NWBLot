// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "images/image_source.h"
#include "text/glyph_page.h"
#include "text/sdf_page.h"

#include <impl/assets_ui_skin/asset.h>
#include <impl/ecs_ui/toolkit/layout/paint_emission.adhd.h>
#include <impl/ecs_ui/toolkit/paint_types.adhd.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using PaintVector = Vector<T, Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The caller's arena must outlive the snapshot and every consumer of it.
class DrawSnapshot final{
    friend class PaintBuilder;


public:
    explicit DrawSnapshot(Core::Alloc::GlobalArena& arena)noexcept
        : m_vertices(arena)
        , m_indices(arena)
        , m_commands(arena)
        , m_glyphPages(arena)
        , m_sdfPages(arena)
        , m_textureImages(arena)
    {}
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
public:
    explicit PaintBuilder(Core::Alloc::GlobalArena& arena)noexcept
        : m_snapshot(arena)
        , m_regions(arena)
        , m_clips(arena)
    {}


public:
    // Skin payload validation belongs to loading; begin captures its descriptor for this frame.
    void begin(const DisplayMetrics& metrics, const u64 generation, const u64 skinGeneration, const Core::Assets::AssetRef<UiSkin>& skinRef, const UiSkin& skin){
        NWB_ASSERT(skinRef.valid() && skinRef.name() == skin.virtualPath());
        NWB_ASSERT(skin.atlasWidth() > 0u && skin.atlasHeight() > 0u && skin.referenceDensity() > 0.0f);
        NWB_ASSERT(IsFinite(skin.referenceDensity()));
        NWB_ASSERT(IsFinite(metrics.logicalWidth) && metrics.logicalWidth >= 0.0f);
        NWB_ASSERT(IsFinite(metrics.logicalHeight) && metrics.logicalHeight >= 0.0f);
        NWB_ASSERT(IsFinite(metrics.pixelScaleX) && metrics.pixelScaleX > 0.0f);
        NWB_ASSERT(IsFinite(metrics.pixelScaleY) && metrics.pixelScaleY > 0.0f);
        m_snapshot.m_generation = generation;
        m_snapshot.m_displayMetrics = metrics;
        m_snapshot.m_skinBinding = { skinRef, skin.texture(), skinGeneration, skin.atlasWidth(), skin.atlasHeight(), skin.referenceDensity() };
        m_snapshot.m_vertices.clear();
        m_snapshot.m_indices.clear();
        m_snapshot.m_commands.clear();
        m_snapshot.m_glyphPages.clear();
        m_snapshot.m_sdfPages.clear();
        m_snapshot.m_textureImages.clear();
        m_regions.assign(skin.regions().begin(), skin.regions().end());
        m_clips.clear();
        m_clips.push_back({ 0.0f, 0.0f, metrics.logicalWidth, metrics.logicalHeight });
        m_overlayDepth = 0u;
        m_layer = 0u;
        m_commandsOrdered = true;
        m_recording = true;
    }
    void reserve(const usize quadCount, const usize clipDepth = s_PaintDefaultClipDepth){
        ReservePaintSnapshot(quadCount, clipDepth, m_snapshot.m_vertices, m_snapshot.m_indices, m_snapshot.m_commands, m_clips);
    }
    void pushClip(const Rect& clip){
        PushPaintClip(m_recording, clip, m_clips);
    }
    [[nodiscard]] b8 popClip()noexcept{
        return PopPaintClip(m_recording, m_overlayDepth, m_overlays, m_clips);
    }
    // Each bounded overlay escapes parent clipping and restores that clip and layer at its balanced end.
    [[nodiscard]] b8 beginOverlay(const u32 layer){
        return BeginPaintOverlay(m_recording, layer, m_snapshot.m_displayMetrics, m_overlays, m_overlayDepth, m_layer, m_clips);
    }
    [[nodiscard]] b8 endOverlay()noexcept{
        return EndPaintOverlay(m_recording, m_overlays, m_overlayDepth, m_layer, m_clips);
    }
    void fillRect(const Rect& rectangle, const Color& color = {}){ emitQuad(rectangle, {}, color, PaintMaterial::Solid); }
    [[nodiscard]] b8 drawRegion(const Name& regionName, const Rect& rectangle, const Color& tint = {}){
        NWB_ASSERT(m_recording);
        return DrawPaintSkinRegion(
            m_recording,
            m_clips.back(),
            m_regions,
            regionName,
            rectangle,
            tint,
            m_snapshot.m_skinBinding.atlasWidth,
            m_snapshot.m_skinBinding.atlasHeight,
            m_snapshot.m_skinBinding.referenceDensity,
            m_layer,
            m_snapshot.m_vertices,
            m_snapshot.m_indices,
            m_snapshot.m_commands,
            m_commandsOrdered
        );
    }
    // Admit all pages before emitting a label. Failure preserves existing bindings and geometry.
    [[nodiscard]] b8 prepareGlyphPages(const Span<const SharedGlyphPage> pages){ return prepareImages(pages, {}); }
    [[nodiscard]] b8 drawGlyph(const SharedGlyphPage& page, const Rect& rectangle, const Rect& uv, const Color& tint = {}){
        NWB_ASSERT(m_recording);
        if(!page || !IsValidPaintUvRect(uv))
            return false;
        u32 index = 0u;
        if(!resolvePreparedResourceSlot(
            m_snapshot.m_glyphPages,
            page,
            [this](const Span<const SharedGlyphPage> pages){ return prepareGlyphPages(pages); },
            [](const GlyphPage& lhs, const GlyphPage& rhs)noexcept{ return SameGlyphPageKey(lhs.binding(), rhs.binding()); },
            index
        ))
            return false;
        emitQuad(rectangle, uv, tint, PaintMaterial::Glyph, index);
        return true;
    }
    [[nodiscard]] b8 prepareSdfPages(const Span<const SharedSdfAtlasPage> pages){ return prepareImages({}, pages); }
    [[nodiscard]] b8 prepareImages(const Span<const SharedGlyphPage> glyphPages, const Span<const SharedSdfAtlasPage> sdfPages, const Span<const SharedImageSource> textureImages = {}){
        NWB_ASSERT(m_recording);
        const usize glyphCount = glyphPages.size();
        const usize sdfCount = sdfPages.size();
        const usize textureCount = textureImages.size();
        if(glyphCount > s_PaintMaxImages || sdfCount > s_PaintMaxImages || textureCount > s_PaintMaxImages)
            return false;
        usize addedGlyphs = 0u;
        for(usize candidate = 0u; candidate < glyphCount; ++candidate){
            if(!glyphPages[candidate])
                return false;
            const GlyphPageBinding& binding = glyphPages[candidate]->binding();
            b8 found = false;
            for(const SharedGlyphPage& existing : m_snapshot.m_glyphPages){
                if(SameGlyphPageKey(binding, existing->binding())){
                    if(binding.width != existing->binding().width || binding.height != existing->binding().height)
                        return false;
                    found = true;
                    break;
                }
            }
            for(usize previous = 0u; previous < candidate; ++previous){
                if(SameGlyphPageKey(binding, glyphPages[previous]->binding())){
                    const GlyphPageBinding& previousBinding = glyphPages[previous]->binding();
                    if(binding.width != previousBinding.width || binding.height != previousBinding.height)
                        return false;
                    found = true;
                    break;
                }
            }
            if(!found)
                ++addedGlyphs;
        }
        usize addedSdf = 0u;
        for(usize candidate = 0u; candidate < sdfCount; ++candidate){
            if(!sdfPages[candidate])
                return false;
            const SdfAtlasPageBinding& binding = sdfPages[candidate]->binding();
            b8 found = false;
            for(const SharedSdfAtlasPage& existing : m_snapshot.m_sdfPages){
                if(SameSdfAtlasPageKey(binding, existing->binding())){
                    if(!(binding == existing->binding()))
                        return false;
                    found = true;
                    break;
                }
            }
            for(usize previous = 0u; previous < candidate; ++previous){
                if(SameSdfAtlasPageKey(binding, sdfPages[previous]->binding())){
                    if(!(binding == sdfPages[previous]->binding()))
                        return false;
                    found = true;
                    break;
                }
            }
            if(!found)
                ++addedSdf;
        }
        usize addedTextures = 0u;
        for(usize candidate = 0u; candidate < textureCount; ++candidate){
            if(!textureImages[candidate])
                return false;
            b8 found = false;
            for(const SharedImageSource& existing : m_snapshot.m_textureImages){
                if(SameTextureImageKey(*textureImages[candidate], *existing)){
                    found = true;
                    break;
                }
            }
            for(usize previous = 0u; previous < candidate; ++previous){
                if(SameTextureImageKey(*textureImages[candidate], *textureImages[previous])){
                    found = true;
                    break;
                }
            }
            if(!found)
                ++addedTextures;
        }
        const usize existingCount = m_snapshot.m_glyphPages.size() + m_snapshot.m_sdfPages.size() + m_snapshot.m_textureImages.size();
        if(existingCount > s_PaintMaxImages || addedGlyphs + addedSdf + addedTextures > s_PaintMaxImages - existingCount)
            return false;
        // All image kinds are admitted before any binding is published, including append-only coverage upgrades.
        m_snapshot.m_glyphPages.reserve(m_snapshot.m_glyphPages.size() + addedGlyphs);
        m_snapshot.m_sdfPages.reserve(m_snapshot.m_sdfPages.size() + addedSdf);
        m_snapshot.m_textureImages.reserve(m_snapshot.m_textureImages.size() + addedTextures);
        for(usize candidate = 0u; candidate < glyphCount; ++candidate){
            SharedGlyphPage* existing = nullptr;
            for(SharedGlyphPage& bound : m_snapshot.m_glyphPages){
                if(SameGlyphPageKey(bound->binding(), glyphPages[candidate]->binding())){
                    existing = &bound;
                    break;
                }
            }
            if(!existing)
                m_snapshot.m_glyphPages.push_back(glyphPages[candidate]);
            else if((*existing)->binding().generation < glyphPages[candidate]->binding().generation)
                *existing = glyphPages[candidate];
        }
        for(usize candidate = 0u; candidate < sdfCount; ++candidate){
            b8 found = false;
            for(const SharedSdfAtlasPage& bound : m_snapshot.m_sdfPages){
                if(SameSdfAtlasPageKey(bound->binding(), sdfPages[candidate]->binding())){
                    found = true;
                    break;
                }
            }
            if(!found)
                m_snapshot.m_sdfPages.push_back(sdfPages[candidate]);
        }
        for(usize candidate = 0u; candidate < textureCount; ++candidate){
            b8 found = false;
            for(const SharedImageSource& bound : m_snapshot.m_textureImages){
                if(SameTextureImageKey(*bound, *textureImages[candidate])){
                    found = true;
                    break;
                }
            }
            if(!found)
                m_snapshot.m_textureImages.push_back(textureImages[candidate]);
        }
        return true;
    }
    [[nodiscard]] b8 drawSdfGlyph(const SharedSdfAtlasPage& page, const u32 channel, const Rect& rectangle, const Rect& uv, const Color& tint = {}){
        NWB_ASSERT(m_recording);
        if(!page || channel >= page->binding().channelCount || !IsValidPaintUvRect(uv))
            return false;
        u32 index = 0u;
        if(!resolvePreparedResourceSlot(
            m_snapshot.m_sdfPages,
            page,
            [this](const Span<const SharedSdfAtlasPage> pages){ return prepareSdfPages(pages); },
            [](const SdfAtlasPage& lhs, const SdfAtlasPage& rhs)noexcept{ return lhs.binding() == rhs.binding(); },
            index
        ))
            return false;
        emitQuad(rectangle, uv, tint, PaintMaterial::SdfGlyph, Limit<u32>::s_Max, index, channel);
        return true;
    }
    [[nodiscard]] b8 prepareTextureImages(const Span<const SharedImageSource> images){ return prepareImages({}, {}, images); }
    // Invisible valid images consume no binding slot; invalid arguments leave bindings and geometry unchanged.
    [[nodiscard]] b8 drawImage(const SharedImageSource& source, const Rect& rectangle, const Rect& uv = { 0.0f, 0.0f, 1.0f, 1.0f }, const Color& tint = {}){
        NWB_ASSERT(m_recording);
        if(!source || !IsValidPaintImageDrawRequest(rectangle, uv, tint))
            return false;
        const Rect clip = m_clips.back();
        if(IsPaintImageInvisible(rectangle, clip, tint))
            return true;
        u32 index = 0u;
        if(!resolvePreparedResourceSlot(
            m_snapshot.m_textureImages,
            source,
            [this](const Span<const SharedImageSource> images){ return prepareTextureImages(images); },
            [](const ImageSource& lhs, const ImageSource& rhs)noexcept{ return SameTextureImageKey(lhs, rhs); },
            index
        ))
            return false;
        emitQuad(rectangle, uv, tint, PaintMaterial::Image, Limit<u32>::s_Max, Limit<u32>::s_Max, 0u, index);
        return true;
    }
    [[nodiscard]] DrawSnapshot freeze(){
        NWB_ASSERT(m_recording && m_clips.size() == 1u && m_overlayDepth == 0u);
        if(!m_commandsOrdered){
            Sort(m_snapshot.m_commands.begin(), m_snapshot.m_commands.end(), [](const DrawCommand& lhs, const DrawCommand& rhs)noexcept{
                return IsPaintDrawCommandBefore(lhs, rhs);
            });
        }
        m_recording = false;
        m_clips.clear();
        return Move(m_snapshot);
    }
    [[nodiscard]] const DisplayMetrics& displayMetrics()const noexcept{ return m_snapshot.displayMetrics(); }
    [[nodiscard]] Rect currentClip()const noexcept{
        NWB_ASSERT(m_recording);
        return m_clips.back();
    }


private:
    template<typename TResource, typename TPrepare, typename TSameKey>
    [[nodiscard]] static b8 resolvePreparedResourceSlot(
        const PaintVector<TResource>& resources,
        const TResource& candidate,
        TPrepare&& prepare,
        TSameKey&& sameKey,
        u32& outIndex
    ){
        for(u32 index = 0u; index < resources.size(); ++index){
            if(resources[index].get() == candidate.get()){
                outIndex = index;
                return true;
            }
        }
        if(!prepare(Span<const TResource>(&candidate, 1u)))
            return false;
        for(u32 index = 0u; index < resources.size(); ++index){
            if(sameKey(*resources[index], *candidate)){
                outIndex = index;
                return true;
            }
        }
        NWB_ASSERT(false);
        return false;
    }
    void emitQuad(const Rect& rectangle, const Rect& uv, const Color& color, const PaintMaterial::Enum material, const u32 glyphPageIndex = Limit<u32>::s_Max, const u32 sdfPageIndex = Limit<u32>::s_Max, const u32 sdfChannel = 0u, const u32 textureImageIndex = Limit<u32>::s_Max){
        NWB_ASSERT(m_recording);
        EmitPaintQuad(
            m_recording,
            m_clips.back(),
            rectangle,
            uv,
            color,
            material,
            m_layer,
            glyphPageIndex,
            sdfPageIndex,
            sdfChannel,
            textureImageIndex,
            m_snapshot.m_vertices,
            m_snapshot.m_indices,
            m_snapshot.m_commands,
            m_commandsOrdered
        );
    }
    void emitNineSlice(const UiSkinRegion& region, const Rect& rectangle, const Color& tint){
        NWB_ASSERT(m_recording);
        EmitPaintNineSlice(
            m_recording,
            m_clips.back(),
            region,
            rectangle,
            tint,
            m_snapshot.m_skinBinding.atlasWidth,
            m_snapshot.m_skinBinding.atlasHeight,
            m_snapshot.m_skinBinding.referenceDensity,
            m_layer,
            m_snapshot.m_vertices,
            m_snapshot.m_indices,
            m_snapshot.m_commands,
            m_commandsOrdered
        );
    }


private:
    DrawSnapshot m_snapshot;
    PaintVector<UiSkinRegion> m_regions;
    PaintVector<Rect> m_clips;
    Array<PaintOverlayFrame, s_PaintMaxOverlayDepth> m_overlays{};
    usize m_overlayDepth = 0u;
    u32 m_layer = 0u;
    b8 m_commandsOrdered = true;
    b8 m_recording = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
