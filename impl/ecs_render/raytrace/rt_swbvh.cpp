// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace{
    // Phase 2 M1: register one per-mesh backing buffer in the global descriptor heap, returning its handle. Every
    // backing buffer registers as a single STORAGE_BUFFER; the raw-vs-structured split is only a shader-side view
    // (P3 aliases) and write() forces the canonical STORAGE_BUFFER type, so the SRV factory choice is documentation.
    // Returns an invalid handle if the resource namespace is exhausted (allocate() logs it); free() then ignores it.
    [[nodiscard]] Core::GpuDescriptorHandle RegisterMeshHeapBuffer(Core::GpuDescriptorHeap& heap, const Core::BindingSetItem& view){
        const Core::GpuDescriptorHandle handle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(handle.valid())
            heap.write(handle, view);
        return handle;
    }
    // Return a per-mesh heap handle to the deferred-free quarantine and invalidate the stored slot. Safe on an
    // already-invalid handle (heap.free ignores it), so a partially-filled prior frame retires cleanly.
    void ReleaseMeshHeapHandle(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle& handle){
        heap.free(handle);
        handle = Core::GpuDescriptorHandle::invalid();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::buildPendingMeshSwBvh(Core::CommandList& commandList){
    // Builds/refits per-mesh software BVHs. Two callers gate WHEN it runs:
    //  - The no-RayQuery fallback prepare (the only shadow backend) -- builds every mesh.
    //  - The hybrid prepare on RT hardware -- only when the scene has a TRANSPARENT occluder (whose colored shadow the
    //    software pass must trace); opaque-only / no-transparent scenes never call this, so they pay no software cost.
    // Resource allocation happens first in preparePendingMeshSwBvhResources; this pass only records build/refit work.
    bool allBuildsReady = true;
    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();

        // Runtime (skinned/deforming) meshes update their software BVH every frame from the freshly skinned
        // positions; static meshes build once and clear the pending flag.
        if(meshResources.runtimeMesh){
            if(!updateMeshSwBvh(commandList, meshResources)){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: runtime mesh '{}' software BVH update failed")
                    , StringConvert(meshResources.meshName.c_str())
                );
                allBuildsReady = false;
            }
            continue;
        }

        if(!meshResources.swBvhBuildPending)
            continue;
        if(updateMeshSwBvh(commandList, meshResources))
            meshResources.swBvhBuildPending = false;
        else{
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: static mesh '{}' software BVH build failed")
                , StringConvert(meshResources.meshName.c_str())
            );
            allBuildsReady = false;
        }
    }
    return allBuildsReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::preparePendingMeshSwBvhResources(){
    // Keep allocation out of updateMeshSwBvh: recording the per-frame build/refit command stream may only consume
    // the resources prepared here. Runtime meshes are intentionally included every frame because a provider can
    // replace the current cache entry between frames; static meshes stay in the set until their first build succeeds.
    bool allResourcesReady = true;
    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();
        if(!meshResources.runtimeMesh && !meshResources.swBvhBuildPending)
            continue;

        if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer){
            allResourcesReady = false;
            continue;
        }
        // meshletPrimitiveIndexCount is the reconstructed triangle-index count (3 per triangle).
        if(meshResources.meshletPrimitiveIndexCount == 0u || (meshResources.meshletPrimitiveIndexCount % 3u) != 0u){
            allResourcesReady = false;
            continue;
        }

        const u32 primitiveCount = meshResources.meshletPrimitiveIndexCount / 3u;
        if(!ensureMeshSwBvhResources(
            meshResources.positionBuffer.get(),
            meshResources.triangleIndexBuffer.get(),
            primitiveCount,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhBindingSet
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software BVH resource preparation failed for mesh '{}'")
                , StringConvert(meshResources.meshName.c_str())
            );
            allResourcesReady = false;
        }
    }
    return allResourcesReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::buildSceneTlas(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena> instances{ scratchArena };
    // Per-instance occluder material, built lockstep with `instances` (one record per push, same order) so the
    // uploaded table indexes by the hardware InstanceID() the trace reads.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // Shadow-OWNED combined material-constants context, built lockstep with `instances`: the per-occluder
    // InstanceGpuData (g_NwbMeshInstances for the trace; index == InstanceID()) + the combined typed bytes
    // (g_NwbMaterialTypedWords for the trace; each occluder's constant + mutable block). The draw pass's buffers
    // hold only the opaque set at trace time, so the trace cannot use them -- see ensureShadowBindingSet.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        scratchArena
    );
    instances.reserve(rendererView.candidateCount());
    instanceMaterials.reserve(rendererView.candidateCount());
    shadowInstanceData.reserve(rendererView.candidateCount());
    shadowMutableTypedRanges.reserve(rendererView.candidateCount());

    // Reset the per-frame distinct-mesh table; the gather repopulates it (slot k -> mesh k's index/attribute/
    // position buffers) indexed by material.meshSlot. Each RT pass fetches that geometry from the global
    // descriptor heap via the per-record slots meshSlot fills below (NwbHeapRawBuffer(<x>Slot)); the table now
    // drives only host-side work -- barriering the backing buffers to ShaderResource and populating those slots.
    // The table is rebuilt every frame (the unconditional reset just below), so retire last frame's handles here
    // before it is repopulated; the deferred-free quarantine (module.cpp pumps advanceFrame() per frame) keeps
    // slots that in-flight frames still reference valid. Guarded on a live heap so non-bindless builds are unaffected.
    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    const bool heapLive = heap.isInitialized();
    if(heapLive){
        auto& rt = rayTracingState();
        for(u32 slot = 0u; slot < rt.m_shadowMeshCount; ++slot){
            ReleaseMeshHeapHandle(heap, rt.m_shadowMeshIndexHandles[slot]);
            ReleaseMeshHeapHandle(heap, rt.m_shadowMeshAttributeHandles[slot]);
            ReleaseMeshHeapHandle(heap, rt.m_shadowMeshPositionHandles[slot]);
        }
    }
    // Clear the distinct-mesh table for this frame's rebuild. The Vectors retain capacity (they grow once to the
    // scene's steady-state distinct-mesh count, then reuse that storage) and m_shadowMeshCount mirrors their length --
    // the gather below repopulates both by push_back.
    rayTracingState().m_shadowMeshIndexBuffers.clear();
    rayTracingState().m_shadowMeshAttributeBuffers.clear();
    rayTracingState().m_shadowMeshPositionBuffers.clear();
    rayTracingState().m_shadowMeshIndexHandles.clear();
    rayTracingState().m_shadowMeshAttributeHandles.clear();
    rayTracingState().m_shadowMeshPositionHandles.clear();
    rayTracingState().m_shadowMeshCount = 0u;
    // Whether any gathered occluder is transparent. On RT hardware this gates the hybrid software-transparent-shadow
    // prepare: the HW pass casts only the opaque (binary) shadow, so the SW colored pass is needed only when a
    // transparent occluder exists; opaque-only scenes skip the software BVH build entirely.
    rayTracingState().m_sceneHasTransparentOccluder = false;

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        MeshResources* mesh = nullptr;
        RenderableMeshDesc resolvedMesh;
        const bool meshReady = __hidden_raytracing_system::ResolveRenderableMeshResources(
            *meshSystem,
            m_renderer.meshSystem(),
            entity,
            resolvedMesh,
            mesh
        );
        // The BLAS owns the positions it traces, while the HW GI trace needs the index buffer (3 vertex indices by
        // PrimitiveIndex), the U2 triangle-corner attribute buffer, and the raw position buffer for geometric face
        // normals, so require all three.
        if(!meshReady || !mesh || !mesh->blas || !mesh->triangleIndexBuffer || !mesh->attributeBuffer || !mesh->positionBuffer)
            continue;

        // Dedupe to a per-mesh table slot: instances sharing a mesh share its index/attribute/position buffers. The
        // table grows on demand, so every distinct mesh is registered.
        Core::Buffer* meshIndexBuffer = mesh->triangleIndexBuffer.get();
        u32 meshSlot = ~0u; // sentinel: mesh not yet in this frame's distinct-mesh table
        for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot){
            if(rayTracingState().m_shadowMeshIndexBuffers[slot] == meshIndexBuffer){
                meshSlot = slot;
                break;
            }
        }
        if(meshSlot == ~0u){
            // New distinct mesh: append its three backing buffers to the per-frame table and mint the parallel
            // global-heap handles the HW caustic/GI traces read this geometry through. Every distinct mesh is always
            // registered (the table grows on demand).
            meshSlot = rayTracingState().m_shadowMeshCount++;
            rayTracingState().m_shadowMeshIndexBuffers.push_back(meshIndexBuffer);
            rayTracingState().m_shadowMeshAttributeBuffers.push_back(mesh->attributeBuffer.get());
            rayTracingState().m_shadowMeshPositionBuffers.push_back(mesh->positionBuffer.get());
            // Phase 2 M1: mirror the same three buffers into the global heap (raw SRV view; the HW caustic/GI traces
            // read them via NwbHeapRawBuffer(record.<x>Slot)). When the heap is unavailable the handles push default
            // (never read) so all six Vectors stay lockstep by slot.
            if(heapLive){
                rayTracingState().m_shadowMeshIndexHandles.push_back(RegisterMeshHeapBuffer(heap, Core::BindingSetItem::RawBuffer_SRV(0u, meshIndexBuffer)));
                rayTracingState().m_shadowMeshAttributeHandles.push_back(RegisterMeshHeapBuffer(heap, Core::BindingSetItem::RawBuffer_SRV(0u, mesh->attributeBuffer.get())));
                rayTracingState().m_shadowMeshPositionHandles.push_back(RegisterMeshHeapBuffer(heap, Core::BindingSetItem::RawBuffer_SRV(0u, mesh->positionBuffer.get())));
            }
            else{
                rayTracingState().m_shadowMeshIndexHandles.emplace_back();
                rayTracingState().m_shadowMeshAttributeHandles.emplace_back();
                rayTracingState().m_shadowMeshPositionHandles.emplace_back();
            }
        }

        Core::RayTracingInstanceDesc instanceDesc;
        instanceDesc.setBLAS(mesh->blas.get());
        instanceDesc.setInstanceID(static_cast<u32>(instances.size()));
        instanceDesc.setInstanceMask(0xFFu);

        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        if(transform){
            // Compose object->world (T * R(quat) * S) and store it as the instance's row-major 3x4 transform;
            // the engine's column-vector SIMDMatrix rows map directly onto AffineTransform (= Float34).
            const SIMDMatrix instanceWorld = __hidden_raytracing_system::BuildObjectToWorld(transform);
            StoreFloat(instanceWorld, &instanceDesc.transform);
        }

        // Resolve the occluder material (transmittance-model id + transparent flag) + the per-mesh attribute slot
        // + the material-constants context the trace surface dispatch reads. That context is packed into the
        // SHADOW-OWNED combined buffers (NOT the draw pass's, which hold only one transparency class at trace
        // time): appendShadowOccluderMaterialContext appends this occluder's constant block (its real byte offset
        // -> materialConstantByteOffset) + its per-instance mutable block (offset packed into the InstanceGpuData
        // pushed lockstep below). meshInstanceIndex == the combined-instance push index == InstanceID(). An
        // unresolved material falls back to a fully-opaque record (still a colorless shadow) + a default instance.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo)){
            if(materialInfo->transparent)
                rayTracingState().m_sceneHasTransparentOccluder = true;
            u32 materialConstantByteOffset = 0u;
            if(!m_renderer.materialSystem().appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                transform,
                shadowMaterialTypedBytes,
                shadowMutableTypedRanges,
                shadowInstance,
                materialConstantByteOffset
            ))
                return false;
            instanceMaterial = __hidden_raytracing_system::ResolveInstanceShadowMaterial(*materialInfo, meshSlot, materialConstantByteOffset, meshInstanceIndex);
        }
        // Phase 2 M2: carry this mesh's heap slots on the shared record, from the M1 handles minted above. Applies
        // to both the resolved and the fallback-opaque record (meshSlot is a valid registered slot on both paths).
        // Write-only this step -- the HW opaque trace reads no geometry; the HW caustic/GI traces (which read this
        // buffer by InstanceID) consume the slots when they are swept. nodeSlot stays s_Max: no SW node buffer here.
        if(heapLive){
            instanceMaterial.indexSlot = rayTracingState().m_shadowMeshIndexHandles[meshSlot].slot();
            instanceMaterial.attributeSlot = rayTracingState().m_shadowMeshAttributeHandles[meshSlot].slot();
            instanceMaterial.positionSlot = rayTracingState().m_shadowMeshPositionHandles[meshSlot].slot();
        }

        // Non-transparent occluders (including the unresolved-material fallback above, which casts a colorless
        // opaque shadow) are marked FORCE_OPAQUE. The hardware shadow RayQuery then lets the fixed-function
        // intersector commit the first opaque occluder and terminate (RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH)
        // with no per-candidate shader callback and no per-instance material load on the common opaque path.
        // Transparent occluders stay non-opaque so they still surface as candidates the hardware trace skips (the
        // software traversal casts their colored shadow). The flag is invisible to the GI and caustic traces:
        // both pass RAY_FLAG_FORCE_OPAQUE at the ray level and so already treat every instance as opaque
        // regardless of it -- so this force-opaque set (= the exact set the shadow trace treated as a solid
        // occluder before) changes only the shadow path.
        if(!(materialInfo && materialInfo->transparent))
            instanceDesc.setFlags(Core::RayTracingInstanceFlags::ForceOpaque);

        instances.push_back(instanceDesc);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    // Phase 2 M1: evidence for the registration gate - confirm handles were minted and track the peak, logging only
    // on a new high so a steady scene stays quiet. 3 handles per HW-shadow distinct mesh (index/attribute/position).
    if(heapLive && rayTracingState().m_shadowMeshCount > rayTracingState().m_shadowMeshHeapHighWater){
        rayTracingState().m_shadowMeshHeapHighWater = rayTracingState().m_shadowMeshCount;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: Phase 2 M1 HW-shadow heap registration high-water: {} distinct meshes -> {} handles")
            , static_cast<u64>(rayTracingState().m_shadowMeshCount)
            , static_cast<u64>(rayTracingState().m_shadowMeshCount) * 3u
        );
    }

    rayTracingState().m_tlasInstanceCount = static_cast<u32>(instances.size());
    if(instances.empty()){
        rayTracingState().m_tlasDeviceAddress = 0u;
        return false;
    }

    if(!rayTracingState().m_tlas || rayTracingState().m_tlasMaxInstances < instances.size()){
        const usize capacity = ::NextGrowingCapacity(
            rayTracingState().m_tlasMaxInstances,
            instances.size(),
            s_TlasInitialInstanceCapacity
        );

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

    // Upload the per-instance occluder material table (indexed by InstanceID()) for the hardware trace paths.
    if(!ensureShadowInstanceMaterialBuffer(instances.size()))
        return false;
    Core::Buffer* materialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    commandList.setBufferState(materialBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(materialBuffer, instanceMaterials.data(), instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu));
    commandList.setBufferState(materialBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // Upload the shadow-owned combined material-constants context the trace surface dispatch reads. A word of
    // padding keeps the typed buffer valid when no occluder contributed any typed bytes.
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    if(!uploadShadowMaterialContextBuffers(commandList, shadowInstanceData, shadowMaterialTypedBytes))
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::buildSceneSwBvh(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    // Software scene/instance BVH (TLAS-analog) over ALL gathered occluders, plus the shadow-owned material context.
    // Built by the no-RayQuery fallback prepare (the only shadow backend) AND, on RT hardware, by the hybrid prepare
    // when the scene has a transparent occluder. The gather order matches buildSceneTlas's (same RendererComponent
    // view, aligned conditions), so the scene-BVH leaf index equals the hardware InstanceID -- and the material context
    // it rebuilds is byte-identical to buildSceneTlas's, so the HW caustic (which reads that context by InstanceID) is
    // unaffected even though both write it. The caller gates WHEN this runs.
    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    // Per-instance GPU records + the world-space AABBs / centroids the CPU build consumes (kept parallel so
    // the BVH leaf payload indexes straight into the uploaded instance buffer).
    Vector<SceneSwBvhInstanceGpu, Core::Alloc::ScratchArena> instances{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMin{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMax{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceCentroid{ scratchArena };
    // Per-instance occluder material, built lockstep with `instances` (same push order) so the uploaded table
    // indexes by the scene-BVH leaf instance index the software traversal reads.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // Shadow-OWNED combined material-constants context, built lockstep with `instances` (see buildSceneTlas): the
    // per-occluder InstanceGpuData (g_NwbMeshInstances for the trace) + the combined typed bytes
    // (g_NwbMaterialTypedWords for the trace). The draw pass's buffers hold only one transparency class at trace
    // time, so the software traversal binds these instead -- see ensureSwShadowBindingSet.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        scratchArena
    );
    instances.reserve(candidateCount);
    instanceAabbMin.reserve(candidateCount);
    instanceAabbMax.reserve(candidateCount);
    instanceCentroid.reserve(candidateCount);
    instanceMaterials.reserve(candidateCount);
    shadowInstanceData.reserve(candidateCount);
    shadowMutableTypedRanges.reserve(candidateCount);

    // Reset the per-frame distinct-mesh table; the gather repopulates it (slot k -> mesh k's buffers) for the
    // per-mesh descriptor arrays the traversal binds.
    // Phase 2 M1: additively mirror the same distinct meshes into the global descriptor heap; retire last frame's
    // handles before the per-frame rebuild (the deferred-free quarantine keeps in-flight frames valid). Nothing
    // consumes the handles yet, so this cannot change the render. See buildSceneTlas for the full rationale.
    Core::GpuDescriptorHeap& heap = graphics().getDevice()->getDescriptorHeap();
    const bool heapLive = heap.isInitialized();
    if(heapLive){
        auto& rt = rayTracingState();
        for(u32 slot = 0u; slot < rt.m_swShadowMeshCount; ++slot){
            ReleaseMeshHeapHandle(heap, rt.m_swShadowMeshNodeHandles[slot]);
            ReleaseMeshHeapHandle(heap, rt.m_swShadowMeshPositionHandles[slot]);
            ReleaseMeshHeapHandle(heap, rt.m_swShadowMeshIndexHandles[slot]);
            ReleaseMeshHeapHandle(heap, rt.m_swShadowMeshAttributeHandles[slot]);
        }
    }
    // Clear the distinct-mesh table for this frame's rebuild. The Vectors retain capacity (they grow once to the
    // scene's steady-state distinct-mesh count, then reuse that storage) and m_swShadowMeshCount mirrors their length
    // -- the gather below repopulates both by push_back.
    rayTracingState().m_swShadowMeshNodeBuffers.clear();
    rayTracingState().m_swShadowMeshPositionBuffers.clear();
    rayTracingState().m_swShadowMeshIndexBuffers.clear();
    rayTracingState().m_swShadowMeshAttributeBuffers.clear();
    rayTracingState().m_swShadowMeshNodeHandles.clear();
    rayTracingState().m_swShadowMeshPositionHandles.clear();
    rayTracingState().m_swShadowMeshIndexHandles.clear();
    rayTracingState().m_swShadowMeshAttributeHandles.clear();
    rayTracingState().m_swShadowMeshCount = 0u;

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        MeshResources* mesh = nullptr;
        RenderableMeshDesc resolvedMesh;
        const bool meshReady = __hidden_raytracing_system::ResolveRenderableMeshResources(
            *meshSystem,
            m_renderer.meshSystem(),
            entity,
            resolvedMesh,
            mesh
        );
        // Only instances whose per-mesh software BVH topology + geometry are built (and that have valid
        // object-space bounds) can be traced. Storage alone is insufficient because resource preparation may
        // allocate it before the first build command has initialized the topology.
        if(!meshReady || !mesh || !mesh->swBvhTopologyBuilt || !mesh->swBvhNodeBuffer || !mesh->positionBuffer || !mesh->triangleIndexBuffer || !mesh->csgLocalBounds.valid())
            continue;

        // Dedupe to a per-mesh table slot: instances sharing a mesh share its node/position/index buffers. The table
        // grows on demand, so every distinct mesh is always traced.
        Core::Buffer* meshNodeBuffer = mesh->swBvhNodeBuffer.get();
        u32 meshSlot = ~0u; // sentinel: mesh not yet in this frame's distinct-mesh table
        for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
            if(rayTracingState().m_swShadowMeshNodeBuffers[slot] == meshNodeBuffer){
                meshSlot = slot;
                break;
            }
        }
        if(meshSlot == ~0u){
            // New distinct mesh: append its four backing buffers to the per-frame table and mint the parallel
            // global-heap handles the SW shadow / caustic / GI traces read this geometry through.
            meshSlot = rayTracingState().m_swShadowMeshCount++;
            rayTracingState().m_swShadowMeshNodeBuffers.push_back(meshNodeBuffer);
            rayTracingState().m_swShadowMeshPositionBuffers.push_back(mesh->positionBuffer.get());
            rayTracingState().m_swShadowMeshIndexBuffers.push_back(mesh->triangleIndexBuffer.get());
            // The U2 per-triangle-corner shadow-trace attribute buffer (normal/uv0), parallel to the triangle index
            // buffer so the trace interpolates the exact raster corner attributes.
            rayTracingState().m_swShadowMeshAttributeBuffers.push_back(mesh->attributeBuffer.get());
            // Phase 2 M1: mirror the four buffers into the global heap. The node buffer takes the
            // StructuredBuffer<NwbBvhNode> view (M3 reads it via NwbHeapBvhNodes(record.nodeSlot)); position/index/
            // attribute take the raw view. All land as STORAGE_BUFFER regardless (write() forces the type). When the
            // heap is unavailable the handles push default (never read) so all eight Vectors stay lockstep by slot.
            if(heapLive){
                rayTracingState().m_swShadowMeshNodeHandles.push_back(RegisterMeshHeapBuffer(heap, Core::BindingSetItem::StructuredBuffer_SRV(0u, meshNodeBuffer)));
                rayTracingState().m_swShadowMeshPositionHandles.push_back(RegisterMeshHeapBuffer(heap, Core::BindingSetItem::RawBuffer_SRV(0u, mesh->positionBuffer.get())));
                rayTracingState().m_swShadowMeshIndexHandles.push_back(RegisterMeshHeapBuffer(heap, Core::BindingSetItem::RawBuffer_SRV(0u, mesh->triangleIndexBuffer.get())));
                rayTracingState().m_swShadowMeshAttributeHandles.push_back(RegisterMeshHeapBuffer(heap, Core::BindingSetItem::RawBuffer_SRV(0u, mesh->attributeBuffer.get())));
            }
            else{
                rayTracingState().m_swShadowMeshNodeHandles.emplace_back();
                rayTracingState().m_swShadowMeshPositionHandles.emplace_back();
                rayTracingState().m_swShadowMeshIndexHandles.emplace_back();
                rayTracingState().m_swShadowMeshAttributeHandles.emplace_back();
            }
        }

        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        const SIMDMatrix objectToWorld = __hidden_raytracing_system::BuildObjectToWorld(transform);
        SIMDVector determinant;
        const SIMDMatrix worldToObject = MatrixInverse(&determinant, objectToWorld);

        // Shared with caustic-target gathering: exact world bounds of all eight object-space corners.
        const SIMDVector localMin = LoadFloatInt(mesh->csgLocalBounds.minBounds);
        const SIMDVector localMax = LoadFloatInt(mesh->csgLocalBounds.maxBounds);
        SIMDVector worldMin{};
        SIMDVector worldMax{};
        if(!AabbTests::Transform(objectToWorld, localMin, localMax, worldMin, worldMax))
            continue;
        __hidden_raytracing_system::InflateSwShadowSceneBounds(worldMin, worldMax);

        SceneSwBvhInstanceGpu instance;
        StoreFloat(worldToObject, &instance.worldToObject);
        instance.meshIndex = meshSlot;
        instance.primitiveCount = mesh->meshletPrimitiveIndexCount / 3u;

        // Resolve the occluder material (transmittance-model id + transparent flag) + the material-constants
        // context the surface hook reads in the trace. That context is packed into the SHADOW-OWNED combined
        // buffers (NOT the draw pass's, which hold only one transparency class at trace time):
        // appendShadowOccluderMaterialContext appends this occluder's constant block (real byte offset ->
        // materialConstantByteOffset) + its per-instance mutable block (offset packed into the InstanceGpuData
        // pushed lockstep below). meshInstanceIndex == the combined-instance push index == the scene-BVH leaf
        // index the traversal reads. An unresolved material falls back to a fully-opaque record (still a colorless
        // shadow) + a default instance.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo)){
            u32 materialConstantByteOffset = 0u;
            if(!m_renderer.materialSystem().appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                transform,
                shadowMaterialTypedBytes,
                shadowMutableTypedRanges,
                shadowInstance,
                materialConstantByteOffset
            ))
                return false;
            instanceMaterial = __hidden_raytracing_system::ResolveInstanceShadowMaterial(*materialInfo, meshSlot, materialConstantByteOffset, meshInstanceIndex);
        }
        // Phase 2 M2: carry this mesh's four heap slots on the shared record, from the SW M1 handles minted above.
        // Applies to both the resolved and the fallback record (meshSlot is a valid registered SW slot on both).
        // Write-only this step; the software shadow traversal -- the first swept consumer -- reads them next.
        if(heapLive){
            instanceMaterial.indexSlot = rayTracingState().m_swShadowMeshIndexHandles[meshSlot].slot();
            instanceMaterial.attributeSlot = rayTracingState().m_swShadowMeshAttributeHandles[meshSlot].slot();
            instanceMaterial.positionSlot = rayTracingState().m_swShadowMeshPositionHandles[meshSlot].slot();
            instanceMaterial.nodeSlot = rayTracingState().m_swShadowMeshNodeHandles[meshSlot].slot();
        }
        Float4 storedWorldMin;
        Float4 storedWorldMax;
        Float4 storedCentroid;
        StoreFloat(worldMin, &storedWorldMin);
        StoreFloat(worldMax, &storedWorldMax);
        StoreFloat(VectorScale(VectorAdd(worldMin, worldMax), 0.5f), &storedCentroid);

        instances.push_back(instance);
        instanceAabbMin.push_back(storedWorldMin);
        instanceAabbMax.push_back(storedWorldMax);
        instanceCentroid.push_back(storedCentroid);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    // Phase 2 M1: registration-gate evidence for the SW distinct-mesh set (see buildSceneTlas), logged only on a new
    // high. 4 handles per SW distinct mesh (node/position/index/attribute).
    if(heapLive && rayTracingState().m_swShadowMeshCount > rayTracingState().m_swShadowMeshHeapHighWater){
        rayTracingState().m_swShadowMeshHeapHighWater = rayTracingState().m_swShadowMeshCount;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: Phase 2 M1 SW-shadow heap registration high-water: {} distinct meshes -> {} handles")
            , static_cast<u64>(rayTracingState().m_swShadowMeshCount)
            , static_cast<u64>(rayTracingState().m_swShadowMeshCount) * 4u
        );
    }

    const u32 instanceCount = static_cast<u32>(instances.size());
    if(instanceCount == 0u){
        // No traceable instances is not a failure: the consumer treats a zero instance count as
        // "nothing occludes" (fully lit), so report success and leave the resident buffers untouched.
        rayTracingState().m_sceneBvhInstanceCount = 0u;
        return true;
    }

    // CPU-build the scene BVH over the gathered instance world AABBs. At TLAS scale (a handful to a few
    // hundred instances) a CPU build + upload is far cheaper than a GPU LBVH dispatch, and the node layout
    // matches the per-mesh BVH so the traversal pass reads scene and mesh BVHs the same way.
    Vector<u32, Core::Alloc::ScratchArena> indices{ scratchArena };
    indices.reserve(instanceCount);
    for(u32 i = 0u; i < instanceCount; ++i)
        indices.push_back(i);

    const usize nodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    Vector<SceneBvhNodeBuildData, Core::Alloc::ScratchArena> buildNodes{ scratchArena };
    buildNodes.reserve(nodeCount);
    // Per-instance leaf cost = primitive count, so a large instance biases the SAH tree like a large primitive.
    Vector<u32, Core::Alloc::ScratchArena> instanceLeafCost{ scratchArena };
    instanceLeafCost.reserve(instanceCount);
    for(u32 i = 0u; i < instanceCount; ++i)
        instanceLeafCost.push_back(instances[i].primitiveCount);
    __hidden_raytracing_system::BuildSceneBvhNode(indices.data(), 0u, instanceCount, instanceAabbMin.data(), instanceAabbMax.data(), instanceCentroid.data(), buildNodes, instanceLeafCost.data());
    NWB_ASSERT(buildNodes.size() == nodeCount);

    Vector<NwbBvhNodeGpu, Core::Alloc::ScratchArena> nodes{ scratchArena };
    nodes.reserve(buildNodes.size());
    for(const SceneBvhNodeBuildData& buildNode : buildNodes){
        NwbBvhNodeGpu node;
        StoreFloatInt(LoadFloat(buildNode.aabbMin), buildNode.leftChild, &node.aabbMinLeftChild);
        StoreFloatInt(LoadFloat(buildNode.aabbMax), buildNode.rightChild, &node.aabbMaxRightChild);
        nodes.push_back(node);
    }

    if(!ensureSceneBvhBuffers(instanceCount))
        return false;
    if(!ensureShadowInstanceMaterialBuffer(instances.size()))
        return false;

    Core::Buffer* nodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
    Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();
    Core::Buffer* materialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();

    commandList.setBufferState(nodeBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(materialBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(nodeBuffer, nodes.data(), nodes.size() * sizeof(NwbBvhNodeGpu));
    commandList.writeBuffer(instanceBuffer, instances.data(), instances.size() * sizeof(SceneSwBvhInstanceGpu));
    commandList.writeBuffer(materialBuffer, instanceMaterials.data(), instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu));
    commandList.setBufferState(nodeBuffer, Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);
    commandList.setBufferState(materialBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // Upload the shadow-owned combined material-constants context the software traversal's per-hit transmittance
    // dispatch reads. A word of padding keeps the typed buffer valid when no occluder contributed any typed bytes.
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    if(!uploadShadowMaterialContextBuffers(commandList, shadowInstanceData, shadowMaterialTypedBytes))
        return false;

    rayTracingState().m_sceneBvhInstanceCount = instanceCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    // Keep the shadow geometry non-opaque so the RayQuery path can inspect each candidate's material flags.
    Core::RayTracingGeometryDesc geometry;
    geometry
        .setTriangles(triangles)
        .setFlags(Core::RayTracingGeometryFlags::NoDuplicateAnyHitInvocation)
    ;

    // Runtime (skinned) meshes keep a single resident BLAS built with AllowUpdate and refit it in
    // place from the freshly skinned positions each frame, forcing a full rebuild once the adaptive
    // refit budget (scales ~cube-root of triangle count) is exhausted to restore BVH quality. Static
    // meshes build once.
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
        && meshResources.blasRefitsSinceRebuild < adaptiveRefitsBeforeRebuild(meshResources.meshletPrimitiveIndexCount / 3u)
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureBvhSortBuffers(usize paddedCount){
    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhSortKeysBuffer || rayTracingState().m_bvhSortCapacity < paddedCount){
        const usize capacity = ::NextGrowingCapacity(
            rayTracingState().m_bvhSortCapacity,
            paddedCount,
            s_BvhSortInitialCapacity
        );

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    // Each step reads and writes the same buffers, so consecutive steps must be serialized with UAV barriers:
    // enable per-buffer UAV barriers, then commit one per step.
    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);

    const u32 groupCount = paddedCount / static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);

    const auto dispatchSort = [this, &commandList](const BvhSortPushConstants& pushConstants, const u32 groups){
        Core::ComputeState computeState;
        computeState.setPipeline(rayTracingState().m_bvhSortPipeline.get());
        computeState.addBindingSet(rayTracingState().m_bvhSortBindingSet.get());
        commandList.setComputeState(computeState);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groups, 1u, 1u);
    };
    const auto bvhSortBarrier = [&commandList, keysBuffer, payloadBuffer](){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    };

    // Phase A — LOCAL_TILE: one dispatch fully sorts every GROUP_SIZE tile in groupshared, replacing the
    // sequenceSize 2..GROUP_SIZE portion of the merge loop (which would otherwise be one dispatch per
    // (sequenceSize, compareDistance) sub-step). Produces a sorted (hence bitonic) run per tile.
    {
        BvhSortPushConstants pushConstants;
        pushConstants.elementCount = elementCount;
        pushConstants.mode = 0u;        // LOCAL_TILE
        dispatchSort(pushConstants, groupCount);
        bvhSortBarrier();
    }

    // Phase B — GLOBAL merge: the standard bitonic merging steps for sequenceSize > GROUP_SIZE. Each step is
    // split by compareDistance relative to the tile stride GROUP_SIZE:
    //   - compareDistance >= GROUP_SIZE : the XOR partner lands in a DIFFERENT tile, so these stay plain
    //     global-memory swaps — one dispatch each (GLOBAL).
    //   - compareDistance <  GROUP_SIZE : the XOR partner stays in the SAME tile, so the 8 steps from
    //     GROUP_SIZE/2 down to 1 run in one groupshared dispatch per sequenceSize (GLOBAL_TAIL), collapsing
    //     what would otherwise be 8 dispatches + 8 UAV barriers per sequenceSize into one.
    for(u32 sequenceSize = static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE) << 1u; sequenceSize <= paddedCount; sequenceSize <<= 1u){
        for(u32 compareDistance = sequenceSize >> 1u; compareDistance >= static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE); compareDistance >>= 1u){
            BvhSortPushConstants pushConstants;
            pushConstants.elementCount = elementCount;
            pushConstants.compareDistance = compareDistance;
            pushConstants.sequenceSize = sequenceSize;
            pushConstants.mode = 1u;    // GLOBAL
            dispatchSort(pushConstants, groupCount);
            bvhSortBarrier();
        }

        BvhSortPushConstants tailPushConstants;
        tailPushConstants.elementCount = elementCount;
        tailPushConstants.sequenceSize = sequenceSize;
        tailPushConstants.mode = 2u;    // GLOBAL_TAIL
        dispatchSort(tailPushConstants, groupCount);
        bvhSortBarrier();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureBvhBuildPipeline(){
    if(
        rayTracingState().m_bvhMortonPipeline
        && rayTracingState().m_bvhTopologyPipeline
        && rayTracingState().m_bvhFitPipeline
    )
        return true;
    if(rayTracingState().m_bvhBuildPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_POSITIONS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_TRIANGLE_INDICES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_KEYS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PAYLOAD, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_NODES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PARENT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_VISIT_COUNTER, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhBuildPushConstants)));

        rayTracingState().m_bvhBuildBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_bvhBuildBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build binding layout"));
            rayTracingState().m_bvhBuildPipelineFailed = true;
            return false;
        }
    }

    const auto createBuildPipeline = [this, device](
        Core::ShaderHandle& shader,
        Core::ComputePipelineHandle& pipeline,
        const Name& shaderName,
        const char* debugLabel
    )->bool{
        if(pipeline)
            return true;
        if(!m_renderer.shaderSystem().loadShader(shader, shaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Compute, debugLabel))
            return false;

        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(shader)
            .addBindingLayout(rayTracingState().m_bvhBuildBindingLayout)
        ;
        pipeline = device->createComputePipeline(pipelineDesc);
        return pipeline != nullptr;
    };

    if(
        !createBuildPipeline(rayTracingState().m_bvhMortonShader, rayTracingState().m_bvhMortonPipeline, AssetsGraphicsBvh::s_BvhMortonShaderName, "ECSRender_BvhMorton")
        || !createBuildPipeline(rayTracingState().m_bvhTopologyShader, rayTracingState().m_bvhTopologyPipeline, AssetsGraphicsBvh::s_BvhTopologyShaderName, "ECSRender_BvhTopology")
        || !createBuildPipeline(rayTracingState().m_bvhFitShader, rayTracingState().m_bvhFitPipeline, AssetsGraphicsBvh::s_BvhFitShaderName, "ECSRender_BvhFit")
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build compute pipeline"));
        rayTracingState().m_bvhBuildPipelineFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureBvhVisitCounterBuffer(usize primitiveCount){
    if(rayTracingState().m_bvhVisitCounterBuffer && rayTracingState().m_bvhBuildCapacity >= primitiveCount)
        return true;

    const usize capacity = ::NextGrowingCapacity(
        rayTracingState().m_bvhBuildCapacity,
        primitiveCount,
        s_BvhBuildInitialCapacity
    );

    // The fit's bottom-up rendezvous counter is SHARED scratch (one u32 per internal node, < N). Meshes are
    // built sequentially within a frame and serialized by barriers, so a single shared counter suffices.
    Core::BufferDesc counterBufferDesc;
    counterBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_visit_counter"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_bvhVisitCounterBuffer = graphics().createBuffer(counterBufferDesc);
    if(!rayTracingState().m_bvhVisitCounterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH visit counter buffer"));
        return false;
    }
    rayTracingState().m_bvhBuildCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer){
    if(nodeBuffer && parentBuffer)
        return true;

    // A binary LBVH over N primitives has exactly 2N-1 nodes (internal [0,N-1) + leaves [N-1,2N-1)). These
    // are PER-MESH and persist across frames (refit reuses the topology), so they are sized exactly to N.
    const usize nodeCount = primitiveCount * 2u - 1u;

    Core::BufferDesc nodeBufferDesc;
    nodeBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount))
        .setStructStride(sizeof(NwbBvhNodeGpu))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_mesh_nodes"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    nodeBuffer = graphics().createBuffer(nodeBufferDesc);
    if(!nodeBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH node buffer"));
        return false;
    }

    Core::BufferDesc parentBufferDesc;
    parentBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * nodeCount))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_mesh_parent"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    parentBuffer = graphics().createBuffer(parentBufferDesc);
    if(!parentBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH parent buffer"));
        nodeBuffer.reset();
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureMeshBvhBindingSet(
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    Core::Buffer* nodeBuffer,
    Core::Buffer* parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(bindingSet)
        return true;

    NWB_ASSERT(rayTracingState().m_bvhBuildBindingLayout);
    NWB_ASSERT(rayTracingState().m_bvhSortKeysBuffer);
    NWB_ASSERT(rayTracingState().m_bvhSortPayloadBuffer);
    NWB_ASSERT(rayTracingState().m_bvhVisitCounterBuffer);
    NWB_ASSERT(positionBuffer);
    NWB_ASSERT(triangleIndexBuffer);
    NWB_ASSERT(nodeBuffer);
    NWB_ASSERT(parentBuffer);

    // The set binds this mesh's per-mesh nodes/parent + the shared sort keys/payload + the shared visit
    // counter + this mesh's raw position/index buffers.
    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_POSITIONS, positionBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_TRIANGLE_INDICES, triangleIndexBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_KEYS, rayTracingState().m_bvhSortKeysBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PAYLOAD, rayTracingState().m_bvhSortPayloadBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_NODES, nodeBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PARENT, parentBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_VISIT_COUNTER, rayTracingState().m_bvhVisitCounterBuffer.get()));

    bindingSet = graphics().getDevice()->createBindingSet(bindingSetDesc, rayTracingState().m_bvhBuildBindingLayout);
    if(!bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH build binding set"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureMeshSwBvhResources(
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount > s_BvhMaxPrimitivesPerMesh){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: mesh exceeds software BVH primitive cap ({} > {}), shadows skipped")
            , static_cast<u64>(primitiveCount)
            , static_cast<u64>(s_BvhMaxPrimitivesPerMesh)
        );
        return false;
    }

    if(!ensureBvhSortPipeline())
        return false;
    if(!ensureBvhBuildPipeline())
        return false;
    // Size the shared sort/counter scratch ONCE to the per-mesh cap (a power of two, so it is itself a valid
    // padded sort length). This keeps the shared buffers stable across builds of different-sized meshes, so
    // the per-mesh binding sets that reference them stay valid; the per-mesh node/parent are sized exactly.
    if(!ensureBvhSortBuffers(s_BvhMaxPrimitivesPerMesh))
        return false;
    if(!ensureBvhVisitCounterBuffer(s_BvhMaxPrimitivesPerMesh))
        return false;
    if(!createMeshBvhStorage(primitiveCount, nodeBuffer, parentBuffer))
        return false;
    return ensureMeshBvhBindingSet(positionBuffer, triangleIndexBuffer, nodeBuffer.get(), parentBuffer.get(), bindingSet);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::meshSwBvhResourcesReady(
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    const Core::BufferHandle& nodeBuffer,
    const Core::BufferHandle& parentBuffer,
    const Core::BindingSetHandle& bindingSet
){
    return
        positionBuffer
        && triangleIndexBuffer
        && nodeBuffer
        && parentBuffer
        && bindingSet
        && rayTracingState().m_bvhSortPipeline
        && rayTracingState().m_bvhSortBindingSet
        && rayTracingState().m_bvhSortKeysBuffer
        && rayTracingState().m_bvhSortPayloadBuffer
        && rayTracingState().m_bvhMortonPipeline
        && rayTracingState().m_bvhTopologyPipeline
        && rayTracingState().m_bvhFitPipeline
        && rayTracingState().m_bvhVisitCounterBuffer
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::buildMeshSwBvh(
    Core::CommandList& commandList,
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    const SIMDVector aabbMin,
    const SIMDVector aabbMax,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount == 0u)
        return false;
    if(!ensureMeshSwBvhResources(positionBuffer, triangleIndexBuffer, primitiveCount, nodeBuffer, parentBuffer, bindingSet))
        return false;

    return buildMeshSwBvhPrepared(
        commandList,
        positionBuffer,
        triangleIndexBuffer,
        primitiveCount,
        aabbMin,
        aabbMax,
        nodeBuffer,
        parentBuffer,
        bindingSet
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::buildMeshSwBvhPrepared(
    Core::CommandList& commandList,
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    const SIMDVector aabbMin,
    const SIMDVector aabbMax,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount == 0u || !meshSwBvhResourcesReady(positionBuffer, triangleIndexBuffer, nodeBuffer, parentBuffer, bindingSet))
        return false;

    u32 paddedCount = static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);
    while(paddedCount < primitiveCount)
        paddedCount <<= 1u;

    Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = rayTracingState().m_bvhSortPayloadBuffer.get();
    Core::Buffer* visitCounterBuffer = rayTracingState().m_bvhVisitCounterBuffer.get();
    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();
    Core::BindingSet* meshBindingSet = bindingSet.get();

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.aabbMin = Float4(VectorGetX(aabbMin), VectorGetY(aabbMin), VectorGetZ(aabbMin), 0.0f);
    pushConstants.aabbMax = Float4(VectorGetX(aabbMax), VectorGetY(aabbMax), VectorGetZ(aabbMax), 0.0f);

    // Initialize: sort-key padding to a sentinel that sorts last, parent links to "no parent", and the
    // per-internal-node visit counters to zero (the fit's second-arrival rendezvous).
    commandList.setBufferState(keysBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(meshParentBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearBufferUInt(keysBuffer, BvhNodeIndex::Invalid);
    commandList.clearBufferUInt(meshParentBuffer, BvhNodeIndex::Invalid);
    commandList.clearBufferUInt(visitCounterBuffer, 0u);

    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer, true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer, true);

    const auto bvhBuildBarrier = [&commandList, keysBuffer, payloadBuffer, meshNodeBuffer, meshParentBuffer, visitCounterBuffer](){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshParentBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    };

    const auto dispatchBuildKernel = [&commandList, &pushConstants, meshBindingSet](Core::ComputePipeline* pipeline, const u32 groupCount){
        Core::ComputeState computeState;
        computeState.setPipeline(pipeline);
        computeState.addBindingSet(meshBindingSet);
        commandList.setComputeState(computeState);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groupCount, 1u, 1u);
    };

    bvhBuildBarrier();

    dispatchBuildKernel(rayTracingState().m_bvhMortonPipeline.get(), DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();

    // The bitonic sort is the rebuild step whose intra-tile collapse (c996a917) is the perf change under
    // measurement; give it its own scope so its cost is separable from the surrounding morton/topology/fit work.
    // Per-mesh (buildMeshSwBvh runs once per rebuild), so all sort dispatches in a frame accumulate into one
    // render.sw_bvh_sort average. Scoped here, not in bvhBitonicSort(), so the one-shot self-test sort stays out.
    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_SwBvhSort, graphics().getDevice(), commandList);
        if(!bvhBitonicSort(commandList, primitiveCount, paddedCount))
            return false;
    }
    bvhBuildBarrier();

    if(primitiveCount > 1u){
        dispatchBuildKernel(rayTracingState().m_bvhTopologyPipeline.get(), DivideUp(primitiveCount - 1u, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
        bvhBuildBarrier();
    }

    dispatchBuildKernel(rayTracingState().m_bvhFitPipeline.get(), DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::refitMeshSwBvh(
    Core::CommandList& commandList,
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount == 0u)
        return false;
    if(!ensureMeshSwBvhResources(positionBuffer, triangleIndexBuffer, primitiveCount, nodeBuffer, parentBuffer, bindingSet))
        return false;

    return refitMeshSwBvhPrepared(
        commandList,
        positionBuffer,
        triangleIndexBuffer,
        primitiveCount,
        nodeBuffer,
        parentBuffer,
        bindingSet
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::refitMeshSwBvhPrepared(
    Core::CommandList& commandList,
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount == 0u || !meshSwBvhResourcesReady(positionBuffer, triangleIndexBuffer, nodeBuffer, parentBuffer, bindingSet))
        return false;

    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();
    Core::Buffer* visitCounterBuffer = rayTracingState().m_bvhVisitCounterBuffer.get();

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.refitMode = 1u;

    // A refit reuses the sorted topology, child links, and per-leaf primitive from the last full build, so
    // only the rendezvous counters reset; the fit pass recomputes every box from the current positions.
    commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearBufferUInt(visitCounterBuffer, 0u);

    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer, true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer, true);
    commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(meshParentBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(rayTracingState().m_bvhFitPipeline.get());
    computeState.addBindingSet(bindingSet.get());
    commandList.setComputeState(computeState);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)), 1u, 1u);

    commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::updateMeshSwBvh(Core::CommandList& commandList, MeshResources& meshResources){
    if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer)
        return false;
    // meshletPrimitiveIndexCount is the reconstructed triangle-index count (3 per triangle).
    if(meshResources.meshletPrimitiveIndexCount == 0u || (meshResources.meshletPrimitiveIndexCount % 3u) != 0u)
        return false;
    const u32 primitiveCount = meshResources.meshletPrimitiveIndexCount / 3u;
    if(!meshSwBvhResourcesReady(
        meshResources.positionBuffer.get(),
        meshResources.triangleIndexBuffer.get(),
        meshResources.swBvhNodeBuffer,
        meshResources.swBvhParentBuffer,
        meshResources.swBvhBindingSet
    ))
        return false;

    // The morton / topology / fit kernels read positions and triangle indices as raw byte buffers, so move
    // both to ShaderResource before the build/refit dispatches bind them.
    commandList.setBufferState(meshResources.positionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(meshResources.triangleIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // Skinned (runtime) meshes deform every frame: refit the build-pose topology in place from the freshly
    // skinned positions, forcing a full rebuild once the adaptive refit budget (scales ~cube-root of triangle
    // count) is exhausted to restore tree quality. Static meshes build once. A mesh's first appearance is
    // always a full build. Resource preparation can allocate node storage before recording begins, so topology
    // initialization is tracked independently from buffer existence.
    const bool firstBuild = !meshResources.swBvhTopologyBuilt;
    const bool performRefit =
        meshResources.runtimeMesh
        && !firstBuild
        && meshResources.swBvhRefitsSinceRebuild < adaptiveRefitsBeforeRebuild(primitiveCount)
    ;

    bool built = false;
    if(performRefit){
        built = refitMeshSwBvhPrepared(
            commandList,
            meshResources.positionBuffer.get(),
            meshResources.triangleIndexBuffer.get(),
            primitiveCount,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhBindingSet
        );
    }
    else{
        const SIMDVector aabbMin = LoadFloatInt(meshResources.csgLocalBounds.minBounds);
        const SIMDVector aabbMax = LoadFloatInt(meshResources.csgLocalBounds.maxBounds);
        built = buildMeshSwBvhPrepared(
            commandList,
            meshResources.positionBuffer.get(),
            meshResources.triangleIndexBuffer.get(),
            primitiveCount,
            aabbMin,
            aabbMax,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhBindingSet
        );
    }
    if(!built)
        return false;

    if(!performRefit)
        meshResources.swBvhTopologyBuilt = true;

    if(firstBuild){
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built software BVH for mesh '{}' (runtime {}, {} triangles)")
            , StringConvert(meshResources.meshName.c_str())
            , meshResources.runtimeMesh
            , static_cast<u64>(primitiveCount)
        );
    }

    meshResources.swBvhRefitsSinceRebuild = performRefit ? (meshResources.swBvhRefitsSinceRebuild + 1u) : 0u;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSceneBvhBuffers(u32 instanceCount){
    // A binary BVH over N instances has 2N-1 nodes. Both buffers are CPU-written each frame and read by the
    // traversal pass, so they are structured SRVs (no UAV) that grow by doubling like the hardware TLAS.
    const usize requiredNodes = static_cast<usize>(instanceCount) * 2u - 1u;
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhNodeCapacity < requiredNodes){
        const usize capacity = ::NextGrowingCapacity(
            rayTracingState().m_sceneBvhNodeCapacity,
            requiredNodes,
            s_SceneBvhInitialInstanceCapacity * 2u - 1u
        );

        Core::BufferDesc nodeBufferDesc;
        nodeBufferDesc
            .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * capacity))
            .setStructStride(sizeof(NwbBvhNodeGpu))
            .setDebugName(Name("scene_bvh_nodes"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_sceneBvhNodeBuffer = graphics().createBuffer(nodeBufferDesc);
        if(!rayTracingState().m_sceneBvhNodeBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH node buffer"));
            return false;
        }
        rayTracingState().m_sceneBvhNodeCapacity = capacity;
    }

    if(!rayTracingState().m_sceneInstanceBuffer || rayTracingState().m_sceneInstanceCapacity < instanceCount){
        const usize capacity = ::NextGrowingCapacity(
            rayTracingState().m_sceneInstanceCapacity,
            instanceCount,
            s_SceneBvhInitialInstanceCapacity
        );

        Core::BufferDesc instanceBufferDesc;
        instanceBufferDesc
            .setByteSize(static_cast<u64>(sizeof(SceneSwBvhInstanceGpu) * capacity))
            .setStructStride(sizeof(SceneSwBvhInstanceGpu))
            .setDebugName(Name("scene_bvh_instances"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_sceneInstanceBuffer = graphics().createBuffer(instanceBufferDesc);
        if(!rayTracingState().m_sceneInstanceBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH instance buffer"));
            return false;
        }
        rayTracingState().m_sceneInstanceCapacity = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created software scene BVH buffers (capacity {} instances)")
            , static_cast<u64>(capacity)
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

