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


// Material asset caches retain descriptor owners for patched heap slots.
struct RendererMaterialResourceState{
    HashMap<Name, UniquePtr<TextureGpuResource>, Core::Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>> textureAssetCache;
    HashMap<Name, UniquePtr<SamplerGpuResource>, Core::Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>> samplerAssetCache;

    explicit RendererMaterialResourceState(Core::Alloc::GlobalArena& arena)
        : textureAssetCache(0, Hasher<Name>(), EqualTo<Name>(), arena)
        , samplerAssetCache(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Device-lifetime backing resources for the first material-authored-resource slice. The fixture payloads are shared by all materials,
// each MaterialSurfaceInfo receives the matching global-heap slot word in its typed constants.
struct RendererMaterialResourceFixtureState{
    Core::TextureHandle checkerRgba8Texture;
    Core::SamplerHandle linearClampSampler;
    Core::GpuDescriptorHandle checkerRgba8HeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle linearClampHeapHandle = Core::GpuDescriptorHandle::invalid();
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
    Core::BindingLayoutHandle m_computeBindingLayout;
    Core::BufferHandle m_instanceBuffer;
    Core::BufferHandle m_materialTypedBuffer;
    Core::ShaderHandle m_emulationVertexShader;
    Core::InputLayoutHandle m_emulationInputLayout;
    Core::ShaderHandle m_objectGeometryDecodeShader;
    Core::ComputePipelineHandle m_objectGeometryDecodePipeline;
    Core::InputLayoutHandle m_objectGeometryInputLayout;
    HashMap<Name, MaterialSurfaceInfo, Core::Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>> m_surfaceInfos;
    RendererMaterialResourceState m_resourceState;
    RendererMaterialResourceFixtureState m_resourceFixtures;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, Core::Alloc::GlobalArena, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo> m_pipelines;
    HashMap<Core::ECS::EntityID, MaterialInstanceMutableCacheEntry, Core::Alloc::GlobalArena, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>> m_instanceMutableCache;
    HashMap<Name, RenderPath::Enum, Core::Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>> m_loggedMaterialPaths;
    usize m_instanceBufferCapacity = 0u;
    usize m_materialTypedBufferCapacity = 0u;
    u64 m_instanceMutableCacheComponentMutationVersion = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

