// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>

#include <impl/ecs_render/raytrace/raytracing_build_policy.adhd.h>

#include <impl/ecs_render/raytrace/raytracing_diagnostics.adhd.h>
#include <impl/ecs_render/raytrace/raytracing_scene_identity_builder.adhd.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>
#include <impl/ecs_render/raytrace/software_bvh_builder.h>
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include <impl/ecs_render/raytrace/mesh_heap_handle_gather.h>
#include <impl/ecs_render/shadow/light_space_scene_gather.h>
#include <impl/ecs_render/shadow/shadow_material_context_preparer.h>
#include <impl/ecs_render/shadow/shadow_occluder_material_gather.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::prepareSceneSwBvhResources(Core::Alloc::ScratchArena& scratchArena){
    using namespace RayTracingSoftwareBvhDetail;

    m_preparedSceneContentStamp = {};
    m_lightSpaceShadow.m_captureSceneTrusted = false;

    // Software scene BVH and material context share hardware instance ordering.
    auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystemPtr)
        return false;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();
    LightSpaceShadowSceneGather lightSpaceGather(m_lightSpaceShadow, m_csgShapeRegistry, m_world, scratchArena);
    lightSpaceGather.begin(candidateCount, false);

    // Parallel instance records and CPU BVH build values.
    Vector<SceneSwBvhInstanceGpu, Core::Alloc::ScratchArena> instances{ scratchArena };
    Vector<SoftwareSceneRefitInstanceGpu, Core::Alloc::ScratchArena> sceneRefitInputs{ scratchArena };
    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> sceneRefitRoots{ scratchArena };
    Vector<SceneBvhPrimitiveCalculation, Core::Alloc::ScratchArena> instanceBvhPrimitives{ scratchArena };
    // Parallel material records index scene-BVH leaves.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // All-occluder trace context; draw buffers hold one transparency class.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    Vector<PreparedSceneSwBvhMesh, Core::Alloc::ScratchArena> preparedMeshes{ scratchArena };
    ECSRenderDetail::MaterialTypedConstantRangeMap shadowConstantTypedRanges(0, ECSRenderDetail::MaterialTypedConstantRangeMap::hasher(), ECSRenderDetail::MaterialTypedConstantRangeMap::key_equal(), scratchArena);
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(0, ECSRenderDetail::MaterialTypedByteContentRangeMap::hasher(), ECSRenderDetail::MaterialTypedByteContentRangeMap::key_equal(), scratchArena);
    instances.reserve(candidateCount);
    sceneRefitInputs.reserve(candidateCount);
    sceneRefitRoots.reserve(candidateCount);
    lightSpaceGather.reserveSoftwareCasters(candidateCount);
    instanceBvhPrimitives.reserve(candidateCount);
    instanceMaterials.reserve(candidateCount);
    shadowInstanceData.reserve(candidateCount);
    preparedMeshes.reserve(candidateCount);
    shadowConstantTypedRanges.reserve(candidateCount);
    shadowMutableTypedRanges.reserve(candidateCount);
    MeshBufferSlotLookup meshSlotLookup(0, Hasher<NotNull<Core::Buffer*>>(), EqualTo<NotNull<Core::Buffer*>>(), scratchArena);
    meshSlotLookup.reserve(candidateCount);

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software scene BVH requires the initialized global descriptor heap"));
        return false;
    }
    MeshHeapHandleGather meshHeapHandles(heap, m_rayTracingState.m_swMeshHeapHandleCache);
    meshHeapHandles.begin();
    m_rayTracingState.m_swShadowMeshNodeBuffers.clear();
    m_rayTracingState.m_swShadowMeshPositionBuffers.clear();
    m_rayTracingState.m_swShadowMeshIndexBuffers.clear();
    m_rayTracingState.m_swShadowMeshAttributeBuffers.clear();
    m_rayTracingState.m_swShadowMeshNodeHandles.clear();
    m_rayTracingState.m_swShadowMeshPositionHandles.clear();
    m_rayTracingState.m_swShadowMeshIndexHandles.clear();
    m_rayTracingState.m_swShadowMeshAttributeHandles.clear();
    m_rayTracingState.m_swShadowMeshCount = 0u;
    b8 staticScene = true;
    b8 contentComplete = true;
    RayTracingOpticalSceneGather opticalScene(scratchArena, candidateCount);

    ShadowMaterialSampledTextureCollector sampledTextureCollector(m_preparedShadowTraceMaterialSampledTextures, scratchArena);
    ShadowOccluderMaterialGather occluderMaterials(m_materialSystem, sampledTextureCollector, shadowMaterialTypedBytes, shadowConstantTypedRanges, shadowMutableTypedRanges, opticalScene, contentComplete);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        const auto meshResult = RayTracingDetail::ResolveRenderableMeshResources(*meshSystemPtr, m_meshSystem, entity);
        if(!meshResult && meshResult.error() == RenderableMeshResolution::Absent)
            continue;
        const b8 meshReady = meshResult.has_value();
        // Preflight selects pending mesh storage; the prepared build and traversal validate topology before use.
        const b8 topologyReady = meshReady && (meshResult->meshResources.swBvhTopologyBuilt || meshResult->meshResources.runtimeMesh || meshResult->meshResources.swBvhBuildPending);
        if(
            !meshReady
            || !topologyReady
            || !meshResult->meshResources.swBvhNodeBuffer
            || !RayTracingDetail::IsHeapHandle(meshResult->meshResources.swBvhNodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
            || !meshResult->meshResources.positionBuffer
            || !meshResult->meshResources.triangleIndexBuffer
            || !meshResult->meshResources.attributeBuffer
            || !meshResult->meshResources.csgLocalBounds.valid()
        ){
            contentComplete = false;
            opticalScene.markIncomplete();
            continue;
        }
        // Runtime mesh updates disable static scene-BVH reuse.
        if(meshResult->resolvedMesh.runtime || meshResult->meshResources.runtimeMesh)
            staticScene = false;

        // Reuse one table slot for instances sharing geometry.
        const NotNull<Core::Buffer*> meshNodeBuffer(meshResult->meshResources.swBvhNodeBuffer.get());
        u32 meshSlot = 0u;
        const auto foundMeshSlot = meshSlotLookup.find(meshNodeBuffer);
        if(foundMeshSlot != meshSlotLookup.end())
            meshSlot = foundMeshSlot.value();
        else{
            const Core::GpuDescriptorHandle nodeHandle = meshResult->meshResources.swBvhNodeHeapHandle;
            const Core::GpuDescriptorHandle positionHandle = meshResult->meshResources.swBvhPositionHeapHandle;
            const Core::GpuDescriptorHandle indexHandle = meshResult->meshResources.swBvhTriangleIndexHeapHandle;
            if(!positionHandle.valid() || positionHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer || !indexHandle.valid() || indexHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer){
                meshHeapHandles.sweep();
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW scene mesh buffers in the global descriptor heap"));
                return false;
            }

            const auto attributeHandle = meshHeapHandles.acquire(meshResult->meshResources.attributeBuffer);
            if(!attributeHandle){
                meshHeapHandles.sweep();
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW scene mesh buffers in the global descriptor heap"));
                return false;
            }

            const Core::BufferDesc& nodeDesc = meshResult->meshResources.swBvhNodeBuffer->getCreationDescription();
            const Core::BufferDesc& positionDesc = meshResult->meshResources.positionBuffer->getCreationDescription();
            const Core::BufferDesc& indexDesc = meshResult->meshResources.triangleIndexBuffer->getCreationDescription();
            const Core::BufferDesc& attributeDesc = meshResult->meshResources.attributeBuffer->getCreationDescription();
            preparedMeshes.push_back(PreparedSceneSwBvhMesh{
                .meshName = meshResult->meshResources.meshName,
                .nodeBuffer = meshResult->meshResources.swBvhNodeBuffer,
                .positionBuffer = meshResult->meshResources.positionBuffer,
                .triangleIndexBuffer = meshResult->meshResources.triangleIndexBuffer,
                .attributeBuffer = meshResult->meshResources.attributeBuffer,
                .nodeHeapHandle = nodeHandle,
                .positionHeapHandle = positionHandle,
                .triangleIndexHeapHandle = indexHandle,
                .attributeHeapHandle = *attributeHandle,
                .runtimeMeshVersion = meshResult->meshResources.runtimeMeshVersion,
                .geometryContentRevision = meshResult->meshResources.runtimeGeometryContentRevision,
                .nodeByteSize = nodeDesc.byteSize,
                .positionByteSize = positionDesc.byteSize,
                .triangleIndexByteSize = indexDesc.byteSize,
                .attributeByteSize = attributeDesc.byteSize,
                .primitiveCount = meshResult->meshResources.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount,
                .runtimeMesh = meshResult->meshResources.runtimeMesh,
            });

            meshSlot = m_rayTracingState.m_swShadowMeshCount;
            m_rayTracingState.m_swShadowMeshNodeBuffers.push_back(meshNodeBuffer);
            m_rayTracingState.m_swShadowMeshPositionBuffers.push_back(MakeNotNull(meshResult->meshResources.positionBuffer.get()));
            m_rayTracingState.m_swShadowMeshIndexBuffers.push_back(MakeNotNull(meshResult->meshResources.triangleIndexBuffer.get()));
            m_rayTracingState.m_swShadowMeshAttributeBuffers.push_back(MakeNotNull(meshResult->meshResources.attributeBuffer.get()));
            m_rayTracingState.m_swShadowMeshNodeHandles.push_back(nodeHandle);
            m_rayTracingState.m_swShadowMeshPositionHandles.push_back(positionHandle);
            m_rayTracingState.m_swShadowMeshIndexHandles.push_back(indexHandle);
            m_rayTracingState.m_swShadowMeshAttributeHandles.push_back(*attributeHandle);
            if(!meshSlotLookup.emplace(meshNodeBuffer, meshSlot).second){
                NWB_ASSERT(false);
                meshHeapHandles.sweep();
                return false;
            }
            ++m_rayTracingState.m_swShadowMeshCount;
        }

        const NWB::Impl::Scene::TransformComponent* objectTransformPtr = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = objectTransformPtr ? MatrixAffineTransformation(LoadFloat(objectTransformPtr->scale), VectorZero(), LoadFloat(objectTransformPtr->rotation), LoadFloat(objectTransformPtr->position)) : MatrixIdentity();
        SIMDVectorSingle determinant;
        const SIMDMatrix worldToObject = MatrixInverse(&determinant, objectToWorld);

        const SIMDVectorSingle localMin = LoadFloatInt(meshResult->meshResources.csgLocalBounds.minBounds);
        const SIMDVectorSingle localMax = LoadFloatInt(meshResult->meshResources.csgLocalBounds.maxBounds);
        const auto worldBounds = AabbTests::Transform(objectToWorld, localMin, localMax);
        if(!worldBounds){
            contentComplete = false;
            opticalScene.markIncomplete();
            continue;
        }
        SIMDVectorSingle worldMin = worldBounds->minBounds;
        SIMDVectorSingle worldMax = worldBounds->maxBounds;
        InflateSwShadowSceneBounds(worldMin, worldMax);

        SceneSwBvhInstanceGpu instance;
        StoreFloat(worldToObject, instance.worldToObject);
        instance.primitiveCount = meshResult->meshResources.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;

        // Build material context in scene-BVH leaf order; unresolved materials remain opaque.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        if(!occluderMaterials.resolve(
            entity,
            renderer.material,
            objectTransformPtr,
            meshInstanceIndex,
            instanceMaterial,
            shadowInstance
        ))
            return false;
        instanceMaterial.indexSlot = m_rayTracingState.m_swShadowMeshIndexHandles[meshSlot].slot();
        instanceMaterial.attributeSlot = m_rayTracingState.m_swShadowMeshAttributeHandles[meshSlot].slot();
        instanceMaterial.positionSlot = m_rayTracingState.m_swShadowMeshPositionHandles[meshSlot].slot();
        instanceMaterial.nodeSlot = m_rayTracingState.m_swShadowMeshNodeHandles[meshSlot].slot();
        const SceneBvhPrimitiveCalculation bvhPrimitive = MakeSceneBvhPrimitiveCalculation(worldMin, worldMax, instanceMaterial.flags);

        SIMDVectorSingle opticalMin = VectorZero();
        SIMDVectorSingle opticalMax = VectorZero();
        b8 opticalBoundsValid = false;
        if(bvhPrimitive.transparentOccluder && !meshResult->resolvedMesh.runtime && !meshResult->meshResources.runtimeMesh){
            const auto bounds = ComputeOpticalWorldBounds(objectToWorld, localMin, localMax);
            if(bounds){
                opticalMin = bounds->minimum;
                opticalMax = bounds->maximum;
                opticalBoundsValid = true;
            }
        }
        const b8 runtimeOpticalBounds =
            bvhPrimitive.transparentOccluder && (meshResult->resolvedMesh.runtime || meshResult->meshResources.runtimeMesh)
            && meshResult->meshResources.runtimeLocalBoundsBuffer && meshResult->meshResources.runtimeLocalBoundsHeapHandle.valid()
        ;
        Float34 instanceWorld{};
        StoreFloat(objectToWorld, instanceWorld);
        if(runtimeOpticalBounds)
            opticalScene.appendRuntime(entity, renderer, meshResult->meshResources.runtimeLocalBoundsBuffer, meshResult->meshResources.runtimeLocalBoundsHeapHandle, instanceWorld);
        else
            opticalScene.append(entity, renderer, bvhPrimitive.transparentOccluder, opticalMin, opticalMax, opticalBoundsValid);

        lightSpaceGather.appendSoftwareCaptureIdentity(entity, meshResult->resolvedMesh.runtime || meshResult->meshResources.runtimeMesh, meshResult->meshResources, instanceMaterial, shadowInstance, opticalScene.instances.back());
        lightSpaceGather.appendSoftwareReceiver(entity, bvhPrimitive.transparentOccluder, objectToWorld, worldToObject, localMin, localMax, meshResult->meshResources);
        sceneRefitInputs.push_back({ instanceWorld, instanceMaterial.nodeSlot, {} });
        sceneRefitRoots.push_back(meshResult->meshResources.swBvhNodeBuffer);
        lightSpaceGather.appendSoftwareCaster(meshResult->meshResources);
        instances.push_back(instance);
        instanceBvhPrimitives.push_back(bvhPrimitive);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    RayTracingDiagnostics().shadowMeshHeapHighWater(
        false,
        m_rayTracingState.m_swShadowMeshCount,
        m_rayTracingState.m_swShadowMeshHeapHighWater
    );
    meshHeapHandles.sweep();

    if(!lightSpaceGather.finishSoftware(instances, instanceMaterials, contentComplete))
        return false;
    const auto& csg = m_lightSpaceShadow.m_csg.snapshot;
    const u32 instanceCount = static_cast<u32>(instances.size());
    if(instanceCount == 0u){
        m_rayTracingState.m_sceneBvhInstanceCount = 0u;
        m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
        m_rayTracingState.m_swShadowMaterialContextHashValid = false;
        return true;
    }

    // Topology hash includes bounds, transforms, and transparent-subtree class.
    const u64 sceneStaticHash = staticScene ? RayTracingSceneIdentityBuilder().staticSceneHash(instances, instanceBvhPrimitives) : 0u;
    const usize requiredNodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    if(!RayTracingBuildPolicy().validateSceneSwBvhNodeCount(requiredNodeCount))
        return false;
    const b8 canReuseSceneBvh =
        staticScene
        && m_rayTracingState.m_sceneSwBvhStaticSceneHashValid
        && m_rayTracingState.m_sceneSwBvhStaticSceneHash == sceneStaticHash
        && m_rayTracingState.m_sceneBvhInstanceCount == instanceCount
        && HasPreparedSceneBvhBuffers(m_rayTracingState, instanceCount)
    ;
    if(!staticScene || !canReuseSceneBvh)
        m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
    if(canReuseSceneBvh && !capturePreparedSceneBvhCacheReuse(sceneStaticHash, instanceCount))
        return false;

    if(!canReuseSceneBvh){
        RayTraceMaterialResources materials(m_graphics, m_rayTracingState, m_rayTracingState);
        SoftwareBvhBuilder builder(m_arena, m_graphics, m_shaderSystem, m_rayTracingState);
        if(!builder.ensureSceneBvhBuffers(instanceCount, materials) || !HasPreparedSceneBvhBuffers(m_rayTracingState, instanceCount))
            return false;
        // CPU-build the small scene BVH and upload the shared node layout.
        Vector<u32, Core::Alloc::ScratchArena> indices{ scratchArena };
        Vector<u32, Core::Alloc::ScratchArena> instanceLeafCost{ scratchArena };
        Vector<SceneBvhNodeCalculation, Core::Alloc::ScratchArena> buildNodes{ scratchArena };
        Vector<NwbBvhNodeGpu, Core::Alloc::ScratchArena> nodes{ scratchArena };
        if(!BuildAndPackSceneBvhNodesInto(instances, instanceBvhPrimitives, indices, instanceLeafCost, buildNodes, nodes))
            return false;

        if(!capturePreparedSceneBvh(staticScene, sceneStaticHash, nodes, instances))
            return false;
    }

    if(!staticScene){
        auto preparedRefit = m_sceneSwBvhRefit.prepare(sceneRefitInputs, sceneRefitRoots, m_preparedSceneBvh.nodeBuffer(), m_preparedSceneBvh.nodeHeapHandle(), static_cast<u32>(requiredNodeCount), m_shaderSystem);
        if(!preparedRefit){
            m_preparedSceneSwBvhRefit.reset();
            return false;
        }
        m_preparedSceneSwBvhRefit = Move(*preparedRefit);
    }

    ShadowMaterialContextPreparer materialContextPreparer(*this);
    const auto materialContextHash = materialContextPreparer.prepare(PreparedShadowMaterialContextRoute::Software, staticScene, instances.size(), instanceMaterials, shadowInstanceData, shadowMaterialTypedBytes);
    if(!materialContextHash)
        return false;
    const u64 swMaterialContextHash = *materialContextHash;

    m_rayTracingState.m_sceneBvhInstanceCount = instanceCount;
    const b8 frozenGraphScene = m_preparedSceneBvh.ready() && m_preparedShadowMaterialContext.ready() && m_preparedShadowMaterialContext.route() == PreparedShadowMaterialContextRoute::Software;
    if(frozenGraphScene){
        if(!m_preparedSceneSwBvhTraversal.capture(preparedMeshes, instanceCount)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze software scene traversal"));
            clearPreparedSceneBvh();
            clearPreparedShadowMaterialContext();
            return false;
        }
    }
    else
        m_preparedSceneSwBvhTraversal.clear();
    if(!m_softwareOpticalScene.prepare(opticalScene) || !m_softwareOpticalScene.prepareRuntimeBounds(opticalScene, m_shaderSystem))
        return false;
    m_preparedSceneContentStamp = RayTracingSceneIdentityBuilder().buildSoftwareContentStamp(
        sceneStaticHash,
        swMaterialContextHash,
        opticalScene.contentHash(),
        csg.hasCsg,
        csg.identity,
        staticScene,
        contentComplete
    );
    // The material collector admits immutable uploaded assets/fixtures; runtime image bindings must also invalidate sampling trust.
    m_rayTracingState.m_softwareTransparentSampling.m_history.prepareScene(m_preparedSceneContentStamp);
    lightSpaceGather.publishSoftwareCaptureIdentity(instanceCount, instanceMaterials, shadowInstanceData, shadowMaterialTypedBytes, m_preparedShadowTraceMaterialSampledTextures, contentComplete);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

