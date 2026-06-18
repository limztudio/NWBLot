// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include "renderer_private.h"

#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/names.h>
#include <impl/assets/graphics/bvh/binding_slots.h>
#include <impl/assets/graphics/bvh/names.h>
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

// Initial element capacity of the BVH Morton-sort scratch buffers (keys + payload); grows by doubling.
inline constexpr usize s_BvhSortInitialCapacity = 1024u;

// CPU mirror of the shader NwbBvhBitonicSortPushConstants block (one bitonic compare-exchange step).
struct BvhSortPushConstants{
    u32 elementCount = 0u;
    u32 compareDistance = 0u;
    u32 sequenceSize = 0u;
    u32 pad0 = 0u;
};
static_assert(sizeof(BvhSortPushConstants) == sizeof(u32) * 4u, "BvhSortPushConstants must match the shader NwbBvhBitonicSortPushConstants layout");


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

#if defined(NWB_DEBUG)
    runBvhSortSelfTest();
#endif
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

bool RendererRayTracingSystem::ensureBvhSortPipeline(){
    if(rayTracingState().m_bvhSortPipeline)
        return true;
    if(rayTracingState().m_bvhSortPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhSortBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_KEYS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_PAYLOAD, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhSortPushConstants)));

        rayTracingState().m_bvhSortBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_bvhSortBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding layout"));
            rayTracingState().m_bvhSortPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_bvhSortShader,
        AssetsGraphicsBvh::s_BitonicSortShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_BvhBitonicSort"
    )){
        rayTracingState().m_bvhSortPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_bvhSortShader)
        .addBindingLayout(rayTracingState().m_bvhSortBindingLayout)
    ;
    rayTracingState().m_bvhSortPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_bvhSortPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort compute pipeline"));
        rayTracingState().m_bvhSortPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhSortBuffers(usize paddedCount){
    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhSortKeysBuffer || rayTracingState().m_bvhSortCapacity < paddedCount){
        usize capacity = rayTracingState().m_bvhSortCapacity > 0u ? rayTracingState().m_bvhSortCapacity : s_BvhSortInitialCapacity;
        while(capacity < paddedCount)
            capacity *= 2u;

        Core::BufferDesc keysBufferDesc;
        keysBufferDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("bvh_sort_keys"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_bvhSortKeysBuffer = graphics().createBuffer(keysBufferDesc);
        if(!rayTracingState().m_bvhSortKeysBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort keys buffer"));
            return false;
        }

        Core::BufferDesc payloadBufferDesc;
        payloadBufferDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("bvh_sort_payload"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_bvhSortPayloadBuffer = graphics().createBuffer(payloadBufferDesc);
        if(!rayTracingState().m_bvhSortPayloadBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort payload buffer"));
            return false;
        }

        rayTracingState().m_bvhSortCapacity = capacity;
        rayTracingState().m_bvhSortBindingSet.reset();
    }

    const Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    if(
        rayTracingState().m_bvhSortBindingSet
        && rayTracingState().m_bvhSortBindingSetKeys == keysBuffer
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_KEYS, rayTracingState().m_bvhSortKeysBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_PAYLOAD, rayTracingState().m_bvhSortPayloadBuffer.get()));

    rayTracingState().m_bvhSortBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_bvhSortBindingLayout);
    if(!rayTracingState().m_bvhSortBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding set"));
        rayTracingState().m_bvhSortBindingSetKeys = nullptr;
        return false;
    }
    rayTracingState().m_bvhSortBindingSetKeys = keysBuffer;
    return true;
}

bool RendererRayTracingSystem::bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount){
    NWB_ASSERT(rayTracingState().m_bvhSortPipeline);
    NWB_ASSERT(rayTracingState().m_bvhSortBindingSet);
    NWB_ASSERT(rayTracingState().m_bvhSortKeysBuffer);
    NWB_ASSERT(rayTracingState().m_bvhSortPayloadBuffer);

    // paddedCount must be a power of two and a multiple of the dispatch group size; the caller fills the
    // sort buffers to it and pads the tail with sentinel keys that sort to the end.
    if(paddedCount < static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE))
        return false;

    Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = rayTracingState().m_bvhSortPayloadBuffer.get();

    // Each (sequenceSize, compareDistance) step reads and writes the same buffers, so consecutive steps
    // must be serialized with UAV barriers: enable per-buffer UAV barriers, then commit one per step.
    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);

    const u32 groupCount = paddedCount / static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);
    for(u32 sequenceSize = 2u; sequenceSize <= paddedCount; sequenceSize <<= 1u){
        for(u32 compareDistance = sequenceSize >> 1u; compareDistance > 0u; compareDistance >>= 1u){
            Core::ComputeState computeState;
            computeState.setPipeline(rayTracingState().m_bvhSortPipeline.get());
            computeState.addBindingSet(rayTracingState().m_bvhSortBindingSet.get());
            commandList.setComputeState(computeState);

            BvhSortPushConstants pushConstants;
            pushConstants.elementCount = elementCount;
            pushConstants.compareDistance = compareDistance;
            pushConstants.sequenceSize = sequenceSize;
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
            commandList.dispatch(groupCount, 1u, 1u);

            commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::runBvhSortSelfTest(){
    if(rayTracingState().m_bvhSortSelfTestDone)
        return;
    rayTracingState().m_bvhSortSelfTestDone = true;

    // A wrong sort silently corrupts every BVH built on top of it, so verify the kernel directly in debug
    // builds: sort a reversed sequence with an identity payload and read it back. The ascending result must
    // be exactly 0..elementCount-1, with the sentinel-padded tail still non-decreasing.
    constexpr u32 elementCount = 1000u;
    constexpr u32 paddedCount = 1024u;
    static_assert(paddedCount >= static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE), "self-test padded count must cover one dispatch group");

    if(!ensureBvhSortPipeline())
        return;
    if(!ensureBvhSortBuffers(paddedCount))
        return;

    auto* device = graphics().getDevice();

    u32 inputKeys[paddedCount];
    u32 inputPayload[paddedCount];
    for(u32 i = 0u; i < paddedCount; ++i){
        inputKeys[i] = i < elementCount ? (elementCount - 1u - i) : 0xFFFFFFFFu;
        inputPayload[i] = i;
    }

    Core::BufferDesc readbackBufferDesc;
    readbackBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * paddedCount))
        .setCpuAccess(Core::CpuAccessMode::Read)
        .setDebugName(Name("bvh_sort_selftest_readback"))
        .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
    ;
    Core::BufferHandle readbackBuffer = graphics().createBuffer(readbackBufferDesc);
    if(!readbackBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort self-test readback buffer"));
        return;
    }

    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort self-test command list"));
        return;
    }

    commandList->open();
    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->setBufferState(rayTracingState().m_bvhSortPayloadBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->commitBarriers();
    commandList->writeBuffer(rayTracingState().m_bvhSortKeysBuffer.get(), inputKeys, sizeof(inputKeys));
    commandList->writeBuffer(rayTracingState().m_bvhSortPayloadBuffer.get(), inputPayload, sizeof(inputPayload));
    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList->setBufferState(rayTracingState().m_bvhSortPayloadBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();

    if(!bvhBitonicSort(*commandList, elementCount, paddedCount)){
        commandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH sort self-test dispatch setup failed"));
        return;
    }

    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::CopySource);
    commandList->commitBarriers();
    commandList->copyBuffer(readbackBuffer.get(), 0u, rayTracingState().m_bvhSortKeysBuffer.get(), 0u, static_cast<u64>(sizeof(u32) * paddedCount));
    commandList->close();

    Core::CommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH sort self-test wait-for-idle failed"));
        return;
    }

    const u32* sortedKeys = static_cast<const u32*>(device->mapBuffer(readbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!sortedKeys){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH sort self-test readback buffer"));
        return;
    }

    bool sorted = true;
    for(u32 i = 0u; i < elementCount; ++i){
        if(sortedKeys[i] != i){
            sorted = false;
            break;
        }
    }
    for(u32 i = 0u; sorted && (i + 1u) < paddedCount; ++i){
        if(sortedKeys[i] > sortedKeys[i + 1u]){
            sorted = false;
            break;
        }
    }
    device->unmapBuffer(readbackBuffer.get());

    if(sorted)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH bitonic sort self-test PASSED ({} elements)"), elementCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH bitonic sort self-test FAILED"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

