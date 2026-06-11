// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pass{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_MeshletConeCullUniformScaleEpsilon = 0.0001f;


[[nodiscard]] static bool MeshletConeCullScaleSafe(const SIMDVector scale){
    const f32 x = VectorGetX(scale);
    const f32 y = VectorGetY(scale);
    const f32 z = VectorGetZ(scale);
    if(!IsFinite(x) || !IsFinite(y) || !IsFinite(z))
        return false;
    if(x <= 0.0f || y <= 0.0f || z <= 0.0f)
        return false;

    const f32 minScale = Min<f32>(Min<f32>(x, y), z);
    const f32 maxScale = Max<f32>(Max<f32>(x, y), z);
    return Abs(maxScale - minScale) <= Max<f32>(maxScale, 1.0f) * s_MeshletConeCullUniformScaleEpsilon;
}

struct MaterialTypedByteRangeKey{
    Name materialName = NAME_NONE;
    u64 typedLayoutHash = 0u;

    friend bool operator==(const MaterialTypedByteRangeKey& lhs, const MaterialTypedByteRangeKey& rhs){
        return lhs.materialName == rhs.materialName
            && lhs.typedLayoutHash == rhs.typedLayoutHash
        ;
    }
};

struct MaterialTypedByteRangeKeyHasher{
    usize operator()(const MaterialTypedByteRangeKey& key)const{
        usize seed = Hasher<Name>{}(key.materialName);
        Core::CoreDetail::HashCombine(seed, key.typedLayoutHash);
        return seed;
    }
};

[[nodiscard]] static bool CsgFrameHasReceiverPassWork(
    const CsgFrameState& csgFrameState,
    const CsgReceiverPass::Enum receiverPass
){
    switch(receiverPass){
    case CsgReceiverPass::Opaque: return csgFrameState.hasOpaqueStaticWork || csgFrameState.hasOpaqueSkinnedWork;
    case CsgReceiverPass::Transparent: return csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork;
    default: return false;
    }
}

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

    Core::Alloc::ScratchArena scratchArena;
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
        RendererResourceLookupMode::CreateMissing
    );
    if(drawItems.empty())
        return true;

    const bool drawBuffersReady = prepareMaterialPassDrawBuffers(instanceData, materialTypedBytes);
    const bool regularDrawResourcesReady = prepareMaterialPassDrawResources(drawItems.regular);
    const bool csgResourcesReady = drawItems.csg.empty() || m_renderer.csgSystem().prepareCsgFrameBuffers(csgFrameData);
    const bool csgDrawResourcesReady = csgResourcesReady && (drawItems.csg.empty() || prepareMaterialPassDrawResources(drawItems.csg));
    const bool csgReceiverSurfaceDrawResourcesReady =
        csgResourcesReady
        && (drawItems.csgReceiverSurface.empty() || prepareMaterialPassDrawResources(drawItems.csgReceiverSurface))
    ;

    return
        drawBuffersReady
        && regularDrawResourcesReady
        && csgResourcesReady
        && csgDrawResourcesReady
        && csgReceiverSurfaceDrawResourcesReady
    ;
}

void RendererMaterialSystem::renderMaterialPass(
    Core::CommandList& commandList,
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    const CsgFrameState& csgFrameState,
    Core::BindingSet* passBindingSet,
    const AvboitFrameTargets* avboitTargets
){
    if(!framebuffer)
        return;
    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(pass);
    if(usesAvboit && (!passBindingSet || !avboitTargets || !avboitTargets->valid()))
        return;

    commandList.endRenderPass();

    Core::Alloc::ScratchArena scratchArena;
    MaterialPassDrawItemPartitions drawItems{scratchArena};
    InstanceGpuDataVector instanceData{scratchArena};
    CsgFrameGpuData csgFrameData{scratchArena};
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{scratchArena};
#endif
    MaterialTypedByteDataVector materialTypedBytes{scratchArena};

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());

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
        RendererResourceLookupMode::PreparedOnly
    );
    if(drawItems.empty())
        return;

    f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(framebuffer->getFramebufferInfo());
    if(avboitTargets && avboitTargets->fullWidth > 0 && avboitTargets->fullHeight > 0)
        meshViewAspectRatio = ECSRenderDetail::ResolveExtentAspectRatio(avboitTargets->fullWidth, avboitTargets->fullHeight);
    if(!m_renderer.meshSystem().updateMeshViewBuffer(commandList, meshViewAspectRatio))
        return;
    if(!materialPassDrawBuffersReady(instanceData, materialTypedBytes))
        return;
    const bool regularDrawResourcesReady = materialPassDrawResourcesReady(drawItems.regular);
    const bool csgResourcesReady = drawItems.csg.empty() || m_renderer.csgSystem().csgFrameBuffersReady(csgFrameData);
    const bool csgDrawResourcesReady = csgResourcesReady && (drawItems.csg.empty() || materialPassDrawResourcesReady(drawItems.csg));
    if(!uploadMaterialPassDrawBuffers(
        commandList,
        instanceData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes
    ))
        return;
    const bool csgUploadReady = csgResourcesReady && (drawItems.csg.empty() || m_renderer.csgSystem().uploadCsgFrameBuffers(commandList, csgFrameData));

    if(passBindingSet){
        commandList.setResourceStatesForBindingSet(passBindingSet);
        commandList.commitBarriers();
    }
    const MaterialPassDrawContext drawContext{ commandList, framebuffer, pass, passBindingSet, avboitTargets, viewportState };
    if(regularDrawResourcesReady)
        renderMaterialPassDrawItems(drawContext, drawItems.regular);
    if(csgUploadReady && csgDrawResourcesReady){
        renderMaterialPassDrawItems(drawContext, drawItems.csg);
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
    const RendererResourceLookupMode::Enum lookupMode
){
    if(!framebuffer)
        return;

    auto rendererView = world().view<RendererComponent>();
    auto* ecsMeshSystem = world().getSystem<NWB::Impl::MeshSystem>();
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

    using MaterialTypedByteRangeMap = HashMap<
        __hidden_material_pass::MaterialTypedByteRangeKey,
        ECSRenderDetail::MaterialTypedByteRange,
        __hidden_material_pass::MaterialTypedByteRangeKeyHasher,
        EqualTo<__hidden_material_pass::MaterialTypedByteRangeKey>,
        Core::Alloc::ScratchArena
    >;
    MaterialTypedByteRangeMap constantMaterialTypedRanges(
        0,
        __hidden_material_pass::MaterialTypedByteRangeKeyHasher(),
        EqualTo<__hidden_material_pass::MaterialTypedByteRangeKey>(),
        materialTypedBytes.get_allocator().arena()
    );
    constantMaterialTypedRanges.reserve(rendererCapacity);

    using MaterialTypedByteContentRangeMap = HashMap<
        ECSRenderDetail::MaterialTypedByteContentKey,
        ECSRenderDetail::MaterialTypedByteRange,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher,
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>,
        Core::Alloc::ScratchArena
    >;
    MaterialTypedByteContentRangeMap mutableMaterialTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        materialTypedBytes.get_allocator().arena()
    );
    mutableMaterialTypedRanges.reserve(rendererCapacity);

    const Core::FramebufferInfo& framebufferInfo = framebuffer->getFramebufferInfo();
    const CsgReceiverPass::Enum csgReceiverPass = transparent ? CsgReceiverPass::Transparent : CsgReceiverPass::Opaque;
    const bool csgPassActive =
        MaterialPipelinePassUsesRendererCsgClip(pass, transparent)
        && __hidden_material_pass::CsgFrameHasReceiverPassWork(csgFrameState, csgReceiverPass)
    ;
    Optional<CsgFrameReceiverLookup> csgReceiverLookup;
    const CsgFrameReceiverLookup* csgReceiverLookupPtr = nullptr;
    if(csgPassActive){
        csgReceiverLookup.emplace(world(), materialTypedBytes.get_allocator().arena());
        if(!csgReceiverLookup->empty()){
            csgReceiverLookupPtr = &*csgReceiverLookup;
            csgFrameData.reserve(rendererCapacity, csgReceiverLookupPtr->cutterCount());
        }
    }

    auto appendConstantMaterialTypedBytes = [&](
        const MaterialSurfaceInfo& materialInfo,
        ECSRenderDetail::MaterialTypedByteRange& outRange
    ) -> bool{
        const __hidden_material_pass::MaterialTypedByteRangeKey rangeKey{
            materialInfo.materialName,
            materialInfo.typedLayoutHash
        };
        const auto foundRange = constantMaterialTypedRanges.find(rangeKey);
        if(foundRange != constantMaterialTypedRanges.end()){
            outRange = foundRange.value();
            return true;
        }

        if(!ECSRenderDetail::AppendMaterialTypedByteRange(materialTypedBytes, materialInfo.constantTypedBytes, outRange))
            return false;

        constantMaterialTypedRanges.emplace(rangeKey, outRange);
        return true;
    };

    auto appendMutableInstanceTypedBytes = [&](
        const Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        ECSRenderDetail::MaterialTypedByteRange& outRange
    ) -> bool{
        const MaterialInstanceComponent* materialInstance = world().tryGetComponent<MaterialInstanceComponent>(entity);
        const MaterialTypedByteVector* mutableTypedBytes = nullptr;
        if(!resolveMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance, mutableTypedBytes))
            return false;

        return ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
            materialTypedBytes,
            mutableMaterialTypedRanges,
            *mutableTypedBytes,
            outRange
        );
    };

    auto appendDrawForMesh = [&](
        const Core::ECS::EntityID entity,
        const Core::Assets::AssetRef<Material>& material,
        MeshResources& mesh,
        const CsgReceiverDrawState& csgReceiverState
    ) -> bool{
        NWB_ASSERT(mesh.valid());

        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);

        MaterialSurfaceInfo* materialInfo = nullptr;
        const bool materialInfoReady = lookupMode == RendererResourceLookupMode::CreateMissing
            ? createMaterialSurfaceInfo(material, materialInfo)
            : findMaterialSurfaceInfo(material, materialInfo)
        ;
        if(!materialInfoReady)
            return false;
        NWB_ASSERT(materialInfo);
        if(materialInfo->transparent != transparent)
            return false;

        MaterialPipelineKey pipelineKey;
        pipelineKey.material = materialInfo->materialName;
        pipelineKey.framebufferInfo = framebufferInfo;
        pipelineKey.pass = pass;
        pipelineKey.twoSided = materialInfo->twoSided;
        const bool csgClipCandidate =
            csgReceiverLookupPtr
            && csgReceiverState.active
            && MaterialPipelinePassUsesRendererCsgClip(pass, transparent)
        ;
        CsgReceiverClipDrawInfo csgClipInfo;
        const bool csgClipInfoReady =
            csgClipCandidate
            && m_renderer.csgSystem().resolveCsgReceiverClipDrawInfo(
                *csgReceiverLookupPtr,
                entity,
                mesh.csgLocalBounds,
                transform,
                csgClipInfo
            )
        ;
        const bool csgClipActive = csgClipInfoReady && csgClipInfo.cutterCount > 0u;
        if(csgClipActive){
            pipelineKey.csgMode = MaterialPipelineCsgMode::ClipOnly;
            pipelineKey.csgEvaluatorVariant = csgClipInfo.evaluatorVariant;
            pipelineKey.twoSided = true;
        }

        MaterialPipelineResources* pipelineResources = nullptr;
        const bool pipelineReady = lookupMode == RendererResourceLookupMode::CreateMissing
            ? createRendererPipeline(*materialInfo, pipelineKey, framebuffer, pipelineResources)
            : findRendererPipeline(pipelineKey, pipelineResources)
        ;
        if(!pipelineReady)
            return false;
        NWB_ASSERT(pipelineResources);
        const RenderPath::Enum renderPath = pipelineResources->renderPath;

        const bool csgReceiverSurfaceActive = csgClipActive && pass == MaterialPipelinePass::Opaque;
        MaterialPipelineKey csgReceiverSurfacePipelineKey = pipelineKey;
        MaterialPipelineResources* csgReceiverSurfacePipelineResources = nullptr;
        RenderPath::Enum csgReceiverSurfaceRenderPath = RenderPath::MeshShader;
        if(csgReceiverSurfaceActive){
            csgReceiverSurfacePipelineKey.pass = MaterialPipelinePass::CsgReceiverSurface;
            const bool csgReceiverSurfacePipelineReady = lookupMode == RendererResourceLookupMode::CreateMissing
                ? createRendererPipeline(*materialInfo, csgReceiverSurfacePipelineKey, framebuffer, csgReceiverSurfacePipelineResources)
                : findRendererPipeline(csgReceiverSurfacePipelineKey, csgReceiverSurfacePipelineResources)
            ;
            if(!csgReceiverSurfacePipelineReady)
                return false;
            NWB_ASSERT(csgReceiverSurfacePipelineResources);
            csgReceiverSurfaceRenderPath = csgReceiverSurfacePipelineResources->renderPath;
        }

        auto appendInstance = [&](ECSRenderDetail::MaterialTypedInstanceRanges& typedRanges) -> u32{
#if defined(NWB_DEBUG)
            if(instanceData.size() >= static_cast<usize>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer instance count exceeds u32 limits"));
                return Limit<u32>::s_Max;
            }
#endif

            if(!appendConstantMaterialTypedBytes(*materialInfo, typedRanges.constantRange))
                return Limit<u32>::s_Max;
            if(!appendMutableInstanceTypedBytes(entity, *materialInfo, typedRanges.mutableRange))
                return Limit<u32>::s_Max;

            const u32 instanceIndex = static_cast<u32>(instanceData.size());
            instanceData.push_back(ECSRenderDetail::BuildInstanceGpuData(transform, typedRanges));
            if(csgReceiverLookupPtr)
                csgFrameData.receiverRanges.push_back(CsgReceiverRangeGpuData{});
#if defined(NWB_DEBUG)
            materialTypedRanges.push_back(typedRanges);
#endif
            return instanceIndex;
        };

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
            case RenderPath::ComputeEmulation:{
                targetDrawItems.computeDrawItems.push_back(drawItem);
                break;
            }
            default:
                NWB_ASSERT(false);
                break;
            }
        };

        ECSRenderDetail::MaterialTypedInstanceRanges typedRanges;
        const u32 instanceIndex = appendInstance(typedRanges);
        if(instanceIndex == Limit<u32>::s_Max)
            return false;

        CsgReceiverRangeGpuData csgRange;
        if(csgClipActive){
            if(!m_renderer.csgSystem().appendCsgReceiverClipData(
                *csgReceiverLookupPtr,
                entity,
                mesh.csgLocalBounds,
                transform,
                csgFrameData,
                csgRange
            ))
                return false;
            NWB_ASSERT(instanceIndex < csgFrameData.receiverRanges.size());
            csgFrameData.receiverRanges[instanceIndex] = csgRange;
        }

        MaterialPassDrawItem drawItem;
        drawItem.meshKey = mesh.meshName;
        drawItem.pipelineKey = pipelineKey;
        drawItem.instanceIndex = instanceIndex;
        drawItem.materialConstantByteOffset = typedRanges.constantRange.byteOffset;
        drawItem.meshletConeCullScaleSafe = transform
            ? __hidden_material_pass::MeshletConeCullScaleSafe(LoadFloat(transform->scale))
            : true
        ;

        MaterialPassDrawItems& targetDrawItems = csgClipActive ? drawItems.csg : drawItems.regular;
        appendDrawItemForRenderPath(renderPath, drawItem, targetDrawItems);

        if(csgReceiverSurfaceActive){
            MaterialPassDrawItem csgReceiverSurfaceDrawItem = drawItem;
            csgReceiverSurfaceDrawItem.pipelineKey = csgReceiverSurfacePipelineKey;
            appendDrawItemForRenderPath(
                csgReceiverSurfaceRenderPath,
                csgReceiverSurfaceDrawItem,
                drawItems.csgReceiverSurface
            );
        }

        return true;
    };

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        if(!ecsMeshSystem){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: MeshSystem is not registered; renderers cannot resolve mesh"));
            break;
        }

        RenderableMeshDesc resolvedMesh;
        if(!ecsMeshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        if(resolvedMesh.runtime){
            const bool meshReady = lookupMode == RendererResourceLookupMode::CreateMissing
                ? m_renderer.meshSystem().createRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh)
                : m_renderer.meshSystem().findRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh)
            ;
            if(!meshReady)
                continue;
        }
        else{
            const bool meshReady = lookupMode == RendererResourceLookupMode::CreateMissing
                ? m_renderer.meshSystem().createMeshResources(resolvedMesh.mesh, mesh)
                : m_renderer.meshSystem().findMeshResources(resolvedMesh.mesh, mesh)
            ;
            if(!meshReady)
                continue;
        }

        NWB_ASSERT(mesh);
        CsgReceiverDrawState csgReceiverState;
        if(csgReceiverLookupPtr && !csgReceiverLookupPtr->resolveReceiverDrawState(entity, csgReceiverPass, csgReceiverState))
            csgReceiverState = CsgReceiverDrawState{};

        appendDrawForMesh(entity, renderer.material, *mesh, csgReceiverState);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

