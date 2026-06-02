// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pass{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_MeshletConeCullUniformScaleEpsilon = 0.0001f;


[[nodiscard]] static bool meshletConeCullScaleSafe(const SIMDVector scale){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererSystem::renderMaterialPass(
    Core::CommandList& commandList,
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
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
        drawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes
    );
    if(drawItems.empty())
        return;

    f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(framebuffer->getFramebufferInfo());
    if(avboitTargets && avboitTargets->fullWidth > 0 && avboitTargets->fullHeight > 0)
        meshViewAspectRatio = ECSRenderDetail::ResolveExtentAspectRatio(avboitTargets->fullWidth, avboitTargets->fullHeight);
    if(!updateMeshViewBuffer(commandList, meshViewAspectRatio))
        return;
    if(!uploadMaterialPassDrawBuffers(
        commandList,
        instanceData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes
    ))
        return;
    const bool csgUploadReady = drawItems.csg.empty() || uploadCsgFrameBuffers(commandList, csgFrameData);

    if(passBindingSet){
        commandList.setResourceStatesForBindingSet(passBindingSet);
        commandList.commitBarriers();
    }

    const MaterialPassDrawContext drawContext{ commandList, framebuffer, pass, passBindingSet, avboitTargets, viewportState };
    renderMaterialPassDrawItems(drawContext, drawItems.regular);
    if(csgUploadReady)
        renderMaterialPassDrawItems(drawContext, drawItems.csg);
}

void RendererSystem::gatherMaterialPassDrawItems(
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    MaterialPassDrawItemPartitions& drawItems,
    InstanceGpuDataVector& instanceData,
    CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
    MaterialTypedByteDataVector& materialTypedBytes
){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();
    auto* meshSystem = m_world.getSystem<NWB::Impl::MeshSystem>();
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
    CsgFrameReceiverLookup csgReceiverLookup(m_world, materialTypedBytes.get_allocator().arena());
    csgFrameData.reserve(rendererCapacity, csgReceiverLookup.cutterCount());

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
        const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
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

        const NWB::Impl::Scene::TransformComponent* transform = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!createMaterialSurfaceInfo(material, materialInfo))
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
            csgReceiverState.active
            && (
                (pass == MaterialPipelinePass::Opaque && !transparent)
                || (MaterialPipelinePassUsesRendererAvboit(pass) && transparent)
            )
        ;
        const u32 csgClipCutterCount = csgClipCandidate ? countCsgReceiverClipCutters(csgReceiverLookup, entity) : 0u;
        const bool csgClipActive = csgClipCutterCount > 0u;
        if(csgClipActive){
            pipelineKey.csgMode = MaterialPipelineCsgMode::ClipOnly;
            pipelineKey.csgEvaluatorVariant = s_CsgBuiltInShapeShaderModuleName;
        }

        MaterialPipelineResources* pipelineResources = nullptr;
        if(!createRendererPipeline(*materialInfo, pipelineKey, framebuffer, pipelineResources))
            return false;
        NWB_ASSERT(pipelineResources);

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
            csgFrameData.receiverRanges.push_back(CsgReceiverRangeGpuData{});
#if defined(NWB_DEBUG)
            materialTypedRanges.push_back(typedRanges);
#endif
            return instanceIndex;
        };

        auto appendDrawItem = [&](MaterialPassDrawItemVector& drawItems) -> bool{
            ECSRenderDetail::MaterialTypedInstanceRanges typedRanges;
            const u32 instanceIndex = appendInstance(typedRanges);
            if(instanceIndex == Limit<u32>::s_Max)
                return false;

            CsgReceiverRangeGpuData csgRange;
            if(csgClipActive){
                if(!appendCsgReceiverClipData(csgReceiverLookup, entity, csgFrameData, csgRange))
                    return false;
                NWB_ASSERT(instanceIndex < csgFrameData.receiverRanges.size());
                csgFrameData.receiverRanges[instanceIndex] = csgRange;
            }

            MaterialPassDrawItem drawItem;
            drawItem.meshKey = mesh.meshName;
            drawItem.pipelineKey = pipelineKey;
            drawItem.instanceIndex = instanceIndex;
            drawItem.materialConstantByteOffset = typedRanges.constantRange.byteOffset;
            drawItem.csgCutterCount = csgClipActive ? csgRange.cutterCount : 0u;
            drawItem.csgGenerateCaps = csgReceiverState.generateCaps;
            drawItem.meshletConeCullScaleSafe = transform
                ? __hidden_material_pass::meshletConeCullScaleSafe(LoadFloat(transform->scale))
                : true
            ;
            drawItems.push_back(drawItem);

            if(
                csgClipActive
                && csgReceiverState.generateCaps
                && pass == MaterialPipelinePass::Opaque
                && !transparent
                && !appendCsgReceiverPlaneCapGeometry(
                    mesh,
                    transform,
                    instanceIndex,
                    csgRange,
                    csgFrameData
                )
            )
                return false;

            return true;
        };

        MaterialPassDrawItems& targetDrawItems = csgClipActive ? drawItems.csg : drawItems.regular;
        switch(pipelineResources->renderPath){
        case RenderPath::MeshShader:{
            NWB_ASSERT(pipelineResources->meshletPipeline);
            return appendDrawItem(targetDrawItems.meshDrawItems);
        }
        case RenderPath::ComputeEmulation:{
            NWB_ASSERT(pipelineResources->computePipeline);
            NWB_ASSERT(pipelineResources->emulationPipeline);
            return appendDrawItem(targetDrawItems.computeDrawItems);
        }
        default:
            return false;
        }
    };

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        if(!meshSystem){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: MeshSystem is not registered; renderers cannot resolve mesh"));
            break;
        }

        RenderableMeshDesc resolvedMesh;
        if(!meshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        if(resolvedMesh.runtime){
            if(!createRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh))
                continue;
        }
        else if(!createMeshResources(resolvedMesh.mesh, mesh))
            continue;

        NWB_ASSERT(mesh);
        CsgReceiverDrawState csgReceiverState;
        if(!csgReceiverLookup.empty() && !csgReceiverLookup.resolveReceiverDrawState(entity, csgReceiverPass, csgReceiverState))
            csgReceiverState = CsgReceiverDrawState{};

        appendDrawForMesh(entity, renderer.material, *mesh, csgReceiverState);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

