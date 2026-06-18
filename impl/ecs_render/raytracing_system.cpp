// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include "renderer_private.h"

#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/names.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Number of in-place refits performed on a skinned BLAS before forcing a full rebuild. A refit
// (in-place UPDATE) keeps the build-pose tree topology and only resizes the bounding boxes, so
// traversal quality drifts as the pose moves away from the last full build; the periodic rebuild
// restores it. This is the quality/cost knob for skinned ray-traced geometry.
inline constexpr u32 s_BlasMaxRefitsBeforeRebuild = 8u;

// Initial scene-TLAS instance capacity; grows by doubling when the live instance count exceeds it.
inline constexpr usize s_TlasInitialInstanceCapacity = 128u;

// Initial software distance-field occluder-instance capacity; grows by doubling like the TLAS.
inline constexpr usize s_SdfInitialInstanceCapacity = 64u;

// One occluder for the software distance-field shadow march: the world->object inverse affine (3 rows)
// + the mesh object-space AABB. v1 approximates each mesh as its bounding box. Must match
// NwbSdfInstance in shadow_march_cs.slang (std430).
struct SdfInstanceGpu{
    Float4 worldToObject0;
    Float4 worldToObject1;
    Float4 worldToObject2;
    Float4 objAabbMin;
    Float4 objAabbMax;
};
static_assert(sizeof(SdfInstanceGpu) == sizeof(Float4) * 5u, "SdfInstanceGpu must match the shader NwbSdfInstance layout");

struct SdfParamsGpu{
    u32 instanceCount = 0u;
    u32 reserved0 = 0u;
    u32 reserved1 = 0u;
    u32 reserved2 = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererRayTracingSystem::RendererRayTracingSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase<RendererSystem>(renderer)
{}


void RendererRayTracingSystem::logCapabilityOnce(){
    if(rayTracingState().m_capabilityLogged)
        return;

    rayTracingState().m_capabilityLogged = true;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: ray tracing capability - accel struct {}, pipeline {}, ray query {}")
        , graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        , graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)
        , graphics().queryFeatureSupport(Core::Feature::RayQuery)
    );
}

bool RendererRayTracingSystem::buildPendingMeshBlas(Core::CommandList& commandList){
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();

        // Runtime (skinned/deforming) meshes change their vertex positions every
        // frame, so their BLAS is rebuilt from the freshly skinned positions each
        // frame. Static meshes build a BLAS once and clear the pending flag.
        if(meshResources.runtimeMesh){
            if(!buildMeshBlas(commandList, meshResources))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: runtime mesh BLAS build failed"));
            continue;
        }

        if(!meshResources.blasBuildPending)
            continue;
        if(buildMeshBlas(commandList, meshResources))
            meshResources.blasBuildPending = false;
    }
    return true;
}

bool RendererRayTracingSystem::buildSceneTlas(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena> instances{ scratchArena };
    instances.reserve(rendererView.candidateCount());

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!meshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        const bool meshReady = resolvedMesh.runtime
            ? m_renderer.meshSystem().findRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh)
            : m_renderer.meshSystem().findMeshResources(resolvedMesh.mesh, mesh)
        ;
        if(!meshReady || !mesh || !mesh->blas)
            continue;

        Core::RayTracingInstanceDesc instanceDesc;
        instanceDesc.setBLAS(mesh->blas.get());
        instanceDesc.setInstanceID(static_cast<u32>(instances.size()));
        instanceDesc.setInstanceMask(0xFFu);

        if(const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity)){
            // Compose object->world (T * R(quat) * S) and store it as the instance's row-major 3x4 transform;
            // the engine's column-vector SIMDMatrix rows map directly onto AffineTransform (= Float34).
            const SIMDMatrix instanceWorld = MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            );
            StoreFloat(instanceWorld, &instanceDesc.transform);
        }

        instances.push_back(instanceDesc);
    }

    if(instances.empty()){
        rayTracingState().m_tlasDeviceAddress = 0u;
        return false;
    }

    if(!rayTracingState().m_tlas || rayTracingState().m_tlasMaxInstances < instances.size()){
        usize capacity = rayTracingState().m_tlasMaxInstances > 0u ? rayTracingState().m_tlasMaxInstances : s_TlasInitialInstanceCapacity;
        while(capacity < instances.size())
            capacity *= 2u;

        Core::RayTracingAccelStructDesc accelStructDesc(arena());
        accelStructDesc.setTopLevelMaxInstances(capacity);
        accelStructDesc.setBuildFlags(Core::RayTracingAccelStructBuildFlags::PreferFastTrace);
        accelStructDesc.setDebugName(Name("scene_tlas"));

        auto* device = graphics().getDevice();
        Core::RayTracingAccelStructHandle tlas = device->createAccelStruct(accelStructDesc);
        if(!tlas){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene TLAS (capacity {})")
                , static_cast<u64>(capacity)
            );
            return false;
        }
        rayTracingState().m_tlas = Move(tlas);
        rayTracingState().m_tlasMaxInstances = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created scene TLAS (capacity {} instances)")
            , static_cast<u64>(capacity)
        );
    }

    commandList.buildTopLevelAccelStruct(
        rayTracingState().m_tlas.get(),
        instances.data(),
        instances.size(),
        Core::RayTracingAccelStructBuildFlags::PreferFastTrace
    );
    rayTracingState().m_tlasDeviceAddress = rayTracingState().m_tlas->getDeviceAddress();
    return true;
}

bool RendererRayTracingSystem::createShadowVisibilityTarget(DeferredFrameTargets& targets){
    // The shadow-visibility image is the shared output of the shadow subsystem: hardware ray tracing
    // writes a per-light visibility mask here, and the software distance-field fallback will write the
    // same image. The deferred lighting pass always samples it, so it is allocated unconditionally and
    // cleared to "all lit" each frame (then overwritten by whichever backend runs) to keep a single
    // binding/shader path regardless of ray-tracing support.
    targets.shadowVisibilityFormat = Core::Format::R32_UINT;

    Core::TextureDesc visibilityDesc;
    visibilityDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.shadowVisibilityFormat)
        .setInUAV(true)
        .setName("engine/shadow/visibility")
    ;
    targets.shadowVisibility = graphics().createTexture(visibilityDesc);
    if(!targets.shadowVisibility){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow visibility target"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return false;
    if(!ensureShadowPipeline())
        return false;
    if(!ensureShadowBindingSet(targets))
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // The binding set carries the TLAS, the G-buffer SRVs, the scene/light buffers and the visibility
    // UAV; letting the command list derive their states keeps the accel-struct read, shader-resource
    // and unordered-access transitions in one place instead of hand-listing each resource.
    commandList.setResourceStatesForBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.commitBarriers();

    Core::RayTracingState rayTracingPassState;
    rayTracingPassState.setShaderTable(rayTracingState().m_shadowShaderTable.get());
    rayTracingPassState.addBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.setRayTracingState(rayTracingPassState);

    Core::RayTracingDispatchRaysArguments dispatchArgs;
    dispatchArgs.setDimensions(targets.width, targets.height, 1u);
    commandList.dispatchRays(dispatchArgs);
    return true;
}

void RendererRayTracingSystem::clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return;

    // 0xFFFFFFFF = every per-light bit set = fully lit. This is the default the deferred lighting pass
    // samples whenever no shadow backend wrote the image this frame (ray tracing unavailable, no
    // trace-able geometry, or a trace that could not be dispatched).
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureUInt(targets.shadowVisibility.get(), ECSRenderDetail::s_FramebufferSubresources, 0xFFFFFFFFu);
}

bool RendererRayTracingSystem::renderSdfShadowVisibility(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena, DeferredFrameTargets& targets){
    // Software backend: produce the SAME R32_UINT visibility bitmask the hardware path writes, by
    // sphere-marching a global distance field assembled from per-instance occluder boxes instead of
    // tracing the TLAS. Runs when ray tracing is unavailable (or forced off).
    if(!targets.shadowVisibility)
        return false;

    u32 instanceCount = 0u;
    if(!uploadSdfInstances(commandList, scratchArena, instanceCount))
        return false;
    if(instanceCount == 0u)
        return false;
    if(!ensureSdfShadowPipeline())
        return false;
    if(!ensureSdfShadowBindingSet(targets))
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    commandList.setResourceStatesForBindingSet(rayTracingState().m_sdfShadowBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(rayTracingState().m_sdfShadowPipeline.get());
    computeState.addBindingSet(rayTracingState().m_sdfShadowBindingSet.get());
    commandList.setComputeState(computeState);
    commandList.dispatch((targets.width + 7u) / 8u, (targets.height + 7u) / 8u, 1u);
    return true;
}

bool RendererRayTracingSystem::buildMeshBlas(Core::CommandList& commandList, MeshResources& meshResources){
    if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer)
        return false;

    const Core::BufferDesc& positionDesc = meshResources.positionBuffer->getDescription();
    if(positionDesc.structStride == 0u || meshResources.meshletPrimitiveIndexCount == 0u)
        return false;

    const u32 vertexStride = static_cast<u32>(positionDesc.structStride);
    const u32 vertexCount = static_cast<u32>(positionDesc.byteSize / positionDesc.structStride);

    Core::RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(meshResources.positionBuffer.get())
        .setVertexFormat(Core::Format::RGB32_FLOAT)
        .setVertexStride(vertexStride)
        .setVertexCount(vertexCount)
        .setIndexBuffer(meshResources.triangleIndexBuffer.get())
        .setIndexFormat(Core::Format::R32_UINT)
        .setIndexCount(meshResources.meshletPrimitiveIndexCount)
    ;

    Core::RayTracingGeometryDesc geometry;
    geometry
        .setTriangles(triangles)
        .setFlags(Core::RayTracingGeometryFlags::Opaque)
    ;

    // Runtime (skinned) meshes keep a single resident BLAS built with AllowUpdate and refit it in
    // place from the freshly skinned positions each frame, forcing a full rebuild every
    // s_BlasMaxRefitsBeforeRebuild frames to restore BVH quality. Static meshes build once.
    Core::RayTracingAccelStructBuildFlags::Mask buildFlags = Core::RayTracingAccelStructBuildFlags::PreferFastTrace;
    if(meshResources.runtimeMesh)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::AllowUpdate;

    const bool firstBuild = !meshResources.blas;
    if(firstBuild){
        Core::RayTracingAccelStructDesc accelStructDesc(arena());
        accelStructDesc.addBottomLevelGeometry(geometry);
        accelStructDesc.setBuildFlags(buildFlags);
        accelStructDesc.setDebugName(DeriveName(meshResources.meshName, AStringView(":blas")));

        auto* device = graphics().getDevice();
        Core::RayTracingAccelStructHandle blas = device->createAccelStruct(accelStructDesc);
        if(!blas){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BLAS for mesh '{}'")
                , StringConvert(meshResources.meshName.c_str())
            );
            return false;
        }
        meshResources.blas = Move(blas);
        meshResources.blasRefitsSinceRebuild = 0u;
    }

    const bool performRefit =
        meshResources.runtimeMesh
        && !firstBuild
        && meshResources.blasRefitsSinceRebuild < s_BlasMaxRefitsBeforeRebuild
    ;
    if(performRefit)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::PerformUpdate;

    commandList.setBufferState(meshResources.positionBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
    commandList.setBufferState(meshResources.triangleIndexBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
    commandList.commitBarriers();

    commandList.buildBottomLevelAccelStruct(meshResources.blas.get(), &geometry, 1u, buildFlags);

    meshResources.blasRefitsSinceRebuild = performRefit ? (meshResources.blasRefitsSinceRebuild + 1u) : 0u;

    if(firstBuild){
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built BLAS for mesh '{}' (runtime {}, {} vertices, {} indices)")
            , StringConvert(meshResources.meshName.c_str())
            , meshResources.runtimeMesh
            , static_cast<u64>(vertexCount)
            , static_cast<u64>(meshResources.meshletPrimitiveIndexCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureShadowPipeline(){
    if(rayTracingState().m_shadowPipeline)
        return true;
    if(rayTracingState().m_shadowPipelineFailed)
        return false;
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::AllRayTracing);
        layoutDesc.addItem(Core::BindingLayoutItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT, 1));

        rayTracingState().m_shadowBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
            rayTracingState().m_shadowPipelineFailed = true;
            return false;
        }
    }

    Core::ShaderHandle raygenShader;
    Core::ShaderHandle missShader;
    Core::ShaderHandle closestHitShader;
    if(
        !m_renderer.shaderSystem().loadShader(raygenShader, AssetsGraphicsShadow::s_RaygenShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::RayGeneration, "ECSRender_ShadowRaygen")
        || !m_renderer.shaderSystem().loadShader(missShader, AssetsGraphicsShadow::s_MissShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Miss, "ECSRender_ShadowMiss")
        || !m_renderer.shaderSystem().loadShader(closestHitShader, AssetsGraphicsShadow::s_ClosestHitShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::ClosestHit, "ECSRender_ShadowClosestHit")
    ){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    Core::RayTracingPipelineDesc pipelineDesc(arena());
    pipelineDesc.setMaxPayloadSize(static_cast<u32>(sizeof(f32)));
    pipelineDesc.setMaxRecursionDepth(1u);
    pipelineDesc.addBindingLayout(rayTracingState().m_shadowBindingLayout);

    Core::RayTracingPipelineShaderDesc raygenDesc(arena());
    raygenDesc.setShader(raygenShader).setExportName("ShadowRayGen");
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc(arena());
    missDesc.setShader(missShader).setExportName("ShadowMiss");
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc(arena());
    hitGroupDesc.setClosestHitShader(closestHitShader).setExportName("ShadowHitGroup");
    pipelineDesc.addHitGroup(hitGroupDesc);

    rayTracingState().m_shadowPipeline = device->createRayTracingPipeline(pipelineDesc);
    if(!rayTracingState().m_shadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT shadow pipeline"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    Core::RayTracingShaderTableHandle shaderTable = rayTracingState().m_shadowPipeline->createShaderTable();
    if(!shaderTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT shadow shader table"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }
    shaderTable->setRayGenerationShader("ShadowRayGen");
    shaderTable->addMissShader("ShadowMiss");
    shaderTable->addHitGroup("ShadowHitGroup");
    rayTracingState().m_shadowShaderTable = Move(shaderTable);

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RT shadow pipeline + shader table"));
    return true;
}

bool RendererRayTracingSystem::ensureShadowBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowBindingLayout);
    NWB_ASSERT(rayTracingState().m_tlas);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);

    // The TLAS handle is the only binding input that can change without a target resize (it is
    // recreated when the live instance count outgrows its capacity); a resize resets the binding set
    // via resetDeferredFrameTargets, so caching against the TLAS pointer covers both rebuild triggers.
    const Core::RayTracingAccelStruct* tlas = rayTracingState().m_tlas.get();
    if(
        rayTracingState().m_shadowBindingSet
        && rayTracingState().m_shadowBindingSetTlas == tlas
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, rayTracingState().m_tlas.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT,
        targets.shadowVisibility.get(),
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    auto* device = graphics().getDevice();
    rayTracingState().m_shadowBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_shadowBindingLayout);
    if(!rayTracingState().m_shadowBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding set"));
        rayTracingState().m_shadowBindingSetTlas = nullptr;
        return false;
    }
    rayTracingState().m_shadowBindingSetTlas = tlas;
    return true;
}

bool RendererRayTracingSystem::ensureSdfShadowPipeline(){
    if(rayTracingState().m_sdfShadowPipeline)
        return true;
    if(rayTracingState().m_sdfPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_sdfShadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_SDF_BINDING_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_SDF_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_SDF_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_SDF_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_SDF_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_SDF_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_SDF_BINDING_VISIBILITY_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_SDF_BINDING_PARAMS, 1));

        rayTracingState().m_sdfShadowBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_sdfShadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SDF shadow binding layout"));
            rayTracingState().m_sdfPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_sdfShadowComputeShader,
        AssetsGraphicsShadow::s_MarchComputeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_ShadowSdfMarch"
    )){
        rayTracingState().m_sdfPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_sdfShadowComputeShader)
        .addBindingLayout(rayTracingState().m_sdfShadowBindingLayout)
    ;
    rayTracingState().m_sdfShadowPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_sdfShadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SDF shadow compute pipeline"));
        rayTracingState().m_sdfPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created software distance-field shadow pipeline"));
    return true;
}

bool RendererRayTracingSystem::uploadSdfInstances(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena, u32& outInstanceCount){
    outInstanceCount = 0u;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    Vector<SdfInstanceGpu, Core::Alloc::ScratchArena> instances{ scratchArena };
    instances.reserve(rendererView.candidateCount());

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!meshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        const bool meshReady = resolvedMesh.runtime
            ? m_renderer.meshSystem().findRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh)
            : m_renderer.meshSystem().findMeshResources(resolvedMesh.mesh, mesh)
        ;
        if(!meshReady || !mesh || !mesh->csgLocalBounds.valid())
            continue;

        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        if(!transform)
            continue;

        // Object-space distance equals world-space distance only for rigid (unit-scale) instances; the
        // analytic box march tolerates the approximation, baked SDFs will track it via the same inverse.
        const SIMDMatrix instanceWorld = MatrixAffineTransformation(
            LoadFloat(transform->scale),
            VectorZero(),
            LoadFloat(transform->rotation),
            LoadFloat(transform->position)
        );
        SIMDVector determinant;
        const SIMDMatrix worldToObject = MatrixInverse(&determinant, instanceWorld);

        Float34 worldToObjectRows{};
        StoreFloat(worldToObject, &worldToObjectRows);

        SdfInstanceGpu instance;
        instance.worldToObject0 = worldToObjectRows.rows[0];
        instance.worldToObject1 = worldToObjectRows.rows[1];
        instance.worldToObject2 = worldToObjectRows.rows[2];
        instance.objAabbMin = Float4(mesh->csgLocalBounds.minBounds.x, mesh->csgLocalBounds.minBounds.y, mesh->csgLocalBounds.minBounds.z, 0.0f);
        instance.objAabbMax = Float4(mesh->csgLocalBounds.maxBounds.x, mesh->csgLocalBounds.maxBounds.y, mesh->csgLocalBounds.maxBounds.z, 0.0f);
        instances.push_back(instance);
    }

    if(instances.empty())
        return true;

    if(!rayTracingState().m_sdfInstanceBuffer || rayTracingState().m_sdfInstanceCapacity < instances.size()){
        usize capacity = rayTracingState().m_sdfInstanceCapacity > 0u ? rayTracingState().m_sdfInstanceCapacity : s_SdfInitialInstanceCapacity;
        while(capacity < instances.size())
            capacity *= 2u;

        Core::BufferDesc instanceBufferDesc;
        instanceBufferDesc
            .setByteSize(static_cast<u64>(sizeof(SdfInstanceGpu) * capacity))
            .setStructStride(sizeof(SdfInstanceGpu))
            .setDebugName(Name("shadow_sdf_instances"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_sdfInstanceBuffer = graphics().createBuffer(instanceBufferDesc);
        if(!rayTracingState().m_sdfInstanceBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SDF shadow instance buffer"));
            return false;
        }
        rayTracingState().m_sdfInstanceCapacity = capacity;
    }
    if(!rayTracingState().m_sdfParamsBuffer){
        Core::BufferDesc paramsBufferDesc;
        paramsBufferDesc
            .setByteSize(sizeof(SdfParamsGpu))
            .setIsConstantBuffer(true)
            .setDebugName(Name("shadow_sdf_params"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_sdfParamsBuffer = graphics().createBuffer(paramsBufferDesc);
        if(!rayTracingState().m_sdfParamsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SDF shadow params buffer"));
            return false;
        }
    }

    commandList.setBufferState(rayTracingState().m_sdfInstanceBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(rayTracingState().m_sdfInstanceBuffer.get(), instances.data(), sizeof(SdfInstanceGpu) * instances.size());
    commandList.setBufferState(rayTracingState().m_sdfInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    SdfParamsGpu params;
    params.instanceCount = static_cast<u32>(instances.size());
    commandList.setBufferState(rayTracingState().m_sdfParamsBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(rayTracingState().m_sdfParamsBuffer.get(), &params, sizeof(params));
    commandList.setBufferState(rayTracingState().m_sdfParamsBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();

    outInstanceCount = static_cast<u32>(instances.size());
    return true;
}

bool RendererRayTracingSystem::ensureSdfShadowBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_sdfShadowBindingLayout);
    NWB_ASSERT(rayTracingState().m_sdfInstanceBuffer);
    NWB_ASSERT(rayTracingState().m_sdfParamsBuffer);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);

    // Rebuild when the instance buffer (grown) or the frame targets (resized) change; the scene/light
    // buffers are persistent and the binding set is reset on resize via resetDeferredFrameTargets.
    const Core::Buffer* instanceBuffer = rayTracingState().m_sdfInstanceBuffer.get();
    const Core::Texture* visibility = targets.shadowVisibility.get();
    if(
        rayTracingState().m_sdfShadowBindingSet
        && rayTracingState().m_sdfShadowBindingSetInstanceBuffer == instanceBuffer
        && rayTracingState().m_sdfShadowBindingSetVisibility == visibility
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_SDF_BINDING_INSTANCES, rayTracingState().m_sdfInstanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_SDF_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_SDF_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_SDF_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_SDF_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_SDF_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_SDF_BINDING_VISIBILITY_OUTPUT,
        targets.shadowVisibility.get(),
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_SDF_BINDING_PARAMS, rayTracingState().m_sdfParamsBuffer.get()));

    auto* device = graphics().getDevice();
    rayTracingState().m_sdfShadowBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_sdfShadowBindingLayout);
    if(!rayTracingState().m_sdfShadowBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SDF shadow binding set"));
        rayTracingState().m_sdfShadowBindingSetInstanceBuffer = nullptr;
        rayTracingState().m_sdfShadowBindingSetVisibility = nullptr;
        return false;
    }
    rayTracingState().m_sdfShadowBindingSetInstanceBuffer = instanceBuffer;
    rayTracingState().m_sdfShadowBindingSetVisibility = visibility;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

