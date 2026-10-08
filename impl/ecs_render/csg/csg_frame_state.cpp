// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_system.h"

#include <impl/ecs_render/material/material_surface_lookup.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>

#include <core/ecs/world.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_scene/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_frame_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<CsgFrameStateCacheSignature> BuildCsgFrameStateCacheSignature(
    Core::ECS::World& world,
    const CsgShapeRegistry& shapeRegistry
){
    CsgFrameStateCacheSignature signature;
    signature.shapeRegistryRevision = shapeRegistry.revision();

    if(world.view<SkinnedCsgMeshComponent>().candidateCount() > 0u)
        return MakeUnexpected(Failure{});

    u64 contentHash = s_Fnv64OffsetBasis;
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
        return MakeUnexpected(Failure{});

    signature.contentHash = contentHash;
    return signature;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameState RendererCsgSystem::buildFrameState(
    Core::Alloc::ScratchArena& scratchArena,
    IMaterialSurfaceLookup& materialSurfaceLookup
){
    const auto signature = __hidden_csg_frame_state::BuildCsgFrameStateCacheSignature(m_world, m_csgShapeRegistry);
    const bool cacheable = signature.has_value();
    if(cacheable && m_csgState.m_frameStateCacheValid && m_csgState.m_frameStateCacheSignature == *signature)
        return m_csgState.m_frameStateCache;

    bool frameStateCacheable = cacheable;
    CsgFrameState state;
    auto finishFrameState = [&](const CsgFrameState& frameState) -> CsgFrameState{
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

    auto* ecsMeshSystemPtr = m_world.getSystem<NWB::Impl::MeshSystem>();
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
        const CsgReceiverComponent* receiver = resolvedReceiver->receiver;
        const CsgReceiverKind::Enum receiverKind = resolvedReceiver->receiverKind;

        const auto resolvedMeshResult = ecsMeshSystem.resolveRenderableMeshStatus(entity);
        if(!resolvedMeshResult){
            frameStateCacheable = false;
            continue;
        }
        const auto& resolvedMesh = *resolvedMeshResult;

        if(resolvedMesh.runtime)
            frameStateCacheable = false;
        const auto meshResult = resolvedMesh.runtime
            ? m_meshSystem.createRuntimeMeshResources(resolvedMesh.runtimeMesh)
            : m_meshSystem.createMeshResources(resolvedMesh.mesh)
        ;
        if(!meshResult){
            frameStateCacheable = false;
            continue;
        }
        MeshResources* const mesh = *meshResult;

        const auto materialInfoResult = materialSurfaceLookup.findMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult){
            frameStateCacheable = false;
            continue;
        }
        MaterialSurfaceInfo* const materialInfo = *materialInfoResult;

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

