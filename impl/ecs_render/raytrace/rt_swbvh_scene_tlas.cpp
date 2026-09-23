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


bool RendererRayTracingSystem::prepareSceneTlasResources(Core::Alloc::ScratchArena& scratchArena){
    return buildSceneTlasImpl(nullptr, scratchArena);
}

bool RendererRayTracingSystem::buildSceneTlas(
    Core::CommandList& commandList,
    Core::Alloc::ScratchArena& scratchArena,
    const bool shadowMaterialContextBatchGraphOwned
){
    return buildSceneTlasImpl(&commandList, scratchArena, shadowMaterialContextBatchGraphOwned);
}

bool RendererRayTracingSystem::buildSceneTlasImpl(
    Core::CommandList* const commandList,
    Core::Alloc::ScratchArena& scratchArena,
    const bool shadowMaterialContextBatchGraphOwned
){
    using namespace __hidden_rt_swbvh;

    if(!commandList)
        m_preparedSceneContentStamp = {};

    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystemPtr)
        return false;

    auto rendererView = m_world.view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena> instances{ scratchArena };
    // RayTracingInstanceDesc contains only a raw BLAS pointer. The opaque graph-owned TLAS build retains this parallel handle stream until the accepting Shadow Preparation packet has submitted.
    Vector<Core::RayTracingAccelStructHandle, Core::Alloc::ScratchArena> instanceBlases{ scratchArena };
    // Kept parallel to instances for hardware InstanceID lookup.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // All-occluder trace context; draw buffers hold one transparency class.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        scratchArena
    );
    instances.reserve(candidateCount);
    instanceBlases.reserve(candidateCount);
    instanceMaterials.reserve(candidateCount);
    shadowInstanceData.reserve(candidateCount);
    shadowMutableTypedRanges.reserve(candidateCount);
    MeshBufferSlotLookup meshSlotLookup(
        0,
        Hasher<const Core::Buffer*>(),
        EqualTo<const Core::Buffer*>(),
        scratchArena
    );
    meshSlotLookup.reserve(candidateCount);

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware TLAS build requires the descriptor-buffer TLAS heap layout"));
        return false;
    }
    BeginMeshHeapHandleGather(m_rayTracingState.m_hwMeshHeapHandleCache);
    const auto resolveMeshHeapHandle = [&](const Core::BufferHandle& buffer, Core::GpuDescriptorHandle& outHandle){
        return commandList
            ? FindPreparedMeshHeapHandle(m_rayTracingState.m_hwMeshHeapHandleCache, buffer, outHandle)
            : AcquireMeshHeapHandle(heap, m_rayTracingState.m_hwMeshHeapHandleCache, buffer, outHandle)
        ;
    };
    m_rayTracingState.m_shadowMeshIndexBuffers.clear();
    m_rayTracingState.m_shadowMeshAttributeBuffers.clear();
    m_rayTracingState.m_shadowMeshPositionBuffers.clear();
    m_rayTracingState.m_shadowMeshIndexHandles.clear();
    m_rayTracingState.m_shadowMeshAttributeHandles.clear();
    m_rayTracingState.m_shadowMeshPositionHandles.clear();
    m_rayTracingState.m_shadowMeshCount = 0u;
    // Gates transparent-shadow resource preparation.
    m_rayTracingState.m_sceneHasTransparentOccluder = false;
    bool staticScene = true;
    bool contentComplete = true;
    RayTracingOpticalSceneGather opticalScene(scratchArena, candidateCount);

    Optional<ShadowMaterialSampledTextureCollector> sampledTextureCollector;
    if(!commandList)
        sampledTextureCollector.emplace(m_preparedShadowTraceMaterialSampledTextures, scratchArena);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
        RenderableMeshDesc resolvedMesh;
        const RenderableMeshResolution::Enum meshResolution = RayTracingDetail::ResolveRenderableMeshResources(
            *meshSystemPtr,
            m_meshSystem,
            entity,
            resolvedMesh,
            mesh
        );
        // An entity without a mesh attachment contributes no geometry to the scene.
        if(meshResolution == RenderableMeshResolution::Absent)
            continue;
        if(
            meshResolution != RenderableMeshResolution::Ready || !mesh.blas
            || !mesh.triangleIndexBuffer || !mesh.attributeBuffer || !mesh.positionBuffer
        ){
            contentComplete = false;
            opticalScene.markIncomplete();
            continue;
        }
        // Runtime mesh updates disable static TLAS reuse.
        if(resolvedMesh.runtime || mesh.runtimeMesh)
            staticScene = false;

        // Reuse one table slot for instances sharing geometry.
        Core::Buffer* meshIndexBuffer = mesh.triangleIndexBuffer.get();
        u32 meshSlot = 0u;
        const auto foundMeshSlot = meshSlotLookup.find(meshIndexBuffer);
        if(foundMeshSlot != meshSlotLookup.end())
            meshSlot = foundMeshSlot.value();
        else{
            Core::GpuDescriptorHandle indexHandle;
            Core::GpuDescriptorHandle attributeHandle;
            Core::GpuDescriptorHandle positionHandle;
            if(
                !resolveMeshHeapHandle(mesh.triangleIndexBuffer, indexHandle)
                || !resolveMeshHeapHandle(mesh.attributeBuffer, attributeHandle)
                || !resolveMeshHeapHandle(mesh.positionBuffer, positionHandle)
            ){
                if(!commandList){
                    SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_hwMeshHeapHandleCache);
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register HW scene mesh buffers in the global descriptor heap"));
                }
                else
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: HW scene mesh descriptor was not prepared before recording"));
                return false;
            }

            meshSlot = m_rayTracingState.m_shadowMeshCount;
            m_rayTracingState.m_shadowMeshIndexBuffers.push_back(meshIndexBuffer);
            m_rayTracingState.m_shadowMeshAttributeBuffers.push_back(mesh.attributeBuffer.get());
            m_rayTracingState.m_shadowMeshPositionBuffers.push_back(mesh.positionBuffer.get());
            m_rayTracingState.m_shadowMeshIndexHandles.push_back(indexHandle);
            m_rayTracingState.m_shadowMeshAttributeHandles.push_back(attributeHandle);
            m_rayTracingState.m_shadowMeshPositionHandles.push_back(positionHandle);
            meshSlotLookup.emplace(meshIndexBuffer, meshSlot);
            ++m_rayTracingState.m_shadowMeshCount;
        }

        Core::RayTracingInstanceDesc instanceDesc;
        instanceDesc.setBLAS(mesh.blas.get());
        instanceDesc.setInstanceID(static_cast<u32>(instances.size()));
        instanceDesc.setInstanceMask(s_RayTracingAllInstanceMask);

        const NWB::Impl::Scene::TransformComponent* transformPtr = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        if(transformPtr){
            const SIMDMatrix instanceWorld = MatrixAffineTransformation(
                LoadFloat(transformPtr->scale),
                VectorZero(),
                LoadFloat(transformPtr->rotation),
                LoadFloat(transformPtr->position)
            );
            StoreFloat(instanceWorld, instanceDesc.transform);
        }

        // Build material context in InstanceID order; unresolved materials remain opaque.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(m_materialSystem.findMaterialSurfaceInfo(renderer.material, materialInfo)){
            if(materialInfo->transparent)
                m_rayTracingState.m_sceneHasTransparentOccluder = true;
            // The trace surface dispatcher reads this material's Texture2D fields through non-uniform bindless slots. Retain the exact resolved handles during preflight; recording must never discover a texture after the shared graph fixed its immutable resource set.
            if(
                !commandList
                && materialInfo->shadowTransmittanceModelId != Limit<u32>::s_Max
                && !appendPreparedShadowTraceMaterialSampledTextures(*materialInfo, *sampledTextureCollector)
            )
                return false;
            u32 materialConstantByteOffset = 0u;
            if(!m_materialSystem.appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                transformPtr,
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
        // An opaque surface hook can be unavailable without hiding an optical boundary. Unknown classification or an unevaluable transparent surface cannot support the outside-volume shortcut.
        if(!materialInfo || (materialInfo->transparent && materialInfo->shadowTransmittanceModelId == Limit<u32>::s_Max))
            opticalScene.markIncomplete();
        instanceMaterial.indexSlot = m_rayTracingState.m_shadowMeshIndexHandles[meshSlot].slot();
        instanceMaterial.attributeSlot = m_rayTracingState.m_shadowMeshAttributeHandles[meshSlot].slot();
        instanceMaterial.positionSlot = m_rayTracingState.m_shadowMeshPositionHandles[meshSlot].slot();

        // Opaque candidates terminate RayQuery; software handles transparent transmittance.
        if(!(materialInfo && materialInfo->transparent))
            instanceDesc.setFlags(Core::RayTracingInstanceFlags::ForceOpaque);

        const bool transparent = (instanceMaterial.flags & RtInstanceMaterialFlag::Transparent) != 0u;
        instanceDesc.setInstanceMask(NWB_RT_OPTICAL_BASE_INSTANCE_MASK
            | (transparent ? NWB_RT_OPTICAL_TRANSPARENT_INSTANCE_MASK : 0u));
        Float3U opticalLocalMin{};
        Float3U opticalLocalMax{};
        StoreFloat(LoadFloatInt(mesh.csgLocalBounds.minBounds), opticalLocalMin);
        StoreFloat(LoadFloatInt(mesh.csgLocalBounds.maxBounds), opticalLocalMax);
        Float34U opticalWorld{};
        StoreFloat(LoadFloat(instanceDesc.transform), opticalWorld);
        Float3U opticalMin{};
        Float3U opticalMax{};
        const bool opticalBoundsValid =
            transparent && !resolvedMesh.runtime && !mesh.runtimeMesh && mesh.csgLocalBounds.valid()
            && !m_world.tryGetComponent<StaticCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<SkinnedCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<CsgReceiverComponent>(entity)
            && ComputeOpticalWorldBounds(opticalWorld, opticalLocalMin, opticalLocalMax, opticalMin, opticalMax)
        ;
        const bool runtimeOpticalBounds =
            transparent && (resolvedMesh.runtime || mesh.runtimeMesh) && mesh.runtimeLocalBoundsBuffer
            && mesh.runtimeLocalBoundsHeapHandle.valid()
            && !m_world.tryGetComponent<StaticCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<SkinnedCsgMeshComponent>(entity)
            && !m_world.tryGetComponent<CsgReceiverComponent>(entity)
        ;
        if(runtimeOpticalBounds)
            opticalScene.appendRuntime(entity, renderer, mesh.runtimeLocalBoundsBuffer, mesh.runtimeLocalBoundsHeapHandle, opticalWorld);
        else
            opticalScene.append(entity, renderer, transparent, opticalMin, opticalMax, opticalBoundsValid);

        instances.push_back(instanceDesc);
        instanceBlases.push_back(mesh.blas);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    if(m_rayTracingState.m_shadowMeshCount > m_rayTracingState.m_shadowMeshHeapHighWater){
        m_rayTracingState.m_shadowMeshHeapHighWater = m_rayTracingState.m_shadowMeshCount;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: HW-shadow heap registration high-water: {} distinct meshes -> {} handles")
            , static_cast<u64>(m_rayTracingState.m_shadowMeshCount)
            , static_cast<u64>(m_rayTracingState.m_shadowMeshCount) * s_HardwareRayTracingMeshBufferCount
        );
    }
    if(!commandList)
        SweepUnseenMeshHeapHandles(heap, m_rayTracingState.m_hwMeshHeapHandleCache);

    m_rayTracingState.m_tlasInstanceCount = static_cast<u32>(instances.size());
    if(instances.empty()){
        m_rayTracingState.m_tlasDeviceAddress = 0u;
        m_rayTracingState.m_tlasStaticSceneHashValid = false;
        m_rayTracingState.m_hwShadowMaterialContextHashValid = false;
        return false;
    }

    // TLAS and material context use separate static keys.
    const u64 tlasStaticSceneHash = staticScene ? ComputeTlasStaticSceneHash(instances) : 0u;
    const bool canReuseTlas =
        staticScene
        && m_rayTracingState.m_tlasStaticSceneHashValid
        && m_rayTracingState.m_tlasStaticSceneHash == tlasStaticSceneHash
        && m_rayTracingState.m_tlas
        && m_rayTracingState.m_tlasMaxInstances >= instances.size()
        && IsAccelStructHeapHandle(m_rayTracingState.m_tlasHeapHandle)
    ;
    if(!staticScene || !canReuseTlas)
        m_rayTracingState.m_tlasStaticSceneHashValid = false;

    if(
        commandList
        && (
            !m_rayTracingState.m_tlas
            || m_rayTracingState.m_tlasMaxInstances < instances.size()
            || !IsAccelStructHeapHandle(m_rayTracingState.m_tlasHeapHandle)
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: scene TLAS changed after preflight; skipping recording-time replacement"));
        return false;
    }

    if(!canReuseTlas && (!m_rayTracingState.m_tlas || m_rayTracingState.m_tlasMaxInstances < instances.size())){
        const usize capacity = ::NextGrowingCapacity(
            m_rayTracingState.m_tlasMaxInstances,
            instances.size(),
            s_TlasInitialInstanceCapacity
        );

        Core::RayTracingAccelStructDesc accelStructDesc(m_arena);
        accelStructDesc.setTopLevelMaxInstances(capacity);
        accelStructDesc.setBuildFlags(Core::RayTracingAccelStructBuildFlags::PreferFastTrace);
        accelStructDesc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
        accelStructDesc.setDebugName(Name("scene_tlas"));

        auto& device = m_graphics.getDevice();
        Core::RayTracingAccelStructHandle tlas = device.createAccelStruct(accelStructDesc);
        if(!tlas){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene TLAS (capacity {})"), static_cast<u64>(capacity));
            return false;
        }
        // Retire the old heap block before replacing the TLAS generation.
        if(m_rayTracingState.m_tlasHeapHandle.valid()){
            heap.free(m_rayTracingState.m_tlasHeapHandle);
            m_rayTracingState.m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        m_rayTracingState.m_tlas = Move(tlas);
        m_rayTracingState.m_tlasBackingFresh = true;
        m_rayTracingState.m_tlasBackingStateHandoffPending = false;
        m_rayTracingState.m_tlasMaxInstances = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created scene TLAS (capacity {} instances)"), static_cast<u64>(capacity));
    }

    if(!canReuseTlas && commandList){
        // The backend records the acceleration-structure build directly. Keep the task graph's declared AccelStructRead boundary truthful by explicitly publishing the native build write and its final read.
        commandList->setAccelStructState(m_rayTracingState.m_tlas.get(), Core::ResourceStates::AccelStructWrite);
        commandList->commitBarriers();
        commandList->buildTopLevelAccelStruct(
            m_rayTracingState.m_tlas.get(),
            instances.data(),
            instances.size(),
            Core::RayTracingAccelStructBuildFlags::PreferFastTrace
        );
        commandList->setAccelStructState(m_rayTracingState.m_tlas.get(), Core::ResourceStates::AccelStructRead);
        commandList->commitBarriers();
        if(m_rayTracingState.m_tlasBackingFresh)
            m_rayTracingState.m_tlasBackingStateHandoffPending = true;
    }
    m_rayTracingState.m_tlasDeviceAddress = m_rayTracingState.m_tlas->getDeviceAddress();

    // Allocate a heap block only for a new TLAS generation.
    if(!IsAccelStructHeapHandle(m_rayTracingState.m_tlasHeapHandle)){
        if(commandList){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: scene TLAS heap view was absent after preflight"));
            return false;
        }
        if(m_rayTracingState.m_tlasHeapHandle.valid()){
            heap.free(m_rayTracingState.m_tlasHeapHandle);
            m_rayTracingState.m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        const Core::GpuDescriptorHandle tlasHeapHandle = heap.allocate(Core::GpuDescriptorClass::AccelStruct);
        if(
            !tlasHeapHandle.valid()
            || !heap.write(tlasHeapHandle, Core::DescriptorWriteItem::RayTracingAccelStruct(0u, m_rayTracingState.m_tlas.get()))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register scene TLAS in the descriptor-buffer heap"));
            if(tlasHeapHandle.valid())
                heap.free(tlasHeapHandle);
            return false;
        }
        m_rayTracingState.m_tlasHeapHandle = tlasHeapHandle;
    }

    // Preserve a valid typed buffer and hash the descriptor-slot representation.
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    usize materialTypedUploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(shadowMaterialTypedBytes, materialTypedUploadBytes))
        return false;
    if(
        commandList
        && !HasPreparedShadowMaterialContextBuffers(
            m_rayTracingState,
            instanceMaterials.size(),
            shadowInstanceData.size(),
            materialTypedUploadBytes
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW shadow material context changed after preflight; skipping recording-time replacement"));
        return false;
    }
    const u64 hwMaterialContextHash = ComputeShadowMaterialContextHash(
        instanceMaterials,
        shadowInstanceData,
        shadowMaterialTypedBytes
    );
    u64 gatheredMaterialContentHash = hwMaterialContextHash;
    const bool canReuseHwMaterialContext =
        staticScene
        && !m_rayTracingState.m_sceneHasTransparentOccluder
        && m_rayTracingState.m_hwShadowMaterialContextHashValid
        && m_rayTracingState.m_hwShadowMaterialContextHash == hwMaterialContextHash
        && HasPreparedShadowMaterialContextBuffers(
            m_rayTracingState,
            instanceMaterials.size(),
            shadowInstanceData.size(),
            materialTypedUploadBytes
        )
    ;
    if(!canReuseHwMaterialContext){
        if(!commandList){
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
                PreparedShadowMaterialContextRoute::Hardware,
                staticScene,
                hwMaterialContextHash,
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
        if(commandList){
            if(shadowMaterialContextBatchGraphOwned){
                if(!matchesPreparedShadowMaterialContext(
                    PreparedShadowMaterialContextRoute::Hardware,
                    staticScene,
                    hwMaterialContextHash,
                    instanceMaterials.data(),
                    instanceMaterials.size(),
                    instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu),
                    shadowInstanceData.data(),
                    shadowInstanceData.size(),
                    shadowInstanceData.size() * sizeof(InstanceGpuData),
                    shadowMaterialTypedBytes.data(),
                    materialTypedUploadBytes
                )){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW shadow material context changed after graph preflight; rejecting frozen upload batch"));
                    return false;
                }
            }
            else{
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: changed HW shadow material context has no graph-owned upload batch"));
                return false;
            }
        }
    }
    else if(commandList && shadowMaterialContextBatchGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: graph-owned HW shadow material context unexpectedly reused a native cache"));
        return false;
    }

    // Freeze the selected hardware instance stream before recording; accepted preparation publishes its cache identity.
    if(!commandList && !canReuseTlas){
        if(!capturePreparedSceneTlasBuild(
            staticScene,
            tlasStaticSceneHash,
            instances,
            instanceBlases
        )){
            if(!m_rayTracingState.m_sceneHasTransparentOccluder){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: could not freeze opaque scene TLAS build after preflight"));
                return false;
            }
            // A capture miss retains the direct TLAS recorder against the same preflighted material context.
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze scene TLAS build; retaining direct retry fallback"));
        }
    }

    if(staticScene && commandList){
        m_rayTracingState.m_tlasStaticSceneHash = tlasStaticSceneHash;
        m_rayTracingState.m_tlasStaticSceneHashValid = true;
    }
    // Publish semantic identity from the complete current gather, including cache-hit frames. Missing geometry or an unresolved surface hook must disable temporal consumers without changing acceleration-cache policy.
    if(!commandList){
        if(!m_hardwareOpticalScene.prepare(opticalScene) || !m_hardwareOpticalScene.prepareRuntimeBounds(opticalScene, m_shaderSystem))
            return false;
        Fnv64AppendValue(gatheredMaterialContentHash, opticalScene.contentHash());
        m_preparedSceneContentStamp = { tlasStaticSceneHash, gatheredMaterialContentHash, staticScene && contentComplete };
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

