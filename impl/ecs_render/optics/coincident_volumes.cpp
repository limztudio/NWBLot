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

[[nodiscard]] bool ValidCandidateLanes(SIMDVector positionVec, SIMDVector rotationVec, SIMDVector scaleVec){
    if(
        !VectorIsFinite(positionVec, VectorComponentMask::s_XYZ)
        || !VectorIsFinite(rotationVec, VectorComponentMask::s_XYZW)
        || !VectorIsFinite(scaleVec, VectorComponentMask::s_XYZ)
    )
        return false;
    // SIMD lanes own the zero/nonzero classification; scalar lane compares stay out of the validity path.
    const bool scaleNonzero = (VectorMoveMask(VectorEqual(scaleVec, VectorZero())) & VectorComponentMask::s_XYZ) == 0u;
    const bool rotationNonzero = (VectorMoveMask(VectorNotEqual(rotationVec, VectorZero())) & VectorComponentMask::s_XYZW) != 0u;
    return scaleNonzero && rotationNonzero
    ;
}

[[nodiscard]] bool ValidCandidate(const Candidate& candidate){ // beginner: Loads candidate storage once, validates on lanes.
    if(!candidate.entity.valid() || !candidate.mesh.valid() || !candidate.material.valid())
        return false;
    if(candidate.mutableTypedByteCount != 0u && !candidate.mutableTypedBytes)
        return false;
    return ValidCandidateLanes(LoadFloat(candidate.position), LoadFloat(candidate.rotation), LoadFloat(candidate.scale));
}

struct CandidateHasher{
    usize hashTransformLanes(SIMDVector positionLanes, SIMDVector rotationLanes, SIMDVector scaleLanes, usize hash)const{
        HashCombineFloat(hash, VectorGetX(positionLanes));
        HashCombineFloat(hash, VectorGetY(positionLanes));
        HashCombineFloat(hash, VectorGetZ(positionLanes));
        HashCombineFloat(hash, VectorGetX(rotationLanes));
        HashCombineFloat(hash, VectorGetY(rotationLanes));
        HashCombineFloat(hash, VectorGetZ(rotationLanes));
        HashCombineFloat(hash, VectorGetW(rotationLanes));
        HashCombineFloat(hash, VectorGetX(scaleLanes));
        HashCombineFloat(hash, VectorGetY(scaleLanes));
        HashCombineFloat(hash, VectorGetZ(scaleLanes));
        return hash;
    }

    usize operator()(const CandidatePointer candidate)const{ // beginner: Loads transform storage once, folds lanes into the hash.
        usize hash = Hasher<Name>{}(candidate->mesh.name());
        HashCombine(hash, candidate->material.name());
        HashCombine(hash, candidate->group);
        // SIMD loads own the lane gathers feeding the scalar hash folds.
        hash = hashTransformLanes(LoadFloat(candidate->position), LoadFloat(candidate->rotation), LoadFloat(candidate->scale), hash);
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
    bool equalTransformLanes(SIMDVector lhsPosition, SIMDVector lhsRotation, SIMDVector lhsScale, SIMDVector rhsPosition, SIMDVector rhsRotation, SIMDVector rhsScale)const{
        return Vector3Equal(lhsPosition, rhsPosition)
            && Vector4Equal(lhsRotation, rhsRotation)
            && Vector3Equal(lhsScale, rhsScale);
    }

    bool operator()(const CandidatePointer lhs, const CandidatePointer rhs)const{ // beginner: Loads both transforms once, compares on lanes.
        // SIMD lane compares own the transform equality; scalar lane compares stay out of the dedup path.
        if(
            lhs->mesh != rhs->mesh || lhs->material != rhs->material || lhs->group != rhs->group
            || !equalTransformLanes(LoadFloat(lhs->position), LoadFloat(lhs->rotation), LoadFloat(lhs->scale), LoadFloat(rhs->position), LoadFloat(rhs->rotation), LoadFloat(rhs->scale))
        )
            return false;
        // Shared groups assert preserved boundaries; otherwise all material bytes must agree.
        return lhs->group != NAME_NONE || (
            lhs->boundaryMode == rhs->boundaryMode && lhs->mediumPriority == rhs->mediumPriority
            && lhs->mutableTypedByteCount == rhs->mutableTypedByteCount
            && (lhs->mutableTypedByteCount == 0u
                || GLOBAL_MEMCMP(lhs->mutableTypedBytes, rhs->mutableTypedBytes, lhs->mutableTypedByteCount) == 0)
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
    const MeshSystem* meshSystemPtr = world.getSystem<MeshSystem>();
    if(!meshSystemPtr || rendererView.candidateCount() < 2u){
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
        if(!meshSystemPtr->resolveRenderableMesh(entity, mesh) || mesh.runtime || !mesh.mesh.valid())
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
        if(const auto* transformPtr = world.tryGetComponent<Scene::TransformComponent>(entity)){
            // SIMD lanes own the aligned transform copies; scalar lane extraction stays out of the gather path.
            StoreFloat(LoadFloat(transformPtr->position), candidate.position);
            StoreFloat(LoadFloat(transformPtr->rotation), candidate.rotation);
            StoreFloat(LoadFloat(transformPtr->scale), candidate.scale);
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
            GLOBAL_ASSERT(false);
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

