// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "private.h"
#include "timing_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_draw{


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
    MaterialPassDrawItemVector meshDrawItems{scratchArena};
    MaterialPassDrawItemVector computeDrawItems{scratchArena};
    InstanceGpuDataVector instanceData{scratchArena};
    MaterialTypedByteDataVector materialTypedBytes{scratchArena};

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());

    gatherMaterialPassDrawItems(
        framebuffer,
        pass,
        transparent,
        meshDrawItems,
        computeDrawItems,
        instanceData,
        materialTypedBytes
    );
    if(meshDrawItems.empty() && computeDrawItems.empty())
        return;
#if defined(NWB_DEBUG)
    if(!ECSRenderDetail::ValidateMaterialTypedUploadRanges(instanceData, materialTypedBytes))
        return;
#endif

    f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(framebuffer->getFramebufferInfo());
    if(avboitTargets && avboitTargets->fullWidth > 0 && avboitTargets->fullHeight > 0)
        meshViewAspectRatio = ECSRenderDetail::ResolveExtentAspectRatio(avboitTargets->fullWidth, avboitTargets->fullHeight);
    if(!updateMeshViewBuffer(commandList, meshViewAspectRatio))
        return;

    if(!uploadInstanceBuffer(commandList, instanceData))
        return;
    if(!uploadMaterialTypedBuffer(commandList, materialTypedBytes))
        return;
    recordMaterialTypedUploadStats(instanceData, materialTypedBytes);

    if(passBindingSet){
        commandList.setResourceStatesForBindingSet(passBindingSet);
        commandList.commitBarriers();
    }

    const MaterialPassDrawContext drawContext{ commandList, framebuffer, pass, passBindingSet, avboitTargets, viewportState };
    renderMeshMaterialPassDrawItems(drawContext, meshDrawItems);
    renderComputeMaterialPassDrawItems(drawContext, computeDrawItems);
}

void RendererSystem::gatherMaterialPassDrawItems(
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    MaterialPassDrawItemVector& meshDrawItems,
    MaterialPassDrawItemVector& computeDrawItems,
    InstanceGpuDataVector& instanceData,
    MaterialTypedByteDataVector& materialTypedBytes
){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();
    auto* meshSystem = m_world.getSystem<NWB::Impl::MeshSystem>();
    usize rendererCapacity = rendererView.candidateCount();
    meshDrawItems.reserve(rendererCapacity);
    computeDrawItems.reserve(rendererCapacity);
    instanceData.reserve(rendererCapacity);
    const usize materialTypedByteReserve = rendererCapacity <= Limit<usize>::s_Max / sizeof(u32)
        ? rendererCapacity * sizeof(u32)
        : rendererCapacity
    ;
    materialTypedBytes.reserve(materialTypedByteReserve);

    using MaterialTypedByteRangeMap = HashMap<
        __hidden_draw::MaterialTypedByteRangeKey,
        ECSRenderDetail::MaterialTypedByteRange,
        __hidden_draw::MaterialTypedByteRangeKeyHasher,
        EqualTo<__hidden_draw::MaterialTypedByteRangeKey>,
        Core::Alloc::ScratchArena
    >;
    MaterialTypedByteRangeMap constantMaterialTypedRanges(
        0,
        __hidden_draw::MaterialTypedByteRangeKeyHasher(),
        EqualTo<__hidden_draw::MaterialTypedByteRangeKey>(),
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

    auto appendConstantMaterialTypedBytes = [&](
        const MaterialSurfaceInfo& materialInfo,
        ECSRenderDetail::MaterialTypedByteRange& outRange
    ) -> bool{
        const __hidden_draw::MaterialTypedByteRangeKey rangeKey{
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
        MeshResources& mesh
    ) -> bool{
#if defined(NWB_DEBUG)
        if(!mesh.valid())
            return false;
#endif

        const NWB::Impl::Scene::TransformComponent* transform = m_world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!createMaterialSurfaceInfo(material, materialInfo))
            return false;
#if defined(NWB_DEBUG)
        if(!materialInfo || !materialInfo->valid)
            return false;
#endif
        if(materialInfo->transparent != transparent)
            return false;

        MaterialPipelineKey pipelineKey;
        pipelineKey.material = materialInfo->materialName;
        pipelineKey.framebufferInfo = framebufferInfo;
        pipelineKey.pass = pass;
        pipelineKey.twoSided = materialInfo->twoSided;

        MaterialPipelineResources* pipelineResources = nullptr;
        if(!createRendererPipeline(*materialInfo, pipelineKey, framebuffer, pipelineResources))
            return false;
#if defined(NWB_DEBUG)
        if(!pipelineResources)
            return false;
#endif

        auto appendInstance = [&]() -> u32{
#if defined(NWB_DEBUG)
            if(instanceData.size() >= static_cast<usize>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer instance count exceeds u32 limits"));
                return Limit<u32>::s_Max;
            }
#endif

            ECSRenderDetail::MaterialTypedInstanceRanges typedRanges;
            if(!appendConstantMaterialTypedBytes(*materialInfo, typedRanges.constantRange))
                return Limit<u32>::s_Max;
            if(!appendMutableInstanceTypedBytes(entity, *materialInfo, typedRanges.mutableRange))
                return Limit<u32>::s_Max;

            const u32 instanceIndex = static_cast<u32>(instanceData.size());
            instanceData.push_back(ECSRenderDetail::BuildInstanceGpuData(transform, typedRanges));
            return instanceIndex;
        };

        auto appendDrawItem = [&](MaterialPassDrawItemVector& drawItems) -> bool{
            const u32 instanceIndex = appendInstance();
            if(instanceIndex == Limit<u32>::s_Max)
                return false;

            MaterialPassDrawItem drawItem;
            drawItem.meshKey = mesh.meshName;
            drawItem.pipelineKey = pipelineKey;
            drawItem.instanceIndex = instanceIndex;
            drawItem.meshletConeCullScaleSafe = transform
                ? __hidden_draw::meshletConeCullScaleSafe(LoadFloat(transform->scale))
                : true
            ;
            drawItems.push_back(drawItem);
            return true;
        };

        switch(pipelineResources->renderPath){
        case RenderPath::MeshShader:{
#if defined(NWB_DEBUG)
            if(!pipelineResources->meshletPipeline)
                return false;
#endif
            return appendDrawItem(meshDrawItems);
        }
        case RenderPath::ComputeEmulation:{
#if defined(NWB_DEBUG)
            if(!pipelineResources->computePipeline || !pipelineResources->emulationPipeline)
                return false;
#endif
            return appendDrawItem(computeDrawItems);
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

#if defined(NWB_DEBUG)
        if(mesh)
            appendDrawForMesh(entity, renderer.material, *mesh);
#else
        appendDrawForMesh(entity, renderer.material, *mesh);
#endif
    }
}

void RendererSystem::setMaterialPassCommonBufferStates(
    Core::CommandList& commandList,
    const MeshResources& mesh
){
    forEachMeshSourceBuffer(mesh, [&](const u32, const Core::BufferHandle& buffer, const bool){
        commandList.setBufferState(buffer.get(), Core::ResourceStates::ShaderResource);
    });
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
}

bool RendererSystem::materialPassDrawResourcesReady(const MeshResources& mesh)const{
#if defined(NWB_DEBUG)
    return mesh.valid() && m_instanceBuffer && m_meshViewBuffer && m_materialTypedBuffer;
#else
    static_cast<void>(mesh);
    return true;
#endif
}

u32 RendererSystem::meshDispatchFlags(
    const MeshResources& mesh,
    const MaterialPipelinePass::Enum pass,
    const bool twoSided,
    const bool meshletConeCullScaleSafe
)const{
    u32 flags = 0u;
    const bool meshletBoundsFresh = !mesh.runtimeMesh || mesh.dynamicMeshletBoundsFresh;
    const bool meshletConesFresh = !mesh.runtimeMesh || mesh.dynamicMeshletConesFresh;
    // Runtime mesh providers own dynamic culling policy; the renderer only consumes the published freshness flags.
    if(meshletBoundsFresh)
        flags |= ECSRenderDetail::s_MeshDispatchFlagMeshletFrustumCull;
    if(meshletConesFresh && pass == MaterialPipelinePass::Opaque && !twoSided && meshletConeCullScaleSafe)
        flags |= ECSRenderDetail::s_MeshDispatchFlagMeshletConeCull;
    return flags;
}

void RendererSystem::setMaterialPassDrawPushConstants(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const MeshResources& mesh
){
    const u32 dispatchFlags = meshDispatchFlags(
        mesh,
        context.pass,
        drawItem.pipelineKey.twoSided,
        drawItem.meshletConeCullScaleSafe
    );
    if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
        ECSRenderDetail::SetTransparentDrawPushConstants(
            context.commandList,
            mesh.meshletCount,
            drawItem.instanceIndex,
            context.viewportState,
            *context.avboitTargets,
            dispatchFlags
        );
        return;
    }

    ECSRenderDetail::SetShaderDrivenPushConstants(
        context.commandList,
        mesh.meshletCount,
        drawItem.instanceIndex,
        context.viewportState,
        dispatchFlags
    );
}

void RendererSystem::renderMeshMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
#if defined(NWB_DEBUG)
        if(!materialPassDrawResourcesReady(mesh) || !pipelineResources.meshletPipeline)
            return;
#endif
        if(!createMeshBindingSet(mesh))
            return;

        setMaterialPassCommonBufferStates(context.commandList, mesh);

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);
        meshletState.addBindingSet(mesh.meshBindingSet.get());
        if(context.passBindingSet)
            meshletState.addBindingSet(context.passBindingSet);

        context.commandList.setMeshletState(meshletState);

        setMaterialPassDrawPushConstants(context, drawItem, mesh);
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::MeshDispatch(), m_graphics.getDevice(), context.commandList);

            context.commandList.dispatchMesh(mesh.meshletCount);
        }
    });
}

void RendererSystem::renderComputeMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    if(drawItems.empty())
        return;
    if(!createEmulationViewResources())
        return;
#if defined(NWB_DEBUG)
    if(!m_meshViewBuffer || !m_emulationViewBindingSet)
        return;
#endif

    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(context.pass);
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
#if defined(NWB_DEBUG)
        if(!materialPassDrawResourcesReady(mesh) || !pipelineResources.computePipeline || !pipelineResources.emulationPipeline)
            return;
#endif
        if(!createComputeBindingSet(mesh))
            return;
#if defined(NWB_DEBUG)
        if(!mesh.computeBindingSet || !mesh.emulationVertexBuffer)
            return;
#endif

        setMaterialPassCommonBufferStates(context.commandList, mesh);
        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);

        Core::ComputeState computeState;
        computeState.setPipeline(pipelineResources.computePipeline.get());
        computeState.addBindingSet(mesh.computeBindingSet.get());

        context.commandList.setComputeState(computeState);

        ECSRenderDetail::SetShaderDrivenPushConstants(
            context.commandList,
            mesh.meshletCount,
            drawItem.instanceIndex,
            context.viewportState,
            meshDispatchFlags(
                mesh,
                context.pass,
                drawItem.pipelineKey.twoSided,
                drawItem.meshletConeCullScaleSafe
            )
        );
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::MeshDispatch(), m_graphics.getDevice(), context.commandList);

            context.commandList.dispatch(mesh.meshletCount);
        }

        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::VertexBuffer);

        Core::GraphicsState graphicsState;
        graphicsState.setPipeline(pipelineResources.emulationPipeline.get());
        graphicsState.setFramebuffer(context.framebuffer);
        graphicsState.setViewport(context.viewportState);
        graphicsState.addVertexBuffer(
            Core::VertexBufferBinding()
                .setBuffer(mesh.emulationVertexBuffer.get())
                .setSlot(0)
                .setOffset(0)
        );
        if(usesAvboit){
            graphicsState.addBindingSet(nullptr);
            graphicsState.addBindingSet(context.passBindingSet);
        }
        else{
            graphicsState.addBindingSet(m_emulationViewBindingSet.get());
            if(context.passBindingSet)
                graphicsState.addBindingSet(context.passBindingSet);
        }

        context.commandList.setGraphicsState(graphicsState);

        if(usesAvboit)
            setMaterialPassDrawPushConstants(context, drawItem, mesh);

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(mesh.meshletPrimitiveIndexCount);
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::Raster(), m_graphics.getDevice(), context.commandList);

            context.commandList.draw(drawArgs);
        }
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

