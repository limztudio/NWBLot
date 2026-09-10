// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "coincident_volumes.h"

#include <impl/ecs_render/components.h>
#include <impl/ecs_render/material/material_system.h>

#include <core/ecs/world.h>
#include <impl/ecs_csg/components.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_scene/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_coincident_volumes{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Candidate = CoincidentOpticalVolumeCandidate;
using CandidatePointer = NotNull<const Candidate*>;

[[nodiscard]] bool ValidCandidate(const Candidate& candidate){
    if(!candidate.entity.valid() || !candidate.mesh.valid() || !candidate.material.valid())
        return false;
    if(candidate.mutableTypedByteCount != 0u && !candidate.mutableTypedBytes)
        return false;
    if(
        !VectorIsFinite(LoadFloat(candidate.position), VectorComponentMask::s_XYZ)
        || !VectorIsFinite(LoadFloat(candidate.rotation), VectorComponentMask::s_XYZW)
        || !VectorIsFinite(LoadFloat(candidate.scale), VectorComponentMask::s_XYZ)
    )
        return false;
    return candidate.scale.x != 0.f && candidate.scale.y != 0.f && candidate.scale.z != 0.f
        && (candidate.rotation.x != 0.f || candidate.rotation.y != 0.f || candidate.rotation.z != 0.f || candidate.rotation.w != 0.f)
    ;
}

struct CandidateHasher{
    usize operator()(const CandidatePointer candidate)const{
        usize hash = Hasher<Name>{}(candidate->mesh.name());
        HashCombine(hash, candidate->material.name());
        HashCombine(hash, candidate->group);
        for(const f32 value : {
            candidate->position.x, candidate->position.y, candidate->position.z,
            candidate->rotation.x, candidate->rotation.y, candidate->rotation.z, candidate->rotation.w,
            candidate->scale.x, candidate->scale.y, candidate->scale.z
        })
            HashCombineFloat(hash, value);
        if(candidate->group == NAME_NONE){
            HashCombine(hash, candidate->boundaryMode);
            HashCombine(hash, candidate->mediumPriority);
            HashCombine(hash, candidate->mutableTypedByteCount);
            HashCombine(hash, ComputeFnv64Bytes(candidate->mutableTypedBytes, candidate->mutableTypedByteCount));
        }
        return hash;
    }
};

struct CandidateEqual{
    bool operator()(const CandidatePointer lhs, const CandidatePointer rhs)const{
        if(
            lhs->mesh != rhs->mesh || lhs->material != rhs->material || lhs->group != rhs->group
            || lhs->position.x != rhs->position.x || lhs->position.y != rhs->position.y || lhs->position.z != rhs->position.z
            || lhs->rotation.x != rhs->rotation.x || lhs->rotation.y != rhs->rotation.y
            || lhs->rotation.z != rhs->rotation.z || lhs->rotation.w != rhs->rotation.w
            || lhs->scale.x != rhs->scale.x || lhs->scale.y != rhs->scale.y || lhs->scale.z != rhs->scale.z
        )
            return false;
        // Shared groups assert preserved boundaries; otherwise all material bytes must agree.
        return lhs->group != NAME_NONE || (
            lhs->boundaryMode == rhs->boundaryMode && lhs->mediumPriority == rhs->mediumPriority
            && lhs->mutableTypedByteCount == rhs->mutableTypedByteCount
            && (lhs->mutableTypedByteCount == 0u
                || NWB_MEMCMP(lhs->mutableTypedBytes, rhs->mutableTypedBytes, lhs->mutableTypedByteCount) == 0)
        );
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererOpticalVolumeSelection::RendererOpticalVolumeSelection(Core::Alloc::GlobalArena& arena)
    : m_suppressed(0u, Hasher<Core::ECS::EntityID>{}, EqualTo<Core::ECS::EntityID>{}, arena)
{}

void RendererOpticalVolumeSelection::prepare(
    Core::ECS::World& world,
    RendererMaterialSystem& materials,
    Core::Alloc::ScratchArena& scratchArena){
    auto rendererView = world.view<RendererComponent>();
    const MeshSystem* meshSystem = world.getSystem<MeshSystem>();
    if(!meshSystem || rendererView.candidateCount() < 2u){
        reset();
        return;
    }

    const auto mergingEnabled = [](const RendererComponent& renderer){
        return renderer.visible && (
            renderer.opticalVolumeCoincidence == OpticalVolumeCoincidence::IdenticalMaterial
            || (renderer.opticalVolumeCoincidence == OpticalVolumeCoincidence::SharedGroup && renderer.opticalVolumeGroup != NAME_NONE)
        );
    };
    // Count opt-ins first; opaque-heavy worlds need no candidate allocation.
    usize candidateCapacity = 0u;
    for(auto&& [entity, renderer] : rendererView){
        if(mergingEnabled(renderer))
            ++candidateCapacity;
    }
    if(candidateCapacity < 2u){
        reset();
        return;
    }
    Vector<CoincidentOpticalVolumeCandidate, Core::Alloc::ScratchArena> candidates(scratchArena);
    candidates.reserve(candidateCapacity);
    for(auto&& [entity, renderer] : rendererView){
        if(!mergingEnabled(renderer))
            continue;
        MaterialSurfaceInfo* material = nullptr;
        const bool closed = renderer.opticalBoundaryMode == OpticalBoundaryMode::ClosedNested
            || renderer.opticalBoundaryMode == OpticalBoundaryMode::ClosedPriority;
        if(!materials.findMaterialSurfaceInfo(renderer.material, material) || !material->transparent || (!material->refractive && !closed))
            continue;

        RenderableMeshDesc mesh;
        if(!meshSystem->resolveRenderableMesh(entity, mesh) || mesh.runtime || !mesh.mesh.valid())
            continue;
        // Runtime providers and CSG can change boundaries; skip them here.
        if(
            world.tryGetComponent<StaticCsgMeshComponent>(entity)
            || world.tryGetComponent<SkinnedCsgMeshComponent>(entity)
            || world.tryGetComponent<CsgReceiverComponent>(entity)
        )
            continue;

        const auto* materialInstance = world.tryGetComponent<MaterialInstanceComponent>(entity);
        const MaterialTypedByteVector* mutableBytes = nullptr;
        if(!materials.findPreparedMaterialInstanceMutableTypedBytes(entity, *material, materialInstance, mutableBytes))
            continue;

        CoincidentOpticalVolumeCandidate candidate;
        candidate.entity = entity;
        candidate.mesh = mesh.mesh;
        candidate.material = renderer.material;
        candidate.group = renderer.opticalVolumeCoincidence == OpticalVolumeCoincidence::SharedGroup
            ? renderer.opticalVolumeGroup : NAME_NONE;
        candidate.priority = renderer.opticalVolumePriority;
        candidate.boundaryMode = static_cast<u32>(renderer.opticalBoundaryMode);
        candidate.mediumPriority = renderer.opticalMediumPriority;
        candidate.mutableTypedBytes = mutableBytes->data();
        candidate.mutableTypedByteCount = mutableBytes->size();
        if(const auto* transform = world.tryGetComponent<Scene::TransformComponent>(entity)){
            candidate.position = Float3U(transform->position.x, transform->position.y, transform->position.z);
            candidate.rotation = Float4U(transform->rotation.x, transform->rotation.y, transform->rotation.z, transform->rotation.w);
            candidate.scale = Float3U(transform->scale.x, transform->scale.y, transform->scale.z);
        }
        candidates.push_back(candidate);
    }
    select(candidates.data(), candidates.size(), scratchArena);
}

void RendererOpticalVolumeSelection::select(
    const CoincidentOpticalVolumeCandidate* candidates,
    const usize candidateCount,
    Core::Alloc::ScratchArena& scratchArena){
    reset();
    if(!candidates || candidateCount < 2u)
        return;

    using namespace __hidden_coincident_volumes;
    using RepresentativeMap = HashMap<CandidatePointer, CandidatePointer, CandidateHasher, CandidateEqual, Core::Alloc::ScratchArena>;
    RepresentativeMap representatives(0u, CandidateHasher{}, CandidateEqual{}, scratchArena);
    representatives.reserve(candidateCount);
    // Inspect membership each frame; components may change without structural mutation.
    for(usize index = 0u; index < candidateCount; ++index){
        const Candidate& candidate = candidates[index];
        if(!ValidCandidate(candidate))
            continue;
        const CandidatePointer current(&candidate);
        auto [entry, inserted] = representatives.emplace(current, current);
        if(inserted)
            continue;

        const Candidate& previous = *entry.value();
        const bool replace = candidate.priority > previous.priority
            || (candidate.priority == previous.priority && candidate.entity < previous.entity);
        const Core::ECS::EntityID suppressed = replace ? previous.entity : candidate.entity;
        if(replace)
            entry.value() = current;
        if(!m_suppressed.insert(suppressed).second){
            NWB_ASSERT(false);
            continue;
        }
    }
}

void RendererOpticalVolumeSelection::reset(){
    m_suppressed.clear();
}

bool RendererOpticalVolumeSelection::isSuppressed(const Core::ECS::EntityID entity)const{
    return !m_suppressed.empty() && m_suppressed.contains(entity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

