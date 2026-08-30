// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <impl/ecs_render/material/renderer_draw_types.h>

#include <core/ecs/entity_id.h>
#include <core/graphics/rhi/pipeline.h>

#include <impl/assets_sampler/loader.h>
#include <impl/assets_texture/loader.h>

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Device-lifetime material asset caches retain the descriptor owner for every patched global heap slot.
struct RendererMaterialResourceState{
    HashMap<Name, UniquePtr<TextureGpuResource>, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> textureAssetCache;
    HashMap<Name, UniquePtr<SamplerGpuResource>, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> samplerAssetCache;

    explicit RendererMaterialResourceState(Core::Alloc::GlobalArena& arena)
        : textureAssetCache(0, Hasher<Name>(), EqualTo<Name>(), arena)
        , samplerAssetCache(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialState final : NoCopy{
    friend class RendererMaterialSystem;

public:
    explicit RendererMaterialState(Core::Alloc::GlobalArena& arena);


private:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_materialPassBindingLayout;
    HashMap<Name, MaterialSurfaceInfo, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_surfaceInfos;
    RendererMaterialResourceState m_resourceState;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo, Core::Alloc::GlobalArena> m_pipelines;
    HashMap<Core::ECS::EntityID, MaterialInstanceMutableCacheEntry, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>, Core::Alloc::GlobalArena> m_instanceMutableCache;
    HashMap<Name, RenderPath::Enum, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_loggedMaterialPaths;
    u64 m_instanceMutableCacheComponentMutationVersion = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

