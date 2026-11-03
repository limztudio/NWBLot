// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "coincident_volumes.h"

#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/optics/types.adhd.h>
#include <impl/ecs_csg/components.adhd.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_scene/components.adhd.h>

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_coincident_volumes{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Candidate = CoincidentOpticalVolumeCandidate;
using CandidatePointer = NotNull<const Candidate*>;

struct CandidateHasher{
public:
    usize operator()(const CandidatePointer candidate)const noexcept{
        return HashCoincidentOpticalVolumeCandidate(*candidate);
    }
};

struct CandidateEqual{
public:
    b8 operator()(const CandidatePointer lhs, const CandidatePointer rhs)const noexcept{
        return EqualCoincidentOpticalVolumeCandidates(*lhs, *rhs);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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

        CoincidentOpticalVolumeCandidate candidate;
        candidate.entity = entity;
        candidate.mesh = mesh.mesh;
        candidate.material = renderer.material;
        candidate.group = renderer.opticalVolumeCoincidence == OpticalVolumeCoincidence::SharedGroup ? renderer.opticalVolumeGroup : s_NameNone;
        candidate.priority = renderer.opticalVolumePriority;
        candidate.boundaryMode = static_cast<u32>(renderer.opticalBoundaryMode);
        candidate.mediumPriority = renderer.opticalMediumPriority;
        candidate.mutableTypedBytes = *mutableBytes;
        if(const auto* transformPtr = world.tryGetComponent<Scene::TransformComponent>(entity)){
            candidate.position = transformPtr->position;
            candidate.rotation = transformPtr->rotation;
            candidate.scale = transformPtr->scale;
        }
        candidates.push_back(candidate);
    }
    select(candidates, scratchArena);
}

void RendererOpticalVolumeSelection::select(const Span<const CoincidentOpticalVolumeCandidate> candidates, Core::Alloc::ScratchArena& scratchArena){
    reset();
    const usize candidateCount = candidates.size();
    if(candidateCount < s_MinCoincidentCandidateCount)
        return;

    using namespace __hidden_coincident_volumes;
    using RepresentativeMap = HashMap<CandidatePointer, CandidatePointer, Core::Alloc::ScratchArena, CandidateHasher, CandidateEqual>;
    RepresentativeMap representatives(0u, CandidateHasher{}, CandidateEqual{}, scratchArena);
    representatives.reserve(candidateCount);
    // Inspect membership each frame; components may change without structural mutation.
    for(usize index = 0u; index < candidateCount; ++index){
        const Candidate& candidate = candidates[index];
        if(!ValidCoincidentOpticalVolumeCandidate(candidate))
            continue;
        const CandidatePointer current(&candidate);
        auto [entry, inserted] = representatives.emplace(current, current);
        if(inserted)
            continue;

        const Candidate& previous = *entry.value();
        const b8 replace = CoincidentOpticalVolumeCandidateOutranks(candidate, previous);
        const Core::ECS::EntityID suppressed = replace ? previous.entity : candidate.entity;
        if(replace)
            entry.value() = current;
        if(!m_suppressed.insert(suppressed).second){
            NWB_ASSERT(false);
            continue;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

