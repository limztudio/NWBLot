// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_types.h"

#include <core/ecs/entity_id.h>

#include <global/generic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshState final : NoCopy{
    friend class RendererSystem;

public:
    explicit RendererMeshState(Core::Alloc::GlobalArena& arena);


public:
    void invalidateResources();


private:
    HashMap<Name, MeshResources, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_meshes;
};

class RendererMaterialState final : NoCopy{
    friend class RendererSystem;

public:
    explicit RendererMaterialState(Core::Alloc::GlobalArena& arena);


public:
    void invalidateResources();


private:
    HashMap<Name, MaterialSurfaceInfo, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_surfaceInfos;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo, Core::Alloc::GlobalArena> m_pipelines;
    HashMap<Core::ECS::EntityID, MaterialInstanceMutableCacheEntry, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>, Core::Alloc::GlobalArena> m_instanceMutableCache;
    HashMap<Name, RenderPath::Enum, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_loggedMaterialPaths;
    u64 m_instanceMutableCacheComponentMutationVersion = 0u;
};

class RendererDrawState final : NoCopy{
    friend class RendererSystem;

public:
    RendererDrawState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_meshBindingLayout;
    Core::BindingLayoutHandle m_computeBindingLayout;
    Core::BindingLayoutHandle m_emulationViewBindingLayout;
    Core::BufferHandle m_instanceBuffer;
    Core::BufferHandle m_materialTypedBuffer;
    Core::BufferHandle m_meshViewBuffer;
    Core::BindingSetHandle m_emulationViewBindingSet;
    Core::ShaderHandle m_emulationVertexShader;
    Core::InputLayoutHandle m_emulationInputLayout;
    usize m_instanceBufferCapacity = 0;
    usize m_materialTypedBufferCapacity = 0;
};

class RendererCsgState final : NoCopy{
    friend class RendererSystem;

public:
    RendererCsgState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_clipBindingLayout;
    Core::BindingSetHandle m_clipBindingSet;
    Core::BufferHandle m_receiverRangeBuffer;
    Core::BufferHandle m_cutterBuffer;
    Core::BufferHandle m_parameterByteBuffer;
    Core::BufferHandle m_planeCapVertexBuffer;
    Core::ShaderHandle m_planeCapVertexShader;
    Core::ShaderHandle m_planeCapPixelShader;
    Core::ShaderHandle m_planeCapAvboitOccupancyPixelShader;
    Core::ShaderHandle m_planeCapAvboitExtinctionPixelShader;
    Core::ShaderHandle m_planeCapAvboitAccumulatePixelShader;
    Core::InputLayoutHandle m_planeCapInputLayout;
    Core::GraphicsPipelineHandle m_planeCapPipeline;
    Core::GraphicsPipelineHandle m_planeCapAvboitOccupancyPipeline;
    Core::GraphicsPipelineHandle m_planeCapAvboitExtinctionPipeline;
    Core::GraphicsPipelineHandle m_planeCapAvboitAccumulatePipeline;
    usize m_receiverRangeBufferCapacity = 0u;
    usize m_cutterBufferCapacity = 0u;
    usize m_parameterByteBufferCapacity = 0u;
    usize m_planeCapVertexBufferCapacity = 0u;
};

class RendererDeferredState final : NoCopy{
    friend class RendererSystem;

public:
    RendererDeferredState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_lightingBindingLayout;
    Core::BufferHandle m_sceneShadingBuffer;
    Core::ShaderHandle m_compositeVertexShader;
    Core::ShaderHandle m_lightingPixelShader;
    Core::GraphicsPipelineHandle m_lightingPipeline;
    Core::BindingLayoutHandle m_compositeBindingLayout;
    Core::SamplerHandle m_sampler;
    Core::ShaderHandle m_compositePixelShader;
    Core::GraphicsPipelineHandle m_compositePipeline;
    DeferredFrameTargets m_targets;
};

class RendererAvboitState final : NoCopy{
    friend class RendererSystem;

public:
    RendererAvboitState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_emptyBindingLayout;
    Core::BindingLayoutHandle m_occupancyBindingLayout;
    Core::BindingLayoutHandle m_depthWarpBindingLayout;
    Core::BindingLayoutHandle m_extinctionBindingLayout;
    Core::BindingLayoutHandle m_integrateBindingLayout;
    Core::BindingLayoutHandle m_accumulateBindingLayout;
    Core::SamplerHandle m_linearSampler;
    Core::ShaderHandle m_occupancyPixelShader;
    Core::ShaderHandle m_depthWarpComputeShader;
    Core::ShaderHandle m_extinctionPixelShader;
    Core::ShaderHandle m_integrateComputeShader;
    Core::ShaderHandle m_accumulatePixelShader;
    Core::ComputePipelineHandle m_depthWarpPipeline;
    Core::ComputePipelineHandle m_integratePipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

