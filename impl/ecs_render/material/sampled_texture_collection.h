// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_material_state.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One collector owns mutation of its borrowed output during its lifetime.
template<typename OutputArena>
class MaterialSampledTextureCollector final : NoCopy{
    friend class ShadowMaterialSampledTextureCollector;


private:
    using TextureIndex = HashSet<Core::Texture*, Core::Alloc::ScratchArena>;


private:
    static constexpr usize s_InlineTextureCount = 32u;


public:
    MaterialSampledTextureCollector(Vector<Core::TextureHandle, OutputArena>& textures, Core::Alloc::ScratchArena& scratchArena)
        : m_textures(textures)
        , m_scratchArena(scratchArena)
    {
        for(const Core::TextureHandle& texture : m_textures){
            Core::Texture* const identity = texture.get();
            if(!m_index){
                if(containsInline(identity))
                    continue;
                if(m_inlineCount < s_InlineTextureCount){
                    m_inlineTextures[m_inlineCount] = identity;
                    ++m_inlineCount;
                    continue;
                }
                promoteIndex();
            }
            m_index->insert(identity);
        }
    }
    MaterialSampledTextureCollector(MaterialSampledTextureCollector&&) = delete;


public:
    void append(const Core::TextureHandle& texture){
        Core::Texture* const identity = texture.get();
        if(!m_index){
            if(containsInline(identity))
                return;
            if(m_inlineCount < s_InlineTextureCount){
                m_textures.push_back(texture);
                m_inlineTextures[m_inlineCount] = identity;
                ++m_inlineCount;
                return;
            }
        }
        appendIndexed(texture);
    }


private:
    [[nodiscard]] bool containsInline(Core::Texture* identity)const{
        for(usize index = 0u; index < m_inlineCount; ++index){
            if(m_inlineTextures[index] == identity)
                return true;
        }
        return false;
    }

    void appendIndexed(const Core::TextureHandle& texture){
        if(!m_index)
            promoteIndex();
        const auto [found, inserted] = m_index->insert(texture.get());
        if(!inserted)
            return;
        ScopeExit discardIdentity([&]()noexcept{ m_index->erase(found); });

        m_textures.push_back(texture);
        discardIdentity.release();
    }

    void promoteIndex(){
        NWB_ASSERT(!m_index && m_inlineCount == s_InlineTextureCount);
        TextureIndex index(s_InlineTextureCount * 4u, m_scratchArena);
        for(Core::Texture* const identity : m_inlineTextures)
            index.insert(identity);
        static_assert(IsNothrowMoveConstructible_V<TextureIndex>);
        m_index.emplace(Move(index));
    }

    void clear()noexcept{
        if(m_index)
            m_index->clear();
        m_inlineCount = 0u;
        m_textures.clear();
    }


private:
    Vector<Core::TextureHandle, OutputArena>& m_textures;
    Core::Alloc::ScratchArena& m_scratchArena;
    Core::Texture* m_inlineTextures[s_InlineTextureCount];
    usize m_inlineCount = 0u;
    Optional<TextureIndex> m_index;
};


using MaterialSurfaceInfoMap = HashMap<Name, MaterialSurfaceInfo, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena>;


// Rejection preserves the successfully collected prefix.
[[nodiscard]] bool AppendPreparedMaterialSurfaceSampledTextures(
    const MaterialSurfaceInfo& materialInfo,
    const RendererMaterialResourceState& resources,
    MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& collector
);
[[nodiscard]] bool GatherPreparedMaterialPassSampledTextures(
    const MaterialSurfaceInfoMap& materials,
    const RendererMaterialResourceState& resources,
    const MaterialPassDrawItems* const* drawItemSets,
    usize drawItemSetCount,
    Vector<Core::TextureHandle, Core::Alloc::ScratchArena>& outTextures,
    Core::Alloc::ScratchArena& scratchArena
);
// Names check sequentially; a missing name preserves the published prefix.
[[nodiscard]] bool MergePreparedShadowMaterialSampledTextures(
    const Vector<Core::TextureHandle, Core::Alloc::ScratchArena>& sampledTextures,
    MaterialSampledTextureCollector<Core::Alloc::GlobalArena>& collector
);


// Materials validate into reused storage before textures enter the output.
class ShadowMaterialSampledTextureCollector final : NoCopy{
private:
    struct PendingTextures{
        Vector<Core::TextureHandle, Core::Alloc::ScratchArena> textures;
        MaterialSampledTextureCollector<Core::Alloc::ScratchArena> collector;

        explicit PendingTextures(Core::Alloc::ScratchArena& scratchArena)
            : textures(scratchArena)
            , collector(textures, scratchArena)
        {}
    };


public:
    ShadowMaterialSampledTextureCollector(
        Vector<Core::TextureHandle, Core::Alloc::GlobalArena>& textures,
        Core::Alloc::ScratchArena& scratchArena
    );
    ShadowMaterialSampledTextureCollector(ShadowMaterialSampledTextureCollector&&) = delete;


public:
    template<typename ResolveSurface>
    [[nodiscard]] bool collect(const MaterialSurfaceInfo& materialInfo, ResolveSurface&& resolveSurface){
        if(!m_pending)
            m_pending.emplace(m_scratchArena);
        ScopeExit clearPending([&]()noexcept{ m_pending->collector.clear(); });

        if(!resolveSurface(materialInfo, m_pending->collector))
            return false;
        return MergePreparedShadowMaterialSampledTextures(m_pending->textures, m_output);
    }


private:
    MaterialSampledTextureCollector<Core::Alloc::GlobalArena> m_output;
    Core::Alloc::ScratchArena& m_scratchArena;
    Optional<PendingTextures> m_pending;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

