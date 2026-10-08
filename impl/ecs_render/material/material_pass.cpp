// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/optics/coincident_volumes.h>

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_scene/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pass{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_MeshletConeCullUniformScaleEpsilon = 0.0001f;

[[nodiscard]] static bool MeshletConeCullScaleSafe(const SIMDVector scale)noexcept{
    if(!VectorIsFinite(scale, VectorComponentMask::s_XYZ) || !Vector3Greater(scale, VectorZero()))
        return false;

    const SIMDVector minScale = Vector3MinComponent(scale);
    const SIMDVector maxScale = Vector3MaxComponent(scale);
    const SIMDVector tolerance = VectorScale(VectorMax(maxScale, s_SIMDOne), s_MeshletConeCullUniformScaleEpsilon);
    return Vector3LessOrEqual(VectorSubtract(maxScale, minScale), tolerance);
}

[[nodiscard]] static MaterialPassMeshResourceSnapshot CaptureMeshResourceSnapshot(const MeshResources& mesh){
    MaterialPassMeshResourceSnapshot snapshot;
    snapshot.sourceBuffers = mesh;
    for(u32 slotIndex = 0u; slotIndex < NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT; ++slotIndex)
        snapshot.geometryHeapHandles[slotIndex] = mesh.geometryHeapHandles[slotIndex];
    snapshot.emulationVertexBuffer = mesh.emulationVertexBuffer;
    snapshot.objectGeometryCache = RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh);
    snapshot.emulationVertexHeapHandle = mesh.emulationVertexHeapHandle;
    snapshot.emulationIndexByteOffset = mesh.emulationIndexByteOffset;
    snapshot.meshletCount = mesh.meshletCount;
    snapshot.meshletPrimitiveIndexCount = mesh.meshletPrimitiveIndexCount;
    snapshot.runtimeMesh = mesh.runtimeMesh;
    snapshot.dynamicMeshletBoundsFresh = mesh.dynamicMeshletBoundsFresh;
    snapshot.dynamicMeshletConesFresh = mesh.dynamicMeshletConesFresh;
    return snapshot;
}

[[nodiscard]] static MaterialPassPipelineResourceSnapshot CapturePipelineResourceSnapshot(
    const MaterialPipelineResources& pipelineResources
)noexcept{
    return {
        .indexedPipeline = pipelineResources.indexedPipeline,
        .emulationPipeline = pipelineResources.emulationPipeline,
        .meshletPipeline = pipelineResources.meshletPipeline,
        .computePipeline = pipelineResources.computePipeline,
        .objectGeometryDecodePipeline = pipelineResources.objectGeometryDecodePipeline,
        .sharedGeometryComputeProgram = pipelineResources.sharedGeometryComputeProgram,
        .indexedGeometryOutput = pipelineResources.indexedGeometryOutput,
    };
}

struct MaterialInstanceAppend{
    u32 instanceIndex = 0u;
    ECSRenderDetail::MaterialTypedInstanceRanges typedRanges;
};

struct MaterialTypedByteRangeKey{
    Name materialName = s_NameNone;
    u64 typedLayoutHash = 0u;

    friend bool operator==(const MaterialTypedByteRangeKey& lhs, const MaterialTypedByteRangeKey& rhs)noexcept{
        return lhs.materialName == rhs.materialName
            && lhs.typedLayoutHash == rhs.typedLayoutHash
        ;
    }
};

struct MaterialTypedByteRangeKeyHasher{
    usize operator()(const MaterialTypedByteRangeKey& key)const{
        usize seed = Hasher<Name>{}(key.materialName);
        ::HashCombine(seed, key.typedLayoutHash);
        return seed;
    }
};

struct MaterialTypedByteRangeCache{
    ECSRenderDetail::MaterialTypedByteRange constantRange;
    ECSRenderDetail::MaterialTypedByteRange defaultMutableRange;
    bool constantRangeCached = false;
    bool defaultMutableRangeCached = false;
};

[[nodiscard]] static bool CsgFrameHasReceiverPassWork(
    const CsgFrameState& csgFrameState,
    const CsgReceiverPass::Enum receiverPass
)noexcept{
    switch(receiverPass){
    case CsgReceiverPass::Opaque: return csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    case CsgReceiverPass::Transparent: return csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork;
    default: return false;
    }
}

inline constexpr Core::GpuTimingScopeDefinition s_NoneGpuTimingScope;

[[nodiscard]] static const Core::GpuTimingScopeDefinition& MaterialPassGpuTimingScope(const MaterialPipelinePass::Enum pass)noexcept{
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy: return RendererGpuTimingScope::s_AvboitOccupancy;
    case MaterialPipelinePass::AvboitExtinction: return RendererGpuTimingScope::s_AvboitExtinction;
    case MaterialPipelinePass::AvboitAccumulate: return RendererGpuTimingScope::s_AvboitAccumulate;
    default: return s_NoneGpuTimingScope;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMaterialSystem::prepareMaterialPassResources(
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    const CsgFrameState& csgFrameState,
    const AvboitFrameTargets* avboitTargets
){
    if(!framebuffer)
        return false;

    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(pass);
    if(usesAvboit && (!avboitTargets || !avboitTargets->valid()))
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PreparePassArena);
    MaterialPassDrawItemPartitions drawItems{scratchArena};
    InstanceGpuDataVector instanceData{scratchArena};
    CsgFrameGpuData csgFrameData{scratchArena};
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{scratchArena};
#endif
    MaterialTypedByteDataVector materialTypedBytes{scratchArena};

    gatherMaterialPassDrawItems(
        framebuffer,
        pass,
        transparent,
        csgFrameState,
        drawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes,
        RendererResourceLookupMode::CreateMissing,
        nullptr
    );
    if(drawItems.empty())
        return true;

    const bool drawBuffersReady = prepareMaterialPassDrawBuffers(instanceData, materialTypedBytes);
    const bool regularDrawResourcesReady = prepareMaterialPassResourceBindings(drawItems.regular);
    const bool csgDrawResourcesReady = drawItems.csg.empty() || prepareMaterialPassResourceBindings(drawItems.csg);
    const bool csgReceiverSurfaceDrawResourcesReady =
        drawItems.csgReceiverSurface.empty() || prepareMaterialPassResourceBindings(drawItems.csgReceiverSurface)
    ;

    return
        drawBuffersReady
        && regularDrawResourcesReady
        && csgDrawResourcesReady
        && csgReceiverSurfaceDrawResourcesReady
    ;
}

void RendererMaterialSystem::renderPreparedMaterialPass(
    Core::CommandList& commandList,
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const AvboitFrameTargets* const avboitTargets,
    const MaterialPassDrawItemPartitions& drawItems,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const usize instanceCount,
    const usize materialTypedByteCount,
    const bool materialFrameStatesGraphOwned,
    const bool materialGeometryStatesGraphOwned,
    const bool emulationOutputEntryStateGraphOwned,
    Optional<Core::GpuTimingMeasure>* const emulationOutputTiming,
    const bool csgEmulationOutputEntryStateGraphOwned,
    const bool emulationOutputReused
){
    const auto discardEmulationOutputTiming = [emulationOutputTiming](){
        if(!emulationOutputTiming || !emulationOutputTiming->has_value())
            return;
        emulationOutputTiming->value().discardTiming();
        emulationOutputTiming->reset();
    };
    if(!framebuffer || drawItems.empty()){
        discardEmulationOutputTiming();
        return;
    }
    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(pass);
    if(usesAvboit && (!avboitTargets || !avboitTargets->valid())){
        discardEmulationOutputTiming();
        return;
    }

    commandList.endRenderPass();

    // Fresh producers open split timing. Reused regular output has a separate, already-validated graph producer.
    const bool emulationOutputStatesGraphOwned =
        emulationOutputEntryStateGraphOwned
        || csgEmulationOutputEntryStateGraphOwned
    ;
    if(emulationOutputReused && (!emulationOutputEntryStateGraphOwned || csgEmulationOutputEntryStateGraphOwned))
        return;
    const bool splitEmulationTiming = emulationOutputStatesGraphOwned && !emulationOutputReused;
    if(splitEmulationTiming && (!emulationOutputTiming || !emulationOutputTiming->has_value()))
        return;

    // Declaration froze ordering and published bytes; keep this consumer side-effect free.
    if(!frameBindings.frameReady(instanceCount, materialTypedByteCount)){
        discardEmulationOutputTiming();
        return;
    }
    const bool regularDrawResourcesReady = materialPassDrawResourcesReady(drawItems.regular, frameBindings);
    // Active handoff covers every regular draw; reject on late resource disagreement.
    if(emulationOutputEntryStateGraphOwned && !regularDrawResourcesReady){
        discardEmulationOutputTiming();
        return;
    }
    const bool csgResourcesReady = csgResources.frameReady(csgFrameData);
    const bool csgDrawResourcesReady = csgResourcesReady
        && (drawItems.csg.empty() || materialPassDrawResourcesReady(drawItems.csg, frameBindings))
    ;
    if(csgEmulationOutputEntryStateGraphOwned && !csgDrawResourcesReady){
        discardEmulationOutputTiming();
        return;
    }

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
    const MaterialPassDrawContext regularDrawContext{
        commandList,
        framebuffer,
        avboitTargets,
        viewportState,
        nullptr,
        frameBindings,
        pass,
        materialFrameStatesGraphOwned,
        materialGeometryStatesGraphOwned,
        emulationOutputEntryStateGraphOwned
    };
    // CSG opts in only via its own frozen producer; keep it separate from the regular flag.
    const MaterialPassDrawContext csgDrawContext{
        commandList,
        framebuffer,
        avboitTargets,
        viewportState,
        &csgResources,
        frameBindings,
        pass,
        materialFrameStatesGraphOwned,
        materialGeometryStatesGraphOwned,
        csgEmulationOutputEntryStateGraphOwned
    };
    const auto recordPreparedDraws = [&](){
        if(regularDrawResourcesReady)
            renderMaterialPassDrawItems(regularDrawContext, drawItems.regular);
        if(csgDrawResourcesReady)
            renderMaterialPassDrawItems(csgDrawContext, drawItems.csg);
    };
    if(splitEmulationTiming){
        recordPreparedDraws();
        emulationOutputTiming->value().finishTiming(commandList);
        emulationOutputTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(
            m_graphics.gpuTiming(),
            __hidden_material_pass::MaterialPassGpuTimingScope(pass),
            m_graphics.getDevice(),
            commandList
        );
        recordPreparedDraws();
    }
}

void RendererMaterialSystem::gatherMaterialPassDrawItems(
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    const CsgFrameState& csgFrameState,
    MaterialPassDrawItemPartitions& drawItems,
    InstanceGpuDataVector& instanceData,
    CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
    MaterialTypedByteDataVector& materialTypedBytes,
    const RendererResourceLookupMode::Enum lookupMode,
    const ECSRenderDetail::MeshViewGpuData* const csgWorkRegionMeshViewState
){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();
    auto* ecsMeshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    if(!ecsMeshSystemPtr){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: MeshSystem is not registered; material pass cannot resolve meshes"));
        return;
    }
    NWB::Impl::MeshSystem& ecsMeshSystem = *ecsMeshSystemPtr;
    const usize rendererCapacity = rendererView.candidateCount();
    drawItems.reserve(rendererCapacity);
    instanceData.reserve(rendererCapacity);
#if defined(NWB_DEBUG)
    materialTypedRanges.reserve(rendererCapacity);
#endif
    const usize materialTypedByteReserve = rendererCapacity <= Limit<usize>::s_Max / sizeof(u32)
        ? rendererCapacity * sizeof(u32)
        : rendererCapacity
    ;
    materialTypedBytes.reserve(materialTypedByteReserve);

    using MaterialTypedByteRangeMap = HashMap<__hidden_material_pass::MaterialTypedByteRangeKey, __hidden_material_pass::MaterialTypedByteRangeCache, Core::Alloc::ScratchArena, __hidden_material_pass::MaterialTypedByteRangeKeyHasher, EqualTo<__hidden_material_pass::MaterialTypedByteRangeKey>>;
    MaterialTypedByteRangeMap materialTypedRangeCache(
        0,
        __hidden_material_pass::MaterialTypedByteRangeKeyHasher(),
        EqualTo<__hidden_material_pass::MaterialTypedByteRangeKey>(),
        materialTypedBytes.get_allocator().arena()
    );
    materialTypedRangeCache.reserve(rendererCapacity);

    ECSRenderDetail::MaterialTypedByteContentRangeMap mutableMaterialTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentRangeMap::hasher(),
        ECSRenderDetail::MaterialTypedByteContentRangeMap::key_equal(),
        materialTypedBytes.get_allocator().arena()
    );
    mutableMaterialTypedRanges.reserve(rendererCapacity);

    const Core::FramebufferInfoEx& framebufferInfo = framebuffer->getFramebufferInfo();
    const CsgReceiverPass::Enum csgReceiverPass = transparent ? CsgReceiverPass::Transparent : CsgReceiverPass::Opaque;
    const bool csgPassActive =
        MaterialPipelinePassUsesRendererCsgClip(pass, transparent)
        && __hidden_material_pass::CsgFrameHasReceiverPassWork(csgFrameState, csgReceiverPass)
    ;
    Optional<CsgFrameReceiverLookup> csgReceiverLookup;
    const CsgFrameReceiverLookup* csgReceiverLookupPtr = nullptr;
    if(csgPassActive){
        csgReceiverLookup.emplace(m_world, materialTypedBytes.get_allocator().arena());
        if(!csgReceiverLookup->empty()){
            csgReceiverLookupPtr = &*csgReceiverLookup;
            csgFrameData.reserve(rendererCapacity, csgReceiverLookupPtr->cutterCount());
        }
    }

    auto appendConstantMaterialTypedBytes = [&](
        const MaterialSurfaceInfo& materialInfo
    ) -> Expected<ECSRenderDetail::MaterialTypedByteRange>{
        const __hidden_material_pass::MaterialTypedByteRangeKey rangeKey{
            materialInfo.materialName,
            materialInfo.typedLayoutHash
        };
        auto cacheIt = materialTypedRangeCache.try_emplace(rangeKey).first;
        __hidden_material_pass::MaterialTypedByteRangeCache& cache = cacheIt.value();
        if(cache.constantRangeCached){
            return cache.constantRange;
        }

        const auto range = ECSRenderDetail::AppendMaterialTypedByteRange(materialTypedBytes, materialInfo.constantTypedBytes);
        if(!range)
            return MakeUnexpected(Failure{});

        cache.constantRange = *range;
        cache.constantRangeCached = true;
        return range;
    };

    auto appendDefaultMutableMaterialTypedBytes = [&](
        const MaterialSurfaceInfo& materialInfo
    ) -> Expected<ECSRenderDetail::MaterialTypedByteRange>{
        const __hidden_material_pass::MaterialTypedByteRangeKey rangeKey{
            materialInfo.materialName,
            materialInfo.typedLayoutHash
        };
        auto cacheIt = materialTypedRangeCache.try_emplace(rangeKey).first;
        __hidden_material_pass::MaterialTypedByteRangeCache& cache = cacheIt.value();
        if(cache.defaultMutableRangeCached){
            return cache.defaultMutableRange;
        }

        const auto range = ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
            materialTypedBytes,
            mutableMaterialTypedRanges,
            materialInfo.mutableDefaultTypedBytes
        );
        if(!range)
            return MakeUnexpected(Failure{});

        cache.defaultMutableRange = *range;
        cache.defaultMutableRangeCached = true;
        return range;
    };

    auto appendMutableInstanceTypedBytes = [&](
        const Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo
    ) -> Expected<ECSRenderDetail::MaterialTypedByteRange>{
        const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
        if(!materialInstance || materialInstance->overrides.empty())
            return appendDefaultMutableMaterialTypedBytes(materialInfo);

        // Prepared-only gathers must never populate the persistent override cache.
        const auto mutableTypedBytes = lookupMode == RendererResourceLookupMode::CreateMissing
            ? prepareMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance)
            : findPreparedMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance)
        ;
        if(!mutableTypedBytes)
            return MakeUnexpected(Failure{});

        return ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
            materialTypedBytes,
            mutableMaterialTypedRanges,
            **mutableTypedBytes
        );
    };

    auto appendDrawForMesh = [&](
        const Core::ECS::EntityID entity,
        const Core::Assets::AssetRef<Material>& material,
        MeshResources& mesh,
        const CsgReceiverDrawState& csgReceiverState
    ) -> bool{
        NWB_ASSERT(mesh.valid());

        // Mesh creation establishes source-stream descriptors; preparation only validates them.
        if(!m_meshSystem.meshGeometryHeapHandlesReady(mesh))
            return false;

        const NWB::Impl::Scene::TransformComponent* transform = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);

        const auto materialInfoResult = lookupMode == RendererResourceLookupMode::CreateMissing
            ? createMaterialSurfaceInfo(material)
            : findMaterialSurfaceInfo(material)
        ;
        if(!materialInfoResult)
            return false;
        MaterialSurfaceInfo* const materialInfo = *materialInfoResult;
        if(materialInfo->transparent != transparent)
            return false;

        MaterialPipelineKey pipelineKey;
        pipelineKey.material = materialInfo->materialName;
        pipelineKey.framebufferInfo = framebufferInfo;
        pipelineKey.pass = pass;
        pipelineKey.twoSided = materialInfo->twoSided;
        const bool csgClipRequested =
            csgReceiverLookupPtr
            && csgReceiverState.active
            && MaterialPipelinePassUsesRendererCsgClip(pass, transparent)
        ;
        if(csgClipRequested && !materialInfo->csgCapSurfaceDispatchAvailable){
            if(!materialInfo->csgCapSurfaceDispatchUnavailableLogged){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: CSG receiver material '{}' has no cook-generated surface hook; clipping is disabled because cap fill requires the declared typed surface contract"), StringConvert(materialInfo->materialName.resolvedText()));
                materialInfo->csgCapSurfaceDispatchUnavailableLogged = true;
            }
        }
        const bool csgClipCandidate = csgClipRequested && materialInfo->csgCapSurfaceDispatchAvailable;
        const auto csgClipInfo = csgClipCandidate
            ? m_csgSystem.resolveCsgReceiverClipDrawInfo(*csgReceiverLookupPtr, csgReceiverState, mesh.csgLocalBounds, transform)
            : Expected<CsgReceiverClipDrawInfo>(MakeUnexpected(Failure{}))
        ;
        const bool csgClipActive = csgClipInfo && csgClipInfo->cutterCount > 0u;
        if(csgClipActive){
            pipelineKey.csgMode = MaterialPipelineCsgMode::ClipOnly;
            pipelineKey.csgEvaluatorVariant = csgClipInfo->evaluatorVariant;
            pipelineKey.twoSided = true;
        }

        auto appendInstance = [&]() -> Expected<__hidden_material_pass::MaterialInstanceAppend>{
            NWB_ASSERT(instanceData.size() < static_cast<usize>(Limit<u32>::s_Max));

            const auto constantRange = appendConstantMaterialTypedBytes(*materialInfo);
            if(!constantRange)
                return MakeUnexpected(Failure{});
            const auto mutableRange = appendMutableInstanceTypedBytes(entity, *materialInfo);
            if(!mutableRange)
                return MakeUnexpected(Failure{});
            const ECSRenderDetail::MaterialTypedInstanceRanges typedRanges{ *constantRange, *mutableRange };

            const u32 instanceIndex = static_cast<u32>(instanceData.size());
            InstanceGpuData instance = ECSRenderDetail::BuildInstanceGpuData(transform, typedRanges);
            m_meshSystem.populateMeshGeometryHeapSlots(instance, mesh);
            instanceData.push_back(Move(instance));
            if(csgReceiverLookupPtr)
                csgFrameData.receiverRanges.push_back(CsgReceiverRangeGpuData{});
#if defined(NWB_DEBUG)
            materialTypedRanges.push_back(typedRanges);
#endif
            return __hidden_material_pass::MaterialInstanceAppend{ instanceIndex, typedRanges };
        };

        if(pass == MaterialPipelinePass::CsgReceiverSurface && !csgClipActive){
            if(!csgReceiverLookupPtr)
                return false;

            return appendInstance().has_value();
        }

        const auto pipelineResult = lookupMode == RendererResourceLookupMode::CreateMissing
            ? createRendererPipeline(*materialInfo, pipelineKey, *framebuffer)
            : findRendererPipeline(pipelineKey)
        ;
        if(!pipelineResult)
            return false;
        MaterialPipelineResources* const pipelineResources = *pipelineResult;
        const auto prepareGeometryResources = [&](const MaterialPipelineResources& resources){
            switch(resources.renderPath){
            case RenderPath::MeshShader:
                return true;
            case RenderPath::VertexIndexed:{
                if(
                    lookupMode == RendererResourceLookupMode::CreateMissing
                    && !m_meshSystem.prepareObjectGeometryCache(mesh, resources.objectGeometryDecodePipeline)
                )
                    return false;
                const auto cache = RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh);
                return cache.valid() && cache.decoderPipeline == resources.objectGeometryDecodePipeline;
            }
            case RenderPath::ComputeEmulation:
                if(
                    lookupMode == RendererResourceLookupMode::CreateMissing
                    && !m_meshSystem.prepareComputeEmulationResources(mesh)
                )
                    return false;
                return
                    mesh.emulationVertexBuffer
                    && mesh.emulationVertexHeapHandle.valid()
                    && mesh.emulationVertexHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
                    && (!resources.indexedGeometryOutput || mesh.emulationIndexByteOffset != 0u)
                ;
            default:
                return false;
            }
        };
        if(!prepareGeometryResources(*pipelineResources))
            return false;
        const RenderPath::Enum renderPath = pipelineResources->renderPath;
        // Freeze primary handles first; sibling cache creation may invalidate this pointer.
        const MaterialPassPipelineResourceSnapshot pipelineResourceSnapshot =
            __hidden_material_pass::CapturePipelineResourceSnapshot(*pipelineResources)
        ;

        const bool passDrawItemActive = pass != MaterialPipelinePass::CsgReceiverSurface;
        const bool csgReceiverSurfaceActive =
            csgClipActive
            && (pass == MaterialPipelinePass::Opaque || pass == MaterialPipelinePass::CsgReceiverSurface)
        ;
        MaterialPipelineKey csgReceiverSurfacePipelineKey = pipelineKey;
        MaterialPipelineResources* csgReceiverSurfacePipelineResources = nullptr;
        RenderPath::Enum csgReceiverSurfaceRenderPath = RenderPath::MeshShader;
        if(csgReceiverSurfaceActive){
            csgReceiverSurfacePipelineKey.pass = MaterialPipelinePass::CsgReceiverSurface;
            if(pass == MaterialPipelinePass::CsgReceiverSurface){
                csgReceiverSurfacePipelineResources = pipelineResources;
            }
            else{
                const auto csgReceiverSurfacePipeline = lookupMode == RendererResourceLookupMode::CreateMissing
                    ? createRendererPipeline(*materialInfo, csgReceiverSurfacePipelineKey, *framebuffer)
                    : findRendererPipeline(csgReceiverSurfacePipelineKey)
                ;
                if(!csgReceiverSurfacePipeline)
                    return false;
                csgReceiverSurfacePipelineResources = *csgReceiverSurfacePipeline;
            }
            if(!prepareGeometryResources(*csgReceiverSurfacePipelineResources))
                return false;
            csgReceiverSurfaceRenderPath = csgReceiverSurfacePipelineResources->renderPath;
        }

        auto appendDrawItemForRenderPath = [](
            const RenderPath::Enum renderPath,
            const MaterialPassDrawItem& drawItem,
            MaterialPassDrawItems& targetDrawItems
        ){
            switch(renderPath){
            case RenderPath::MeshShader:{
                targetDrawItems.meshDrawItems.push_back(drawItem);
                break;
            }
            case RenderPath::VertexIndexed:{
                targetDrawItems.indexedDrawItems.push_back(drawItem);
                break;
            }
            case RenderPath::ComputeEmulation:{
                targetDrawItems.computeDrawItems.push_back(drawItem);
                break;
            }
            default:
                NWB_ASSERT(false);
                break;
            }
        };

        const auto appendedInstance = appendInstance();
        if(!appendedInstance)
            return false;
        const u32 instanceIndex = appendedInstance->instanceIndex;
        const auto& typedRanges = appendedInstance->typedRanges;

        if(csgClipActive){
            auto csgRange = m_csgSystem.appendCsgReceiverClipData(
                *csgReceiverLookupPtr,
                csgReceiverState,
                mesh.csgLocalBounds,
                transform,
                framebufferInfo.width,
                framebufferInfo.height,
                csgFrameData,
                csgWorkRegionMeshViewState
            );
            if(!csgRange)
                return false;
            // Cap shader shares the receiver surface hook; rangeInfo.w carries the BXDF id.
            csgRange->shadingModelId = materialInfo->shadingModelId;
            csgRange->surfaceDispatchId = materialInfo->surfaceDispatchId;
            csgRange->materialConstantByteOffset = typedRanges.constantRange.byteOffset;
            csgRange->meshInstanceIndex = instanceIndex;
            NWB_ASSERT(instanceIndex < csgFrameData.receiverRanges.size());
            csgFrameData.receiverRanges[instanceIndex] = *csgRange;
        }

        MaterialPassDrawItem drawItem;
        drawItem.meshKey = mesh.meshName;
        drawItem.pipelineKey = pipelineKey;
        drawItem.meshResources = __hidden_material_pass::CaptureMeshResourceSnapshot(mesh);
        drawItem.pipelineResources = pipelineResourceSnapshot;
        drawItem.instanceIndex = instanceIndex;
        drawItem.materialConstantByteOffset = typedRanges.constantRange.byteOffset;
        drawItem.shadingModelId = materialInfo->shadingModelId;
        drawItem.meshletConeCullScaleSafe = transform
            ? __hidden_material_pass::MeshletConeCullScaleSafe(LoadFloat(transform->scale))
            : true
        ;

        if(passDrawItemActive){
            MaterialPassDrawItems& targetDrawItems = csgClipActive ? drawItems.csg : drawItems.regular;
            appendDrawItemForRenderPath(renderPath, drawItem, targetDrawItems);
        }

        if(csgReceiverSurfaceActive){
            MaterialPassDrawItem csgReceiverSurfaceDrawItem = drawItem;
            csgReceiverSurfaceDrawItem.pipelineKey = csgReceiverSurfacePipelineKey;
            csgReceiverSurfaceDrawItem.pipelineResources =
                __hidden_material_pass::CapturePipelineResourceSnapshot(*csgReceiverSurfacePipelineResources)
            ;
            appendDrawItemForRenderPath(
                csgReceiverSurfaceRenderPath,
                csgReceiverSurfaceDrawItem,
                drawItems.csgReceiverSurface
            );
        }

        return true;
    };

    for(auto&& [entity, renderer] : rendererView){
        // Every pass consumes the same frozen selection before assigning indices.
        if(!renderer.visible || m_opticalVolumes.isSuppressed(entity))
            continue;

        const auto resolvedMeshResult = ecsMeshSystem.resolveRenderableMeshStatus(entity);
        if(!resolvedMeshResult)
            continue;
        const auto& resolvedMesh = *resolvedMeshResult;

        const auto meshResult = resolvedMesh.runtime
            ? (lookupMode == RendererResourceLookupMode::CreateMissing
                ? m_meshSystem.createRuntimeMeshResources(resolvedMesh.runtimeMesh)
                : m_meshSystem.findRuntimeMeshResources(resolvedMesh.runtimeMesh))
            : (lookupMode == RendererResourceLookupMode::CreateMissing
                ? m_meshSystem.createMeshResources(resolvedMesh.mesh)
                : m_meshSystem.findMeshResources(resolvedMesh.mesh))
        ;
        if(!meshResult)
            continue;
        MeshResources* const mesh = *meshResult;

        CsgReceiverDrawState csgReceiverState;
        if(csgReceiverLookupPtr){
            if(const auto resolvedState = csgReceiverLookupPtr->resolveReceiverDrawState(entity, csgReceiverPass))
                csgReceiverState = *resolvedState;
        }

        appendDrawForMesh(entity, renderer.material, *mesh, csgReceiverState);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

