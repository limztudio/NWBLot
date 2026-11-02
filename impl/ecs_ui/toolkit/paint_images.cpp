// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"
#include "rect_math.h"

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_paint_images{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TResource, typename TPrepare, typename TSameKey>
[[nodiscard]] static b8 ResolvePreparedResourceSlot(
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PaintBuilder::prepareImages(const Span<const SharedGlyphPage> glyphPages, const Span<const SharedSdfAtlasPage> sdfPages, const Span<const SharedImageSource> textureImages){
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

b8 PaintBuilder::drawGlyph(const SharedGlyphPage& page, const Rect& rectangle, const Rect& uv, const Color& tint){
    using namespace __hidden_ui_paint_images;
    NWB_ASSERT(m_recording);
    if(!page || !IsValidPaintUvRect(uv))
        return false;
    u32 index = 0u;
    if(!ResolvePreparedResourceSlot(
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

b8 PaintBuilder::drawSdfGlyph(const SharedSdfAtlasPage& page, const u32 channel, const Rect& rectangle, const Rect& uv, const Color& tint){
    using namespace __hidden_ui_paint_images;
    NWB_ASSERT(m_recording);
    if(!page || channel >= page->binding().channelCount || !IsValidPaintUvRect(uv))
        return false;
    u32 index = 0u;
    if(!ResolvePreparedResourceSlot(
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

b8 PaintBuilder::drawImage(const SharedImageSource& source, const Rect& rectangle, const Rect& uv, const Color& tint){
    using namespace __hidden_ui_paint_images;
    NWB_ASSERT(m_recording);
    if(
        !source || !IsValidUiRect(rectangle) || !IsValidPaintUvRect(uv)
        || !IsFinite(tint.r) || !IsFinite(tint.g) || !IsFinite(tint.b) || !IsFinite(tint.a)
        || tint.a < 0.0f || tint.a > 1.0f
    )
        return false;
    const Rect clip = m_clips.back();
    if(!HasUiRectArea(rectangle) || !HasUiRectArea(clip) || tint.a == 0.0f || !HasVisibleRectIntersection(LoadRect(rectangle), LoadRect(clip)))
        return true;
    u32 index = 0u;
    if(!ResolvePreparedResourceSlot(
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

