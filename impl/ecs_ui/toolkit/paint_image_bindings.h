// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "images/image_source.h"
#include "text/glyph_page.h"
#include "text/sdf_page.h"

#include <global/assert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PaintImageBindings final{
public:
    explicit PaintImageBindings(Core::Alloc::GlobalArena& arena)noexcept
        : m_glyphPages(arena)
        , m_sdfPages(arena)
        , m_textureImages(arena)
    {}
    PaintImageBindings(PaintImageBindings&&)noexcept = default;
    PaintImageBindings(const PaintImageBindings&) = delete;
    PaintImageBindings& operator=(PaintImageBindings&&)noexcept = default;
    PaintImageBindings& operator=(const PaintImageBindings&) = delete;


public:
    [[nodiscard]] b8 prepare(Span<const SharedGlyphPage> glyphPages, Span<const SharedSdfAtlasPage> sdfPages, Span<const SharedImageSource> textureImages);
    void clear()noexcept{ m_glyphPages.clear(); m_sdfPages.clear(); m_textureImages.clear(); }
    [[nodiscard]] const Vector<SharedGlyphPage, Core::Alloc::GlobalArena>& glyphPages()const noexcept{ return m_glyphPages; }
    [[nodiscard]] const Vector<SharedSdfAtlasPage, Core::Alloc::GlobalArena>& sdfPages()const noexcept{ return m_sdfPages; }
    [[nodiscard]] const Vector<SharedImageSource, Core::Alloc::GlobalArena>& textureImages()const noexcept{ return m_textureImages; }

    template<typename TResource, typename TPrepare, typename TSameKey>
    [[nodiscard]] static b8 Resolve(
        const Vector<TResource, Core::Alloc::GlobalArena>& resources,
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


private:
    Vector<SharedGlyphPage, Core::Alloc::GlobalArena> m_glyphPages;
    Vector<SharedSdfAtlasPage, Core::Alloc::GlobalArena> m_sdfPages;
    Vector<SharedImageSource, Core::Alloc::GlobalArena> m_textureImages;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

