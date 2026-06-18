// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_types.h"

#include <core/ecs/entity_id.h>

#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>

#include <global/generic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem;
class RendererShaderSystem;
class RendererMeshSystem;
class RendererMaterialSystem;
class RendererCsgSystem;
class RendererDeferredSystem;
class RendererAvboitSystem;
class RendererRayTracingSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgFrameStateCacheSignature{
    u64 contentHash = 0u;

    friend bool operator==(const CsgFrameStateCacheSignature& lhs, const CsgFrameStateCacheSignature& rhs){
        return lhs.contentHash == rhs.contentHash;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;
    friend class RendererRayTracingSystem;

public:
    explicit RendererMeshState(Core::Alloc::GlobalArena& arena);


public:
    void invalidateResources();


private:
    HashMap<Name, MeshResources, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_meshes;
};

class RendererMaterialState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;

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
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;

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
    u8 m_meshViewGpuData[sizeof(f32) * NWB_MESH_VIEW_FLOAT_COUNT] = {};
    usize m_instanceBufferCapacity = 0;
    usize m_materialTypedBufferCapacity = 0;
    bool m_meshViewGpuDataValid = false;
};

class RendererCsgState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;

public:
    explicit RendererCsgState(Core::Alloc::GlobalArena& arena);


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_clipBindingLayout;
    Core::BindingSetHandle m_clipBindingSet;
    Core::BindingLayoutHandle m_intervalPeelBindingLayout;
    Core::BindingSetHandle m_intervalPeelBindingSet;
    Core::BindingLayoutHandle m_receiverSpanBuildBindingLayout;
    Core::BindingSetHandle m_receiverSpanBuildBindingSet;
    Core::BindingLayoutHandle m_intervalCombineBindingLayout;
    Core::BindingSetHandle m_intervalCombineBindingSet;
    Core::BindingLayoutHandle m_receiverSurfaceBindingLayout;
    Core::BindingSetHandle m_receiverSurfaceBindingSet;
    Core::BindingLayoutHandle m_intervalSampleBindingLayout;
    Core::BindingSetHandle m_intervalSampleBindingSet;
    Core::ShaderHandle m_intervalPeelComputeShader;
    Core::ShaderHandle m_receiverSpanBuildComputeShader;
    Core::ShaderHandle m_intervalCombineComputeShader;
    Core::ShaderHandle m_intervalCapFillPixelShader;
    Core::ComputePipelineHandle m_intervalPeelPipeline;
    Core::ComputePipelineHandle m_receiverSpanBuildPipeline;
    Core::ComputePipelineHandle m_intervalCombinePipeline;
    Core::GraphicsPipelineHandle m_intervalCapFillPipeline;
    Core::BufferHandle m_receiverRangeBuffer;
    Core::BufferHandle m_cutterBuffer;
    Core::BufferHandle m_intervalSampleStateBuffer;
    CsgFrameStateCacheSignature m_frameStateCacheSignature;
    CsgFrameState m_frameStateCache;
    usize m_receiverRangeBufferCapacity = 0u;
    usize m_cutterBufferCapacity = 0u;
    bool m_frameStateCacheValid = false;
};

class RendererDeferredState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;
    friend class RendererRayTracingSystem;

public:
    RendererDeferredState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_lightingBindingLayout;
    Core::BufferHandle m_sceneShadingBuffer;
    Core::BufferHandle m_lightBuffer;
    Core::ShaderHandle m_compositeVertexShader;
    Core::ShaderHandle m_lightingPixelShader;
    Core::GraphicsPipelineHandle m_lightingPipeline;
    Core::BindingLayoutHandle m_compositeBindingLayout;
    Core::SamplerHandle m_sampler;
    Core::ShaderHandle m_compositePixelShader;
    Core::GraphicsPipelineHandle m_compositePipeline;
    u8 m_sceneShadingGpuData[sizeof(f32) * NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT] = {};
    bool m_sceneShadingGpuDataValid = false;
    DeferredFrameTargets m_targets;
};

class RendererAvboitState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;

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
    bool m_targetsNeedClear = true;
};

class RendererRayTracingState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;
    friend class RendererRayTracingSystem;

public:
    RendererRayTracingState() = default;


public:
    void invalidateResources();


private:
    Core::RayTracingAccelStructHandle m_tlas;
    usize m_tlasMaxInstances = 0u;
    u64 m_tlasDeviceAddress = 0u;
    Core::BindingLayoutHandle m_shadowBindingLayout;
    Core::RayTracingPipelineHandle m_shadowPipeline;
    Core::RayTracingShaderTableHandle m_shadowShaderTable;
    Core::BindingSetHandle m_shadowBindingSet;
    const Core::RayTracingAccelStruct* m_shadowBindingSetTlas = nullptr;
    Core::BufferHandle m_sdfInstanceBuffer;
    Core::BufferHandle m_sdfParamsBuffer;
    Core::BindingLayoutHandle m_sdfShadowBindingLayout;
    Core::ShaderHandle m_sdfShadowComputeShader;
    Core::ComputePipelineHandle m_sdfShadowPipeline;
    Core::BindingSetHandle m_sdfShadowBindingSet;
    const Core::Buffer* m_sdfShadowBindingSetInstanceBuffer = nullptr;
    const Core::Texture* m_sdfShadowBindingSetVisibility = nullptr;
    usize m_sdfInstanceCapacity = 0u;
    bool m_capabilityLogged = false;
    bool m_shadowPipelineFailed = false;
    bool m_sdfPipelineFailed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

