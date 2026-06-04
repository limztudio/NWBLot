// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameState RendererCsgSystem::buildFrameState(Core::Alloc::ScratchArena& scratchArena){
    CsgFrameState state;
    CsgFrameReceiverLookup receiverLookup(world(), scratchArena);
    if(receiverLookup.empty())
        return state;

    auto* ecsMeshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!ecsMeshSystem)
        return state;

    auto rendererView = world().view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        CsgReceiverKind::Enum receiverKind = CsgReceiverKind::Static;
        const CsgReceiverComponent* receiver = ResolveCsgReceiverComponent(world(), entity, receiverKind);
        if(!receiver || !receiver->enabled)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!ecsMeshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        if(resolvedMesh.runtime){
            if(!m_renderer.meshSystem().createRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh))
                continue;
        }
        else if(!m_renderer.meshSystem().createMeshResources(resolvedMesh.mesh, mesh))
            continue;
        NWB_ASSERT(mesh);

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!m_renderer.materialSystem().createMaterialSurfaceInfo(renderer.material, materialInfo))
            continue;
        NWB_ASSERT(materialInfo);

        const Scene::TransformComponent* transform = world().tryGetComponent<Scene::TransformComponent>(entity);

        auto countPassCutters = [&](const CsgReceiverPass::Enum pass) -> u32{
            CsgReceiverDrawState drawState;
            if(!receiverLookup.resolveReceiverDrawState(entity, pass, drawState) || drawState.receiverKind != receiverKind)
                return 0u;

            const u32 cutterCount = countCsgReceiverClipCutters(receiverLookup, entity, mesh->csgLocalBounds, transform);
            if(cutterCount == 0u)
                return 0u;

            Name evaluatorVariant = NAME_NONE;
            if(!resolveCsgReceiverEvaluatorVariant(receiverLookup, entity, mesh->csgLocalBounds, transform, evaluatorVariant))
                return 0u;

            return cutterCount;
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
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
