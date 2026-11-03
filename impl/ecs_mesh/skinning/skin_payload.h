// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <impl/assets_mesh/skin_types.adhd.h>
#include <impl/ecs_skeleton/runtime_helpers.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(NWB_SKINNED_MESH_SKIN_INFLUENCE_ALIGNMENT) MeshSkinningInfluenceGpu{
    Array<u32, NWB_SKINNED_MESH_SKIN_JOINT_WORD_COUNT> packedJoint{};
    Float4U weight{};
};
static_assert(sizeof(MeshSkinningInfluenceGpu) == NWB_SKINNED_MESH_SKIN_INFLUENCE_BYTE_SIZE, "MeshSkinning influence GPU layout drifted");
static_assert(alignof(MeshSkinningInfluenceGpu) == NWB_SKINNED_MESH_SKIN_INFLUENCE_ALIGNMENT, "MeshSkinning influence GPU alignment must match the packed uint2");
static_assert(offsetof(MeshSkinningInfluenceGpu, packedJoint) == 0u, "MeshSkinning packed joints must lead each GPU record");
static_assert(offsetof(MeshSkinningInfluenceGpu, weight) == NWB_SKINNED_MESH_SKIN_WEIGHT_BYTE_OFFSET, "MeshSkinning FP32 weights must follow the packed joints");

// Keep the full current pose for validation and deformation history; the graph owns its upload copy.
struct RuntimeSkinPayloadScratch final{
    Vector<SkeletonJointMatrix, Core::Alloc::ScratchArena> poseJoints;
    Vector<SkeletonJointMatrix, Core::Alloc::ScratchArena> jointMatrices;
    usize skinInfluenceCount = 0u;
    u32 resolvedSkinningMode = SkeletonSkinningMode::LinearBlend;


public:
    explicit RuntimeSkinPayloadScratch(Core::Alloc::ScratchArena& scratchArena)noexcept
        : poseJoints(scratchArena)
        , jointMatrices(scratchArena)
    {}


public:
    [[nodiscard]] b8 hasActiveSkin()const noexcept{
        return skinInfluenceCount != 0u && !jointMatrices.empty();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshSkinningPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Encode mesh-owned data only when creating its GPU buffer.
[[nodiscard]] inline Expected<Vector<MeshSkinningInfluenceGpu, Core::Alloc::ScratchArena>> BuildSkinInfluences(const MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& arena){
    Vector<MeshSkinningInfluenceGpu, Core::Alloc::ScratchArena> influences(arena);
    if(instance.skin.empty())
        return influences;
    if(!CanBuildMeshSkinningInfluences(instance.skeletonJointCount, instance.skin.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skin influence counts are invalid"), instance.handle.value);
        return MakeUnexpected(Failure{});
    }

    influences.reserve(instance.skin.size());
    for(usize vertexIndex = 0u; vertexIndex < instance.skin.size(); ++vertexIndex){
        const SkinInfluence4& sourceSkin = instance.skin[vertexIndex];
        if(!ValidateMeshSkinningInfluenceRecord(instance.handle.value, vertexIndex, instance.skeletonJointCount, sourceSkin))
            return MakeUnexpected(Failure{});

        MeshSkinningInfluenceGpu gpuSkin;
        gpuSkin.packedJoint = PackSkinInfluenceJoints(sourceSkin);
        gpuSkin.weight = sourceSkin.weight;
        influences.push_back(gpuSkin);
    }
    return influences;
}

// Source validation and deformation snapshots keep every row; GPU reads only joints referenced by this Skin.
[[nodiscard]] inline Expected<Span<const SkeletonJointMatrix>> ResolveGpuJointPalette(const MeshSkinningRuntimeInstance& instance, const RuntimeSkinPayloadScratch& payload)noexcept{
    const usize jointCount = instance.skeletonJointCount;
    if(!CanResolveGpuJointPalette(payload.hasActiveSkin(), payload.skinInfluenceCount, instance.skin.size(), jointCount, payload.jointMatrices.size()))
        return MakeUnexpected(Failure{});
    return Span<const SkeletonJointMatrix>(payload.jointMatrices.data(), jointCount);
}

template<typename JointPaletteVector>
[[nodiscard]] b8 BuildSkinJointPalette(const MeshSkinningRuntimeInstance& instance, const Span<const SkeletonJointMatrix> sourceJoints, const u32 skinningMode, JointPaletteVector& outJointPalette){
    outJointPalette.clear();

    if(instance.skin.empty() || sourceJoints.empty())
        return true;
    if(!ValidateSkinJointPaletteInputs(
        instance.handle.value,
        instance.skeletonJointCount,
        instance.skin.size(),
        sourceJoints.size(),
        instance.inverseBindMatrices.size(),
        skinningMode
    ))
        return false;

    const usize jointCount = sourceJoints.size();
    const b8 useDualQuaternionPayload = skinningMode == SkeletonSkinningMode::DualQuaternion;
    const b8 hasInverseBindMatrices = !instance.inverseBindMatrices.empty();
    outJointPalette.reserve(jointCount);

    for(usize jointIndex = 0u; jointIndex < jointCount; ++jointIndex){
        const SIMDMatrix inverseBindMatrix = hasInverseBindMatrices ? LoadFloat(instance.inverseBindMatrices[jointIndex]) : SIMDMatrix{};

        const auto jointMatrix = SkeletonRuntime::ResolveSkinningJointMatrix(LoadFloat(sourceJoints[jointIndex]), hasInverseBindMatrices, inverseBindMatrix, s_SkinValidationEpsilon);
        if(!jointMatrix){
            outJointPalette.clear();
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint palette entry {} is not a finite invertible affine matrix"), instance.handle.value, jointIndex);
            return false;
        }

        SkeletonJointMatrix storedJointMatrix{};
        if(!useDualQuaternionPayload)
            StoreFloat(*jointMatrix, storedJointMatrix);
        else{
            const auto dualQuaternion = MatrixTryBuildRigidDualQuaternion(*jointMatrix, s_SkeletonAffineEpsilon, s_SkeletonRigidJointEpsilon);
            if(dualQuaternion){
                StoreFloat(dualQuaternion->real, storedJointMatrix.rows[0]);
                StoreFloat(dualQuaternion->dual, storedJointMatrix.rows[1]);
            }
            else{
                outJointPalette.clear();
                NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' joint palette entry {} failed dual-quaternion payload build"), instance.handle.value, jointIndex);
                return false;
            }
        }
        outJointPalette.push_back(storedJointMatrix);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 BuildRuntimeSkinPayload(const MeshSkinningRuntimeInstance& instance, const SkeletonJointPaletteComponent* jointPalette, const SkeletonPoseComponent* skeletonPose, RuntimeSkinPayloadScratch& payload){
    payload.skinInfluenceCount = 0u;
    payload.jointMatrices.clear();
    payload.poseJoints.clear();
    payload.resolvedSkinningMode = jointPalette ? jointPalette->skinningMode : SkeletonSkinningMode::LinearBlend;
    if(skeletonPose && HasSkeletonPoseData(*skeletonPose)){
        const auto skinningMode = SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(*skeletonPose, payload.poseJoints);
        if(!skinningMode){
            payload.poseJoints.clear();
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: runtime mesh '{}' skeleton pose is invalid"), instance.handle.value);
            return false;
        }
        payload.resolvedSkinningMode = *skinningMode;
        if(!BuildSkinJointPalette(instance, payload.poseJoints, payload.resolvedSkinningMode, payload.jointMatrices)){
            payload.poseJoints.clear();
            return false;
        }
    }
    else if(jointPalette && !BuildSkinJointPalette(instance, jointPalette->joints, payload.resolvedSkinningMode, payload.jointMatrices))
        return false;

    if(!payload.jointMatrices.empty())
        payload.skinInfluenceCount = instance.skin.size();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

