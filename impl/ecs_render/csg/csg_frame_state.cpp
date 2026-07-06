// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_frame_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool BuildCsgFrameStateCacheSignature(
    Core::ECS::World& world,
    CsgFrameStateCacheSignature& outSignature
){
    outSignature = CsgFrameStateCacheSignature{};

    if(world.view<SkinnedCsgMeshComponent>().candidateCount() > 0u)
        return false;

    u64 contentHash = FNV64_OFFSET_BASIS;
    auto cutterView = world.view<CsgCutterComponent>();
    cutterView.each(
        [&](const Core::ECS::EntityID entity, CsgCutterComponent& cutter){
            Fnv64AppendValue(contentHash, entity.id);
            Fnv64AppendBool(contentHash, cutter.active);
            Fnv64AppendValue(contentHash, cutter.receiverGroup.hash());
            Fnv64AppendValue(contentHash, cutter.shapeType.hash());
            Fnv64AppendValue(contentHash, cutter.worldToShape);
            Fnv64AppendValue(contentHash, cutter.shapeToWorld);
            Fnv64AppendBuffer(contentHash, cutter.parameterBytes.data(), cutter.parameterBytes.size());
        }
    );

    auto receiverView = world.view<StaticCsgMeshComponent>();
    bool cacheable = true;
    receiverView.each(
        [&](const Core::ECS::EntityID entity, StaticCsgMeshComponent& typedReceiver){
            const CsgReceiverComponent& receiver = typedReceiver;
            Fnv64AppendValue(contentHash, entity.id);
            Fnv64AppendBool(contentHash, receiver.enabled);
            Fnv64AppendBool(contentHash, receiver.affectOpaquePass);
            Fnv64AppendBool(contentHash, receiver.affectTransparentPass);
            Fnv64AppendValue(contentHash, receiver.receiverGroup.hash());

            const RendererComponent* renderer = world.tryGetComponent<RendererComponent>(entity);
            Fnv64AppendBool(contentHash, renderer != nullptr);
            if(renderer){
                Fnv64AppendBool(contentHash, renderer->visible);
                Fnv64AppendValue(contentHash, renderer->material.name().hash());
            }

            const MeshComponent* mesh = world.tryGetComponent<MeshComponent>(entity);
            Fnv64AppendBool(contentHash, mesh != nullptr);
            if(mesh){
                Fnv64AppendValue(contentHash, mesh->mesh.name().hash());
            }
            else if(receiver.enabled && renderer){
                cacheable = false;
            }

            const Scene::TransformComponent* transform = world.tryGetComponent<Scene::TransformComponent>(entity);
            Fnv64AppendBool(contentHash, transform != nullptr);
            if(transform){
                Fnv64AppendValue(contentHash, transform->position);
                Fnv64AppendValue(contentHash, transform->rotation);
                Fnv64AppendValue(contentHash, transform->scale);
            }
        }
    );
    if(!cacheable)
        return false;

    outSignature.contentHash = contentHash;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameState RendererCsgSystem::buildFrameState(Core::Alloc::ScratchArena& scratchArena){
    CsgFrameStateCacheSignature signature;
    const bool cacheable = __hidden_csg_frame_state::BuildCsgFrameStateCacheSignature(world(), signature);
    if(cacheable && csgState().m_frameStateCacheValid && csgState().m_frameStateCacheSignature == signature)
        return csgState().m_frameStateCache;

    bool frameStateCacheable = cacheable;
    CsgFrameState state;
    auto finishFrameState = [&](const CsgFrameState& frameState) -> CsgFrameState{
        if(frameStateCacheable){
            csgState().m_frameStateCacheSignature = signature;
            csgState().m_frameStateCache = frameState;
            csgState().m_frameStateCacheValid = true;
        }
        else{
            csgState().m_frameStateCacheValid = false;
        }
        return frameState;
    };

    CsgFrameReceiverLookup receiverLookup(world(), scratchArena);
    if(receiverLookup.empty())
        return finishFrameState(state);

    auto* ecsMeshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!ecsMeshSystem){
        frameStateCacheable = false;
        return finishFrameState(state);
    }

    auto rendererView = world().view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        CsgReceiverKind::Enum receiverKind = CsgReceiverKind::Static;
        const CsgReceiverComponent* receiver = ResolveCsgReceiverComponent(world(), entity, receiverKind);
        if(!receiver || !receiver->enabled)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!ecsMeshSystem->resolveRenderableMesh(entity, resolvedMesh)){
            frameStateCacheable = false;
            continue;
        }

        MeshResources* mesh = nullptr;
        if(resolvedMesh.runtime){
            frameStateCacheable = false;
            if(!m_renderer.meshSystem().createRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh))
                continue;
        }
        else if(!m_renderer.meshSystem().createMeshResources(resolvedMesh.mesh, mesh)){
            frameStateCacheable = false;
            continue;
        }
        NWB_ASSERT(mesh);

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo)){
            frameStateCacheable = false;
            continue;
        }
        NWB_ASSERT(materialInfo);

        const Scene::TransformComponent* transform = world().tryGetComponent<Scene::TransformComponent>(entity);

        auto countPassCutters = [&](const CsgReceiverPass::Enum pass) -> u32{
            CsgReceiverDrawState drawState;
            if(!receiverLookup.resolveReceiverDrawState(entity, pass, drawState) || drawState.receiverKind != receiverKind)
                return 0u;

            CsgReceiverClipDrawInfo clipInfo;
            if(!resolveCsgReceiverClipDrawInfo(receiverLookup, entity, mesh->csgLocalBounds, transform, clipInfo))
                return 0u;
            if(clipInfo.cutterCount == 0u)
                return 0u;

            return clipInfo.cutterCount;
        };

        const u32 opaqueCutterCount =
            !materialInfo->transparent && receiver->affectOpaquePass
            ? countPassCutters(CsgReceiverPass::Opaque)
            : 0u
        ;
        const u32 transparentCutterCount =
            materialInfo->transparent && receiver->affectTransparentPass
            ? countPassCutters(CsgReceiverPass::Transparent)
            : 0u
        ;
        const bool opaqueWork = opaqueCutterCount > 0u;
        const bool transparentWork = transparentCutterCount > 0u;
        if(!opaqueWork && !transparentWork)
            continue;

        AddCsgFrameReceiverWork(
            state,
            receiverKind,
            opaqueWork,
            transparentWork,
            Max(opaqueCutterCount, transparentCutterCount)
        );
    }

    FinalizeCsgFrameState(state);
    return finishFrameState(state);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

