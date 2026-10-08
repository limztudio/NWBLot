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
    using namespace RayTracingSoftwareBvhDetail;

    m_preparedSceneContentStamp = {};
    m_lightSpaceShadow.m_captureSceneTrusted = false;

    // Software scene BVH and material context share hardware instance ordering.
    auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystemPtr)
        return false;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();
    BeginLightSpaceCsgGather(m_lightSpaceShadow.m_csg, m_world, candidateCount, false);
    m_lightSpaceShadow.m_sceneBuffers.reserve(candidateCount * (m_lightSpaceShadow.m_csg.gathering ? 7u : 5u));

    // Parallel instance records and CPU BVH build values.
    Vector<SceneSwBvhInstanceGpu, Core::Alloc::ScratchArena> instances{ scratchArena };
    Vector<SoftwareSceneRefitInstanceGpu, Core::Alloc::ScratchArena> sceneRefitInputs{ scratchArena };
    Vector<Core::BufferHandle, Core::Alloc::ScratchArena> sceneRefitRoots{ scratchArena };
    Vector<LightSpaceShadowCaster, Core::Alloc::ScratchArena> lightSpaceCasters{ scratchArena };
    Vector<SceneBvhPrimitiveCalculation, Core::Alloc::ScratchArena> instanceBvhPrimitives{ scratchArena };
    // Parallel material records index scene-BVH leaves.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // All-occluder trace context; draw buffers hold one transparency class.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    Vector<PreparedSceneSwBvhMesh, Core::Alloc::ScratchArena> preparedMeshes{ scratchArena };
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentRangeMap::hasher(),
        ECSRenderDetail::MaterialTypedByteContentRangeMap::key_equal(),
        scratchArena
    );
    instances.reserve(candidateCount);
    sceneRefitInputs.reserve(candidateCount);
    sceneRefitRoots.reserve(candidateCount);
    lightSpaceCasters.reserve(candidateCount);
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
    bool contentComplete = true;
    bool captureSceneTrusted = true;
    u64 captureSceneIdentity = s_Fnv64OffsetBasis;
    RayTracingOpticalSceneGather opticalScene(scratchArena, candidateCount);

    ShadowMaterialSampledTextureCollector sampledTextureCollector(m_preparedShadowTraceMaterialSampledTextures, scratchArena);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        const auto meshResult = RayTracingDetail::ResolveRenderableMeshResources(
            *meshSystemPtr,
            m_meshSystem,
            entity
        );
        if(!meshResult && meshResult.error() == RenderableMeshResolution::Absent)
            continue;
        const bool meshReady = meshResult.has_value();
        // Preflight selects pending mesh storage; the prepared build and traversal validate topology before use.
        const bool topologyReady = meshReady && (meshResult->meshResources.swBvhTopologyBuilt || meshResult->meshResources.runtimeMesh || meshResult->meshResources.swBvhBuildPending);
        if(
            !meshReady
            || !topologyReady
            || !meshResult->meshResources.swBvhNodeBuffer
            || !RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(meshResult->meshResources.swBvhNodeHeapHandle)
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
        Core::Buffer* meshNodeBuffer = meshResult->meshResources.swBvhNodeBuffer.get();
        u32 meshSlot = 0u;
        const auto foundMeshSlot = meshSlotLookup.find(meshNodeBuffer);
        if(foundMeshSlot != meshSlotLookup.end())
            meshSlot = foundMeshSlot.value();
        else{
            const Core::GpuDescriptorHandle nodeHandle = meshResult->meshResources.swBvhNodeHeapHandle;
            const Core::GpuDescriptorHandle positionHandle = meshResult->meshResources.swBvhPositionHeapHandle;
            const Core::GpuDescriptorHandle indexHandle = meshResult->meshResources.swBvhTriangleIndexHeapHandle;
            if(
                !positionHandle.valid()
                || positionHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
                || !indexHandle.valid()
                || indexHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer

            ){
                SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_swMeshHeapHandleCache);
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW scene mesh buffers in the global descriptor heap"));
                return false;
            }

            const auto attributeHandle = AcquireMeshHeapHandle(heap, m_rayTracingState.m_swMeshHeapHandleCache, meshResult->meshResources.attributeBuffer);
            if(!attributeHandle){
                SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_swMeshHeapHandleCache);
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
            m_rayTracingState.m_swShadowMeshPositionBuffers.push_back(meshResult->meshResources.positionBuffer.get());
            m_rayTracingState.m_swShadowMeshIndexBuffers.push_back(meshResult->meshResources.triangleIndexBuffer.get());
            m_rayTracingState.m_swShadowMeshAttributeBuffers.push_back(meshResult->meshResources.attributeBuffer.get());
            m_rayTracingState.m_swShadowMeshNodeHandles.push_back(nodeHandle);
            m_rayTracingState.m_swShadowMeshPositionHandles.push_back(positionHandle);
            m_rayTracingState.m_swShadowMeshIndexHandles.push_back(indexHandle);
            m_rayTracingState.m_swShadowMeshAttributeHandles.push_back(*attributeHandle);
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

        const SIMDVector localMin = LoadFloatInt(meshResult->meshResources.csgLocalBounds.minBounds);
        const SIMDVector localMax = LoadFloatInt(meshResult->meshResources.csgLocalBounds.maxBounds);
        const auto worldBounds = AabbTests::Transform(objectToWorld, localMin, localMax);
        if(!worldBounds){
            contentComplete = false;
            opticalScene.markIncomplete();
            continue;
        }
        SIMDVector worldMin = worldBounds->minBounds;
        SIMDVector worldMax = worldBounds->maxBounds;
        RayTracingDetail::InflateSwShadowSceneBounds(worldMin, worldMax);

        SceneSwBvhInstanceGpu instance;
        StoreFloat(worldToObject, instance.worldToObject);
        instance.primitiveCount = meshResult->meshResources.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;

        // Build material context in scene-BVH leaf order; unresolved materials remain opaque.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(const auto materialInfoResult = m_materialSystem.findMaterialSurfaceInfo(renderer.material); materialInfoResult){
            materialInfo = *materialInfoResult;
            // Software shadow, caustic, and surfel traversal evaluate the same material surface dispatcher as the hardware path. Freeze its sampled textures alongside the scene-BVH material context.
            if(
                materialInfo->surfaceDispatchId != Limit<u32>::s_Max
                && !appendPreparedShadowTraceMaterialSampledTextures(*materialInfo, sampledTextureCollector)
            )
                return false;
            const auto materialContext = m_materialSystem.appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                objectTransformPtr,
                shadowMaterialTypedBytes,
                shadowMutableTypedRanges
            );
            if(!materialContext)
                return false;
            shadowInstance = materialContext->instance;
            instanceMaterial = RayTracingDetail::ResolveInstanceShadowMaterial(*materialInfo, materialContext->constantByteOffset, meshInstanceIndex);
        }
        if(!materialInfo || materialInfo->surfaceDispatchId == Limit<u32>::s_Max)
            contentComplete = false;
        if(!materialInfo || (materialInfo->transparent && materialInfo->surfaceDispatchId == Limit<u32>::s_Max))
            opticalScene.markIncomplete();
        instanceMaterial.indexSlot = m_rayTracingState.m_swShadowMeshIndexHandles[meshSlot].slot();
        instanceMaterial.attributeSlot = m_rayTracingState.m_swShadowMeshAttributeHandles[meshSlot].slot();
        instanceMaterial.positionSlot = m_rayTracingState.m_swShadowMeshPositionHandles[meshSlot].slot();
        instanceMaterial.nodeSlot = m_rayTracingState.m_swShadowMeshNodeHandles[meshSlot].slot();
        SceneBvhPrimitiveCalculation bvhPrimitive;
        bvhPrimitive.aabbMin = worldMin;
        bvhPrimitive.aabbMax = worldMax;
        constexpr f32 s_BoundsMidpointWeight = 0.5f;
        bvhPrimitive.centroid = VectorScale(VectorAdd(worldMin, worldMax), s_BoundsMidpointWeight);
        bvhPrimitive.transparentOccluder = (instanceMaterial.flags & RtInstanceMaterialFlag::Transparent) != 0u;

        Float3U opticalLocalMin{};
        Float3U opticalLocalMax{};
        StoreFloat(localMin, opticalLocalMin);
        StoreFloat(localMax, opticalLocalMax);
        Float34U opticalWorld{};
        StoreFloat(objectToWorld, opticalWorld);
        Float3U opticalMin{};
        Float3U opticalMax{};
        bool opticalBoundsValid = false;
        if(
            bvhPrimitive.transparentOccluder && !meshResult->resolvedMesh.runtime && !meshResult->meshResources.runtimeMesh
            && !m_world.tryGetComponent<StaticCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<SkinnedCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<CsgReceiverComponent>(entity)
        ){
            const auto bounds = ComputeOpticalWorldBounds(opticalWorld, opticalLocalMin, opticalLocalMax);
            if(bounds){
                opticalMin = bounds->minimum;
                opticalMax = bounds->maximum;
                opticalBoundsValid = true;
            }
        }
        opticalScene.append(entity, renderer, bvhPrimitive.transparentOccluder, opticalMin, opticalMax, opticalBoundsValid);

        // Preserve the exact emitted instance/boundary ordering, while allowing only world transforms to lag one frame.
        Fnv64AppendValue(captureSceneIdentity, entity.id);
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.positionBuffer.get());
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.triangleIndexBuffer.get());
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.attributeBuffer.get());
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.meshletCount);
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.meshletDescBuffer.get());
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.meshletLocalBoundsBuffer.get());
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.meshletDescHeapHandle.value);
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.meshletLocalBoundsHeapHandle.value);
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.meshletPrimitiveIndexCount);
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.runtimeMeshVersion);
        Fnv64AppendValue(captureSceneIdentity, meshResult->meshResources.runtimeGeometryContentRevision);
        Fnv64AppendValue(captureSceneIdentity, instanceMaterial);
        Fnv64AppendValue(captureSceneIdentity, shadowInstance.translation.w);
        Fnv64AppendValue(captureSceneIdentity, shadowInstance.geometryHeapSlots);
        Fnv64AppendValue(captureSceneIdentity, opticalScene.instances.back());
        if((meshResult->resolvedMesh.runtime || meshResult->meshResources.runtimeMesh) && meshResult->meshResources.runtimeGeometryContentRevision == 0u)
            captureSceneTrusted = false;
        m_lightSpaceShadow.m_sceneBuffers.push_back(meshResult->meshResources.positionBuffer);
        m_lightSpaceShadow.m_sceneBuffers.push_back(meshResult->meshResources.triangleIndexBuffer);
        m_lightSpaceShadow.m_sceneBuffers.push_back(meshResult->meshResources.attributeBuffer);
        if(meshResult->meshResources.meshletCount != 0u){
            m_lightSpaceShadow.m_sceneBuffers.push_back(meshResult->meshResources.meshletDescBuffer);
            m_lightSpaceShadow.m_sceneBuffers.push_back(meshResult->meshResources.meshletLocalBoundsBuffer);
        }
        if(m_lightSpaceShadow.m_csg.gathering){
            if(meshResult->meshResources.runtimeLocalBoundsBuffer)
                m_lightSpaceShadow.m_sceneBuffers.push_back(meshResult->meshResources.runtimeLocalBoundsBuffer);
            if(meshResult->meshResources.swBvhNodeBuffer)
                m_lightSpaceShadow.m_sceneBuffers.push_back(meshResult->meshResources.swBvhNodeBuffer);
        }

        AppendLightSpaceCsgReceiver(m_lightSpaceShadow.m_csg, entity, bvhPrimitive.transparentOccluder, objectToWorld, meshResult->meshResources);
        sceneRefitInputs.push_back({ opticalWorld, instanceMaterial.nodeSlot, {} });
        sceneRefitRoots.push_back(meshResult->meshResources.swBvhNodeBuffer);
        lightSpaceCasters.push_back({
            .triangleIndexBuffer = meshResult->meshResources.triangleIndexBuffer,
            .meshletDescBuffer = meshResult->meshResources.meshletDescBuffer,
            .meshletBoundsBuffer = meshResult->meshResources.meshletLocalBoundsBuffer,
            .meshletCount = meshResult->meshResources.meshletCount,
            .meshletDescSlot = meshResult->meshResources.meshletCount != 0u ? meshResult->meshResources.meshletDescHeapHandle.slot() : 0u,
            .meshletBoundsSlot = meshResult->meshResources.meshletCount != 0u ? meshResult->meshResources.meshletLocalBoundsHeapHandle.slot() : 0u,
        });
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

    if(!FinishLightSpaceCsgGather(m_lightSpaceShadow.m_csg, m_world, m_csgShapeRegistry, scratchArena))
        return false;
    const auto& csg = m_lightSpaceShadow.m_csg.snapshot;
    const u32 instanceCount = static_cast<u32>(instances.size());
    m_lightSpaceShadow.m_sceneEligible = contentComplete && instanceCount <= NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK;
    m_lightSpaceShadow.m_casters.clear();
    m_lightSpaceShadow.m_casters.reserve(instanceCount);
    for(u32 index = 0u; index < instanceCount; ++index){
        const u64 indexCount = static_cast<u64>(instances[index].primitiveCount) * 3u;
        LightSpaceShadowCaster& caster = lightSpaceCasters[index];
        const Core::BufferHandle& indexBuffer = caster.triangleIndexBuffer;
        if(
            indexCount == 0u || indexCount > Limit<u32>::s_Max || !indexBuffer
            || !indexBuffer->getCreationDescription().isIndexBuffer
            || indexBuffer->getCreationDescription().byteSize < indexCount * sizeof(u32)
        )
            m_lightSpaceShadow.m_sceneEligible = false;
        const bool csgReceiver = csg.hasCsg && (csg.receiverRanges[index].flags & NWB_CSG_RAY_RECEIVER_ACTIVE) != 0u;
        if(csgReceiver)
            instanceMaterials[index].flags |= NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW;
        caster.instanceIndex = index;
        caster.indexCount = static_cast<u32>(indexCount);
        caster.transparent = (instanceMaterials[index].flags & RtInstanceMaterialFlag::Transparent) != 0u;
        caster.csg = csgReceiver;
        m_lightSpaceShadow.m_casters.push_back(Move(caster));
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
            NotNull<const u32*>(instanceLeafCost.data())
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
    const usize materialTypedUploadBytes = ECSRenderDetail::ResolveMaterialTypedUploadByteCount(shadowMaterialTypedBytes);
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
    if(csg.hasCsg)
        Fnv64AppendValue(samplingStamp.material, csg.identity);
    m_rayTracingState.m_softwareTransparentSampling.m_history.prepareScene(samplingStamp);
    Fnv64AppendValue(captureSceneIdentity, instanceCount);
    Fnv64AppendBuffer(captureSceneIdentity, shadowMaterialTypedBytes.data(), shadowMaterialTypedBytes.size());
    for(const auto& texture : m_preparedShadowTraceMaterialSampledTextures)
        Fnv64AppendValue(captureSceneIdentity, texture.get());
    m_lightSpaceShadow.m_sceneTextures.assign(m_preparedShadowTraceMaterialSampledTextures.begin(), m_preparedShadowTraceMaterialSampledTextures.end());
    // Sampled material assets are immutable under this collector; resource/shader invalidation also clears capture history.
    if(csg.hasCsg){
        Fnv64AppendValue(captureSceneIdentity, csg.identity);
        if(
            m_lightSpaceShadow.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseOneFrame
            || m_lightSpaceShadow.m_settings.captureCadence == SoftwareShadowCaptureCadence::ReuseTwoFrames
        ){
            captureSceneIdentity = BuildLightSpaceCsgCaptureIdentity(
                m_lightSpaceShadow.m_csg, instanceMaterials.data(), shadowInstanceData.data(), instanceMaterials.size(),
                shadowMaterialTypedBytes.data(), shadowMaterialTypedBytes.size(),
                m_preparedShadowTraceMaterialSampledTextures.data(), m_preparedShadowTraceMaterialSampledTextures.size()
            );
            captureSceneTrusted = m_lightSpaceShadow.m_csg.captureGeometryTrusted;
        }
    }
    m_lightSpaceShadow.m_captureSceneIdentity = captureSceneIdentity;
    m_lightSpaceShadow.m_captureSceneTrusted = captureSceneTrusted && contentComplete;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

