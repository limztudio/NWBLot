// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"

#include <impl/ecs_render/material/material_surface_lookup.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_scene/module.h>

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameState RendererCsgSystem::buildFrameState(Core::Alloc::ScratchArena& scratchArena, IMaterialSurfaceLookup& materialSurfaceLookup){
    auto* ecsMeshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
    Expected<CsgFrameStateCacheSignature> signature = MakeUnexpected(Failure{});
    if(ecsMeshSystemPtr)
        signature = CsgFrameCacheDetail::BuildSignature(m_world, m_csgShapeRegistry, *ecsMeshSystemPtr);
    const bool cacheable = signature.has_value();
    if(cacheable && m_csgState.m_frameStateCacheValid && m_csgState.m_frameStateCacheSignature == *signature)
        return m_csgState.m_frameStateCache;

    bool frameStateCacheable = cacheable;
    CsgFrameState state;
    auto finishFrameState = [&](const CsgFrameState& frameState)noexcept -> CsgFrameState{
        if(frameStateCacheable){
            m_csgState.m_frameStateCacheSignature = *signature;
            m_csgState.m_frameStateCache = frameState;
            m_csgState.m_frameStateCacheValid = true;
        }
        else{
            m_csgState.m_frameStateCacheValid = false;
        }
        return frameState;
    };

    CsgFrameReceiverLookup receiverLookup(m_world, scratchArena);
    if(receiverLookup.empty())
        return finishFrameState(state);

    if(!ecsMeshSystemPtr){
        frameStateCacheable = false;
        return finishFrameState(state);
    }
    NWB::Impl::MeshSystem& ecsMeshSystem = *ecsMeshSystemPtr;

    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        const auto resolvedReceiver = ResolveCsgReceiverComponent(m_world, entity);
        if(!resolvedReceiver || !resolvedReceiver->receiver->enabled)
            continue;
        const NotNull<const CsgReceiverComponent*> receiver = resolvedReceiver->receiver;
        const CsgReceiverKind::Enum receiverKind = resolvedReceiver->receiverKind;

        const auto resolvedMeshResult = ecsMeshSystem.resolveRenderableMeshStatus(entity);
        if(!resolvedMeshResult){
            frameStateCacheable = false;
            continue;
        }
        const auto& resolvedMesh = *resolvedMeshResult;

        if(resolvedMesh.runtime)
            frameStateCacheable = false;
        const auto meshResult = resolvedMesh.runtime ? m_meshSystem.createRuntimeMeshResources(resolvedMesh.runtimeMesh) : m_meshSystem.createMeshResources(resolvedMesh.mesh, scratchArena);
        if(!meshResult){
            frameStateCacheable = false;
            continue;
        }
        const NotNull<MeshResources*> mesh = *meshResult;

        const auto materialInfoResult = materialSurfaceLookup.findMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult){
            frameStateCacheable = false;
            continue;
        }
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;

        const Scene::TransformComponent* transform = m_world.tryGetComponent<Scene::TransformComponent>(entity);

        auto countPassCutters = [&](const CsgReceiverPass::Enum pass) -> u32{
            const auto drawState = receiverLookup.resolveReceiverDrawState(entity, pass);
            if(!drawState || drawState->receiverKind != receiverKind)
                return 0u;

            const auto clipInfo = resolveCsgReceiverClipDrawInfo(receiverLookup, *drawState, mesh->csgLocalBounds, transform);
            if(!clipInfo)
                return 0u;
            if(clipInfo->cutterCount == 0u)
                return 0u;

            return clipInfo->cutterCount;
        };

        const u32 opaqueCutterCount = !materialInfo->transparent && receiver->affectOpaquePass ? countPassCutters(CsgReceiverPass::Opaque) : 0u;
        const u32 transparentCutterCount = materialInfo->transparent && receiver->affectTransparentPass ? countPassCutters(CsgReceiverPass::Transparent) : 0u;
        const bool opaqueWork = opaqueCutterCount > 0u;
        const bool transparentWork = transparentCutterCount > 0u;
        if(!opaqueWork && !transparentWork)
            continue;

        AddCsgFrameReceiverWork(state, receiverKind, opaqueWork, transparentWork, Max(opaqueCutterCount, transparentCutterCount));
    }

    FinalizeCsgFrameState(state);
    return finishFrameState(state);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

