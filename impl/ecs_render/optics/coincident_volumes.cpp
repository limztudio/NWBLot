// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "coincident_volumes.h"

#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_csg/components.adhd.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_scene/components.adhd.h>

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererOpticalVolumeSelection::prepare(Core::ECS::World& world, RendererMaterialSystem& materials, Core::Alloc::ScratchArena& scratchArena){
    auto rendererView = world.view<RendererComponent>();
    const MeshSystem* meshSystemPtr = world.getSystem<MeshSystem>();
    if(!meshSystemPtr || rendererView.candidateCount() < s_MinCoincidentCandidateCount){
        reset();
        return;
    }

    // Count opt-ins first; opaque-heavy worlds need no candidate allocation.
    usize candidateCapacity = 0u;
    for(auto&& [entity, renderer] : rendererView){
        if(RendererComponentOpticalVolumeMergingEnabled(renderer))
            ++candidateCapacity;
    }
    if(candidateCapacity < s_MinCoincidentCandidateCount){
        reset();
        return;
    }
    Vector<CoincidentOpticalVolumeCandidate, Core::Alloc::ScratchArena> candidates(scratchArena);
    candidates.reserve(candidateCapacity);
    for(auto&& [entity, renderer] : rendererView){
        if(!RendererComponentOpticalVolumeMergingEnabled(renderer))
            continue;
        const auto materialResult = materials.findMaterialSurfaceInfo(renderer.material);
        const b8 closed = OpticalBoundaryModeIsClosed(renderer.opticalBoundaryMode);
        if(!materialResult)
            continue;
        const NotNull<MaterialSurfaceInfo*> material = *materialResult;
        if(!material->transparent || (!material->refractive && !closed))
            continue;

        const auto meshResult = meshSystemPtr->resolveRenderableMeshStatus(entity);
        if(!meshResult || meshResult->runtime || !meshResult->mesh.valid())
            continue;
        const auto& mesh = *meshResult;
        // Runtime providers and CSG can change boundaries; skip them here.
        if(world.tryGetComponent<StaticCsgMeshComponent>(entity) || world.tryGetComponent<SkinnedCsgMeshComponent>(entity) || world.tryGetComponent<CsgReceiverComponent>(entity))
            continue;

        const auto* materialInstance = world.tryGetComponent<MaterialInstanceComponent>(entity);
        const auto mutableBytes = materials.findPreparedMaterialInstanceMutableTypedBytes(entity, *material, materialInstance);
        if(!mutableBytes)
            continue;

        CoincidentOpticalVolumeCandidate candidate = MakeCoincidentOpticalVolumeCandidate(entity, mesh.mesh, renderer, *mutableBytes);
        if(const auto* transformPtr = world.tryGetComponent<Scene::TransformComponent>(entity)){
            candidate.position = transformPtr->position;
            candidate.rotation = transformPtr->rotation;
            candidate.scale = transformPtr->scale;
        }
        candidates.push_back(candidate);
    }
    select(candidates, scratchArena);
}

void RendererOpticalVolumeSelection::select(const Span<const CoincidentOpticalVolumeCandidate> candidates, [[maybe_unused]] Core::Alloc::ScratchArena& scratchArena){
    SelectSuppressedCoincidentOpticalVolumes(candidates, m_suppressed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
