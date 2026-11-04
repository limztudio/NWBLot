// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin.h"

#include <core/common/log.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FbxSkinDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDMatrixDouble LoadUfbxMatrix(const ufbx_matrix& matrix)noexcept{
    return MakeUfbxMatrixDouble(
        static_cast<f64>(matrix.m00), static_cast<f64>(matrix.m01), static_cast<f64>(matrix.m02), static_cast<f64>(matrix.m03),
        static_cast<f64>(matrix.m10), static_cast<f64>(matrix.m11), static_cast<f64>(matrix.m12), static_cast<f64>(matrix.m13),
        static_cast<f64>(matrix.m20), static_cast<f64>(matrix.m21), static_cast<f64>(matrix.m22), static_cast<f64>(matrix.m23)
    );
}

Expected<::Vector<u16, Core::Alloc::ScratchArena>> BuildClusterJointMap(const MeshInstance& instance, const ImportOptions& options, const ufbx_skin_deformer& skin, Core::Alloc::ScratchArena& scratchArena, ExportContext& inOutContext){
    ::Vector<u16, Core::Alloc::ScratchArena> outClusterJoints(scratchArena);
    const Expected<usize> reservedJointCountResult = ValidateFbxSkinClusterCount(skin.clusters.count, inOutContext.joints.size());
    if(!reservedJointCountResult)
        return MakeUnexpected(Failure{});

    const usize reservedJointCount = *reservedJointCountResult;
    inOutContext.joints.reserve(reservedJointCount);
    inOutContext.bindPoseMatrices.reserve(reservedJointCount);
    inOutContext.inverseBindMatrices.reserve(reservedJointCount);
    outClusterJoints.reserve(skin.clusters.count);
    const f64 inverseScale = s_UniformScaleNumerator / options.scale;
    ufbx_matrix meshFromOutput = {};
    meshFromOutput.m00 = inverseScale;
    meshFromOutput.m11 = inverseScale;
    meshFromOutput.m22 = inverseScale;
    if(options.bakeTransforms){
        const ufbx_matrix nodeMeshToWorldInverse = ufbx_matrix_invert(&instance.node->geometry_to_world);
        meshFromOutput = ufbx_matrix_mul(&nodeMeshToWorldInverse, &meshFromOutput);
    }
    for(usize clusterIndex = 0u; clusterIndex < skin.clusters.count; ++clusterIndex){
        ufbx_skin_cluster* cluster = skin.clusters.data[clusterIndex];
        if(!cluster){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer contains a null cluster"));
            return MakeUnexpected(Failure{});
        }

        if(!cluster->bone_node){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin cluster is missing a bone node"));
            return MakeUnexpected(Failure{});
        }
        const ufbx_matrix inverseBind = ufbx_matrix_mul(&cluster->geometry_to_bone, &meshFromOutput);
        const Expected<SIMDMatrix> inverseBindValueResult = TryConvertFbxInverseBindMatrix(LoadUfbxMatrix(inverseBind), static_cast<f64>(ufbx_matrix_determinant(&inverseBind)));
        if(!inverseBindValueResult)
            return MakeUnexpected(Failure{});
        const SIMDMatrix inverseBindValue = *inverseBindValueResult;

        const ufbx_matrix bindPose = ufbx_matrix_invert(&inverseBind);
        const Expected<SIMDMatrix> bindPoseValueResult = TryConvertFbxBindPoseMatrix(LoadUfbxMatrix(bindPose));
        if(!bindPoseValueResult)
            return MakeUnexpected(Failure{});
        const SIMDMatrix bindPoseValue = *bindPoseValueResult;

        auto foundJoint = inOutContext.jointLookup.find(MakeNotNull(cluster->bone_node));
        if(foundJoint != inOutContext.jointLookup.end()){
            const usize jointIndex = static_cast<usize>(foundJoint.value());
            NWB_ASSERT(jointIndex < inOutContext.inverseBindMatrices.size());
            NWB_ASSERT(jointIndex < inOutContext.bindPoseMatrices.size());
            const SIMDMatrix existingInverseBind = LoadFloat(inOutContext.inverseBindMatrices[jointIndex]);
            if(!NearlyEqualFbxJointMatrices(existingInverseBind, inverseBindValue)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected meshes bind skeleton joint '{}' with different inverse bind matrices"), ConvertString(ResolveFbxNodeDisplayName(cluster->bone_node != nullptr, UfbxStringView(cluster->bone_node->name))));
                return MakeUnexpected(Failure{});
            }
            outClusterJoints.push_back(foundJoint.value());
            continue;
        }

        if(inOutContext.joints.size() >= s_MaxSkeletonJointCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skeleton has more than {} joints"), s_MaxSkeletonJointCount);
            return MakeUnexpected(Failure{});
        }

        JointMatrix storedInverseBind{};
        JointMatrix storedBindPose{};
        StoreFloat(inverseBindValue, storedInverseBind);
        StoreFloat(bindPoseValue, storedBindPose);
        const u16 joint = static_cast<u16>(inOutContext.joints.size());
        inOutContext.joints.push_back(cluster->bone_node);
        inOutContext.bindPoseMatrices.push_back(storedBindPose);
        inOutContext.inverseBindMatrices.push_back(storedInverseBind);
        const auto inserted = inOutContext.jointLookup.emplace(MakeNotNull(cluster->bone_node), joint);
        if(!inserted.second){
            NWB_ASSERT(false);
            return MakeUnexpected(Failure{});
        }
        outClusterJoints.push_back(joint);
    }

    return outClusterJoints;
}

Expected<MeshSkinInfluence> LoadInfluence(const ufbx_skin_deformer& skin, const Span<const u16> clusterJoints, const u32 logicalVertex){
    MeshSkinInfluence result;
    if(logicalVertex >= skin.vertices.count){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer does not contain weights for every logical vertex"));
        return MakeUnexpected(Failure{});
    }

    const ufbx_skin_vertex skinVertex = skin.vertices.data[logicalVertex];
    if(!ValidateFbxSkinVertexWeightRange(skinVertex.weight_begin, skinVertex.num_weights, skin.weights.count))
        return MakeUnexpected(Failure{});

    f64 weightSum = 0.0;
    u32 writtenInfluenceCount = 0u;
    for(u32 weightOffset = 0u; weightOffset < skinVertex.num_weights; ++weightOffset){
        if(writtenInfluenceCount == static_cast<u32>(Impl::s_SkinInfluenceJointCount))
            break;

        const ufbx_skin_weight& weight = skin.weights.data[skinVertex.weight_begin + weightOffset];
        if(!TryAccumulateFbxSkinWeight(static_cast<f64>(weight.weight), weight.cluster_index, clusterJoints, writtenInfluenceCount, weightSum, result))
            return MakeUnexpected(Failure{});
    }

    if(!TryNormalizeFbxSkinInfluenceWeights(weightSum, result))
        return MakeUnexpected(Failure{});

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

