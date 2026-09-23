// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/material/sampled_texture_collection.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/mesh_acceleration_update.h>
#include <impl/ecs_csg/components.h>
#include <global/algorithm.h>
#include <global/hash_utils.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareSceneSwBvhResources(Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_rt_swbvh;

    m_preparedSceneContentStamp = {};

    // Software scene BVH and material context share hardware instance ordering.
    auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystemPtr)
        return false;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

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
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        scratchArena
    );
    instances.reserve(candidateCount);
    sceneRefitInputs.reserve(candidateCount);
    sceneRefitRoots.reserve(candidateCount);
    instanceBvhPrimitives.reserve(candidateCount);
    instanceMaterials.reserve(candidateCount);
    shadowInstanceData.reserve(candidateCount);
    preparedMeshes.reserve(candidateCount);
    shadowMutableTypedRanges.reserve(candidateCount);
    MeshBufferSlotLookup meshSlotLookup(
        0,
        Hasher<const Core::Buffer*>(),
        EqualTo<const Core::Buffer*>(),
        scratchArena
    );
    meshSlotLookup.reserve(candidateCount);

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software scene BVH requires the initialized global descriptor heap"));
        return false;
    }
    BeginMeshHeapHandleGather(m_rayTracingState.m_swMeshHeapHandleCache);
    m_rayTracingState.m_swShadowMeshNodeBuffers.clear();
    m_rayTracingState.m_swShadowMeshPositionBuffers.clear();
    m_rayTracingState.m_swShadowMeshIndexBuffers.clear();
    m_rayTracingState.m_swShadowMeshAttributeBuffers.clear();
    m_rayTracingState.m_swShadowMeshNodeHandles.clear();
    m_rayTracingState.m_swShadowMeshPositionHandles.clear();
    m_rayTracingState.m_swShadowMeshIndexHandles.clear();
    m_rayTracingState.m_swShadowMeshAttributeHandles.clear();
    m_rayTracingState.m_swShadowMeshCount = 0u;
    bool staticScene = true;
    bool samplingSceneTrusted = true;
    bool contentComplete = true;
    RayTracingOpticalSceneGather opticalScene(scratchArena, candidateCount);

    ShadowMaterialSampledTextureCollector sampledTextureCollector(m_preparedShadowTraceMaterialSampledTextures, scratchArena);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;
        if(m_world.tryGetComponent<StaticCsgMeshComponent>(entity) || m_world.tryGetComponent<SkinnedCsgMeshComponent>(entity)
            || m_world.tryGetComponent<CsgReceiverComponent>(entity))
            samplingSceneTrusted = false;

        ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
        RenderableMeshDesc resolvedMesh;
        const RenderableMeshResolution::Enum meshResolution = RayTracingDetail::ResolveRenderableMeshResources(
            *meshSystemPtr,
            m_meshSystem,
            entity,
            resolvedMesh,
            mesh
        );
        if(meshResolution == RenderableMeshResolution::Absent)
            continue;
        const bool meshReady = meshResolution == RenderableMeshResolution::Ready;
        // Preflight selects pending mesh storage; the prepared build and traversal validate topology before use.
        const bool topologyReady = meshReady && (mesh.swBvhTopologyBuilt || mesh.runtimeMesh || mesh.swBvhBuildPending);
        if(
            !meshReady
            || !topologyReady
            || !mesh.swBvhNodeBuffer
            || !__hidden_rt_swbvh::IsStorageBufferHeapHandle(mesh.swBvhNodeHeapHandle)
            || !mesh.positionBuffer
            || !mesh.triangleIndexBuffer
            || !mesh.attributeBuffer
            || !mesh.csgLocalBounds.valid()
        ){
            contentComplete = false;
            opticalScene.markIncomplete();
            continue;
        }
        // Runtime mesh updates disable static scene-BVH reuse.
        if(resolvedMesh.runtime || mesh.runtimeMesh)
            staticScene = false;

        // Reuse one table slot for instances sharing geometry.
        Core::Buffer* meshNodeBuffer = mesh.swBvhNodeBuffer.get();
        u32 meshSlot = 0u;
        const auto foundMeshSlot = meshSlotLookup.find(meshNodeBuffer);
        if(foundMeshSlot != meshSlotLookup.end())
            meshSlot = foundMeshSlot.value();
        else{
            const Core::GpuDescriptorHandle nodeHandle = mesh.swBvhNodeHeapHandle;
            Core::GpuDescriptorHandle attributeHandle;
            const Core::GpuDescriptorHandle positionHandle = mesh.swBvhPositionHeapHandle;
            const Core::GpuDescriptorHandle indexHandle = mesh.swBvhTriangleIndexHeapHandle;
            if(
                !positionHandle.valid()
                || positionHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
                || !indexHandle.valid()
                || indexHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
                || !AcquireMeshHeapHandle(heap, m_rayTracingState.m_swMeshHeapHandleCache, mesh.attributeBuffer, attributeHandle)
            ){
                SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_swMeshHeapHandleCache);
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW scene mesh buffers in the global descriptor heap"));
                return false;
            }

            const Core::BufferDesc& nodeDesc = mesh.swBvhNodeBuffer->getCreationDescription();
            const Core::BufferDesc& positionDesc = mesh.positionBuffer->getCreationDescription();
            const Core::BufferDesc& indexDesc = mesh.triangleIndexBuffer->getCreationDescription();
            const Core::BufferDesc& attributeDesc = mesh.attributeBuffer->getCreationDescription();
            preparedMeshes.push_back(PreparedSceneSwBvhMesh{
                .meshName = mesh.meshName,
                .nodeBuffer = mesh.swBvhNodeBuffer,
                .positionBuffer = mesh.positionBuffer,
                .triangleIndexBuffer = mesh.triangleIndexBuffer,
                .attributeBuffer = mesh.attributeBuffer,
                .nodeHeapHandle = nodeHandle,
                .positionHeapHandle = positionHandle,
                .triangleIndexHeapHandle = indexHandle,
                .attributeHeapHandle = attributeHandle,
                .runtimeMeshVersion = mesh.runtimeMeshVersion,
                .geometryContentRevision = mesh.runtimeGeometryContentRevision,
                .nodeByteSize = nodeDesc.byteSize,
                .positionByteSize = positionDesc.byteSize,
                .triangleIndexByteSize = indexDesc.byteSize,
                .attributeByteSize = attributeDesc.byteSize,
                .primitiveCount = mesh.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount,
                .runtimeMesh = mesh.runtimeMesh,
            });

            meshSlot = m_rayTracingState.m_swShadowMeshCount;
            m_rayTracingState.m_swShadowMeshNodeBuffers.push_back(meshNodeBuffer);
            m_rayTracingState.m_swShadowMeshPositionBuffers.push_back(mesh.positionBuffer.get());
            m_rayTracingState.m_swShadowMeshIndexBuffers.push_back(mesh.triangleIndexBuffer.get());
            m_rayTracingState.m_swShadowMeshAttributeBuffers.push_back(mesh.attributeBuffer.get());
            m_rayTracingState.m_swShadowMeshNodeHandles.push_back(nodeHandle);
            m_rayTracingState.m_swShadowMeshPositionHandles.push_back(positionHandle);
            m_rayTracingState.m_swShadowMeshIndexHandles.push_back(indexHandle);
            m_rayTracingState.m_swShadowMeshAttributeHandles.push_back(attributeHandle);
            meshSlotLookup.emplace(meshNodeBuffer, meshSlot);
            ++m_rayTracingState.m_swShadowMeshCount;
        }

        const NWB::Impl::Scene::TransformComponent* objectTransformPtr = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = objectTransformPtr
            ? MatrixAffineTransformation(
                LoadFloat(objectTransformPtr->scale),
                VectorZero(),
                LoadFloat(objectTransformPtr->rotation),
                LoadFloat(objectTransformPtr->position)
            )
            : MatrixIdentity()
        ;
        SIMDVector determinant;
        const SIMDMatrix worldToObject = MatrixInverse(&determinant, objectToWorld);

        const SIMDVector localMin = LoadFloatInt(mesh.csgLocalBounds.minBounds);
        const SIMDVector localMax = LoadFloatInt(mesh.csgLocalBounds.maxBounds);
        SIMDVector worldMin{};
        SIMDVector worldMax{};
        if(!AabbTests::Transform(objectToWorld, localMin, localMax, worldMin, worldMax)){
            contentComplete = false;
            opticalScene.markIncomplete();
            continue;
        }
        RayTracingDetail::InflateSwShadowSceneBounds(worldMin, worldMax);

        SceneSwBvhInstanceGpu instance;
        StoreFloat(worldToObject, instance.worldToObject);
        instance.primitiveCount = mesh.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;

        // Build material context in scene-BVH leaf order; unresolved materials remain opaque.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(m_materialSystem.findMaterialSurfaceInfo(renderer.material, materialInfo)){
            // Software shadow, caustic, and surfel traversal evaluate the same material surface dispatcher as the hardware path. Freeze its sampled textures alongside the scene-BVH material context.
            if(
                materialInfo->shadowTransmittanceModelId != Limit<u32>::s_Max
                && !appendPreparedShadowTraceMaterialSampledTextures(*materialInfo, sampledTextureCollector)
            )
                return false;
            u32 materialConstantByteOffset = 0u;
            if(!m_materialSystem.appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                objectTransformPtr,
                shadowMaterialTypedBytes,
                shadowMutableTypedRanges,
                shadowInstance,
                materialConstantByteOffset
            ))
                return false;
            instanceMaterial = RayTracingDetail::ResolveInstanceShadowMaterial(*materialInfo, materialConstantByteOffset, meshInstanceIndex);
        }
        if(!materialInfo || materialInfo->shadowTransmittanceModelId == Limit<u32>::s_Max)
            contentComplete = false;
        if(!materialInfo || (materialInfo->transparent && materialInfo->shadowTransmittanceModelId == Limit<u32>::s_Max))
            opticalScene.markIncomplete();
        instanceMaterial.indexSlot = m_rayTracingState.m_swShadowMeshIndexHandles[meshSlot].slot();
        instanceMaterial.attributeSlot = m_rayTracingState.m_swShadowMeshAttributeHandles[meshSlot].slot();
        instanceMaterial.positionSlot = m_rayTracingState.m_swShadowMeshPositionHandles[meshSlot].slot();
        instanceMaterial.nodeSlot = m_rayTracingState.m_swShadowMeshNodeHandles[meshSlot].slot();
        SceneBvhPrimitiveCalculation bvhPrimitive;
        bvhPrimitive.aabbMin = worldMin;
        bvhPrimitive.aabbMax = worldMax;
        bvhPrimitive.centroid = VectorScale(VectorAdd(worldMin, worldMax), 0.5f);
        bvhPrimitive.transparentOccluder = (instanceMaterial.flags & RtInstanceMaterialFlag::Transparent) != 0u;

        Float3U opticalLocalMin{};
        Float3U opticalLocalMax{};
        StoreFloat(localMin, opticalLocalMin);
        StoreFloat(localMax, opticalLocalMax);
        Float34U opticalWorld{};
        StoreFloat(objectToWorld, opticalWorld);
        Float3U opticalMin{};
        Float3U opticalMax{};
        const bool opticalBoundsValid =
            bvhPrimitive.transparentOccluder && !resolvedMesh.runtime && !mesh.runtimeMesh
            && !m_world.tryGetComponent<StaticCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<SkinnedCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<CsgReceiverComponent>(entity)
            && ComputeOpticalWorldBounds(opticalWorld, opticalLocalMin, opticalLocalMax, opticalMin, opticalMax)
        ;
        opticalScene.append(entity, renderer, bvhPrimitive.transparentOccluder, opticalMin, opticalMax, opticalBoundsValid);

        sceneRefitInputs.push_back({ opticalWorld, instanceMaterial.nodeSlot, {} });
        sceneRefitRoots.push_back(mesh.swBvhNodeBuffer);
        instances.push_back(instance);
        instanceBvhPrimitives.push_back(bvhPrimitive);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    if(m_rayTracingState.m_swShadowMeshCount > m_rayTracingState.m_swShadowMeshHeapHighWater){
        m_rayTracingState.m_swShadowMeshHeapHighWater = m_rayTracingState.m_swShadowMeshCount;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: SW-shadow heap registration high-water: {} distinct meshes -> {} handles")
            , static_cast<u64>(m_rayTracingState.m_swShadowMeshCount)
            , static_cast<u64>(m_rayTracingState.m_swShadowMeshCount) * s_SoftwareRayTracingMeshBufferCount
        );
    }
    SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_swMeshHeapHandleCache);

    const u32 instanceCount = static_cast<u32>(instances.size());
    m_lightSpaceShadow.m_sceneEligible = samplingSceneTrusted && contentComplete && instanceCount <= NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK;
    m_lightSpaceShadow.m_casters.clear();
    m_lightSpaceShadow.m_casters.reserve(instanceCount);
    for(u32 index = 0u; index < instanceCount; ++index){
        const u64 vertexCount = static_cast<u64>(instances[index].primitiveCount) * 3u;
        if(vertexCount == 0u || vertexCount > Limit<u32>::s_Max)
            m_lightSpaceShadow.m_sceneEligible = false;
        m_lightSpaceShadow.m_casters.push_back({ index, static_cast<u32>(vertexCount),
            (instanceMaterials[index].flags & RtInstanceMaterialFlag::Transparent) != 0u });
    }
    if(instanceCount == 0u){
        m_rayTracingState.m_sceneBvhInstanceCount = 0u;
        m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
        m_rayTracingState.m_swShadowMaterialContextHashValid = false;
        return true;
    }

    // Topology hash includes bounds, transforms, and transparent-subtree class.
    const u64 sceneStaticHash = staticScene
        ? ComputeSceneSwBvhStaticSceneHash(instances, instanceBvhPrimitives)
        : 0u
    ;
    const usize requiredNodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    if(requiredNodeCount > static_cast<usize>(BvhNodeIndex::ChildIndexMask) + 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software scene BVH requires {} nodes, exceeding the tagged child-index limit")
            , static_cast<u64>(requiredNodeCount)
        );
        return false;
    }
    const bool canReuseSceneBvh =
        staticScene
        && m_rayTracingState.m_sceneSwBvhStaticSceneHashValid
        && m_rayTracingState.m_sceneSwBvhStaticSceneHash == sceneStaticHash
        && m_rayTracingState.m_sceneBvhInstanceCount == instanceCount
        && HasPreparedSceneBvhBuffers(m_rayTracingState, instanceCount)
    ;
    if(!staticScene || !canReuseSceneBvh)
        m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
    if(
        canReuseSceneBvh
        && !capturePreparedSceneBvhCacheReuse(sceneStaticHash, instanceCount)
    )
        return false;

    if(!canReuseSceneBvh){
        if(!ensureSceneBvhBuffers(instanceCount) || !HasPreparedSceneBvhBuffers(m_rayTracingState, instanceCount))
            return false;
        // CPU-build the small scene BVH and upload the shared node layout.
        Vector<u32, Core::Alloc::ScratchArena> indices{ scratchArena };
        indices.reserve(instanceCount);
        for(u32 i = 0u; i < instanceCount; ++i)
            indices.push_back(i);

        Vector<SceneBvhNodeCalculation, Core::Alloc::ScratchArena> buildNodes{ scratchArena };
        buildNodes.reserve(requiredNodeCount);
        Vector<u32, Core::Alloc::ScratchArena> instanceLeafCost{ scratchArena };
        instanceLeafCost.reserve(instanceCount);
        for(u32 i = 0u; i < instanceCount; ++i)
            instanceLeafCost.push_back(instances[i].primitiveCount);
        RayTracingDetail::BuildSceneBvhNode(
            indices.data(),
            0u,
            instanceCount,
            instanceBvhPrimitives.data(),
            buildNodes,
            instanceLeafCost.data()
        );
        NWB_ASSERT(buildNodes.size() == requiredNodeCount);

        Vector<NwbBvhNodeGpu, Core::Alloc::ScratchArena> nodes{ scratchArena };
        nodes.reserve(buildNodes.size());
        for(const SceneBvhNodeCalculation& buildNode : buildNodes){
            const u32 nodeIndex = static_cast<u32>(nodes.size());
            const bool leaf = (buildNode.leftChild & BvhNodeIndex::LeafFlag) != 0u;
            if(
                leaf ? ((buildNode.leftChild & ~BvhNodeIndex::LeafFlag) >= instanceCount || buildNode.rightChild != 1u)
                    : (buildNode.leftChild <= nodeIndex || buildNode.rightChild <= nodeIndex || buildNode.leftChild >= buildNodes.size() || buildNode.rightChild >= buildNodes.size())
            )
                return false;
            NwbBvhNodeGpu node;
            StoreFloatInt(buildNode.aabbMin, buildNode.leftChild, node.aabbMinLeftChild);
            const u32 taggedRightChild = buildNode.rightChild
                | (buildNode.containsTransparentOccluder ? BvhNodeIndex::TransparentSubtreeFlag : 0u)
            ;
            StoreFloatInt(buildNode.aabbMax, taggedRightChild, node.aabbMaxRightChild);
            nodes.push_back(node);
        }

        if(!capturePreparedSceneBvh(
            staticScene,
            sceneStaticHash,
            nodes.data(),
            nodes.size(),
            nodes.size() * sizeof(NwbBvhNodeGpu),
            instances.data(),
            instances.size(),
            instances.size() * sizeof(SceneSwBvhInstanceGpu)
        ))
            return false;
    }

    if(!staticScene){
        m_preparedSceneSwBvhRefit = m_sceneSwBvhRefit.prepare(
            sceneRefitInputs.data(), sceneRefitRoots.data(), sceneRefitInputs.size(),
            m_preparedSceneBvhNodeBuffer, m_preparedSceneBvhNodeHeapHandle, static_cast<u32>(requiredNodeCount), m_shaderSystem
        );
        if(!m_preparedSceneSwBvhRefit)
            return false;
    }

    // Preserve a valid typed buffer and refresh SW node-slot context independently.
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    usize materialTypedUploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(shadowMaterialTypedBytes, materialTypedUploadBytes))
        return false;
    const u64 swMaterialContextHash = ComputeShadowMaterialContextHash(
        instanceMaterials,
        shadowInstanceData,
        shadowMaterialTypedBytes
    );
    const bool canReuseSwMaterialContext =
        staticScene
        && m_rayTracingState.m_swShadowMaterialContextHashValid
        && m_rayTracingState.m_swShadowMaterialContextHash == swMaterialContextHash
        && HasPreparedShadowMaterialContextBuffers(
            m_rayTracingState,
            instanceMaterials.size(),
            shadowInstanceData.size(),
            materialTypedUploadBytes
        )
    ;
    if(
        canReuseSwMaterialContext
        && !capturePreparedShadowMaterialContextCacheReuse(
            swMaterialContextHash,
            instanceMaterials.size(),
            shadowInstanceData.size(),
            materialTypedUploadBytes
        )
    )
        return false;
    if(!canReuseSwMaterialContext){
        if(
            !ensureShadowInstanceMaterialBuffer(instances.size())
            || !ensureShadowInstanceContextBuffer(shadowInstanceData.size())
            || !ensureShadowMaterialTypedBuffer(materialTypedUploadBytes)
            || !HasPreparedShadowMaterialContextBuffers(
                m_rayTracingState,
                instanceMaterials.size(),
                shadowInstanceData.size(),
                materialTypedUploadBytes
            )
        )
            return false;
        if(!capturePreparedShadowMaterialContext(
            PreparedShadowMaterialContextRoute::Software,
            staticScene,
            swMaterialContextHash,
            instanceMaterials.data(),
            instanceMaterials.size(),
            instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu),
            shadowInstanceData.data(),
            shadowInstanceData.size(),
            shadowInstanceData.size() * sizeof(InstanceGpuData),
            shadowMaterialTypedBytes.data(),
            materialTypedUploadBytes
        ))
            return false;
    }

    m_rayTracingState.m_sceneBvhInstanceCount = instanceCount;
    const bool frozenGraphScene =
        m_preparedSceneBvhReady
        && m_preparedShadowMaterialContextReady
        && m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Software
    ;
    if(frozenGraphScene){
        if(!capturePreparedSceneSwBvhTraversal(
            preparedMeshes.data(),
            preparedMeshes.size(),
            instanceCount
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze software scene traversal"));
            clearPreparedSceneBvh();
            clearPreparedShadowMaterialContext();
            return false;
        }
    }
    else
        clearPreparedSceneSwBvhTraversal();
    if(!m_softwareOpticalScene.prepare(opticalScene))
        return false;
    u64 opticalMaterialContentHash = swMaterialContextHash;
    Fnv64AppendValue(opticalMaterialContentHash, opticalScene.contentHash());
    m_preparedSceneContentStamp = { sceneStaticHash, opticalMaterialContentHash, staticScene && contentComplete };
    RayTracingSceneContentStamp samplingStamp = m_preparedSceneContentStamp;
    // The material collector admits immutable uploaded assets/fixtures; runtime image bindings must also invalidate sampling trust.
    samplingStamp.trusted = samplingStamp.trusted && samplingSceneTrusted;
    m_rayTracingState.m_softwareTransparentSampling.m_history.prepareScene(samplingStamp);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

