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
    SIMDMatrixDouble result{};
    result.v[0u] = VectorDoubleSet(static_cast<f64>(matrix.m00), static_cast<f64>(matrix.m01), static_cast<f64>(matrix.m02), static_cast<f64>(matrix.m03));
    result.v[1u] = VectorDoubleSet(static_cast<f64>(matrix.m10), static_cast<f64>(matrix.m11), static_cast<f64>(matrix.m12), static_cast<f64>(matrix.m13));
    result.v[2u] = VectorDoubleSet(static_cast<f64>(matrix.m20), static_cast<f64>(matrix.m21), static_cast<f64>(matrix.m22), static_cast<f64>(matrix.m23));
    result.v[3u] = VectorDoubleSet(0.0, 0.0, 0.0, 1.0);
    return result;
}

b8 FiniteUfbxMatrix(const SIMDMatrixDouble& matrix)noexcept{
    for(usize rowIndex = 0u; rowIndex < 3u; ++rowIndex){
        if(!VectorDoubleIsFinite(matrix.v[rowIndex], VectorComponentMask::s_XYZW))
            return false;
    }
    return true;
}

SIMDMatrix ConvertJointMatrix(const SIMDMatrixDouble& matrix)noexcept{
    SIMDMatrix result{};
    result.v[0u] = ConvertVectorDoubleToSingle(matrix.v[0u]);
    result.v[1u] = ConvertVectorDoubleToSingle(matrix.v[1u]);
    result.v[2u] = ConvertVectorDoubleToSingle(matrix.v[2u]);
    result.v[3u] = VectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    return result;
}

b8 NearlyEqualJointMatrices(const SIMDMatrix& lhs, const SIMDMatrix& rhs)noexcept{
    const SIMDVectorSingle epsilon = VectorReplicate(s_JointMatrixRowEpsilon);
    for(usize rowIndex = 0u; rowIndex < Impl::s_SkeletonJointMatrixRowCount; ++rowIndex){
        if(!Vector4NearEqual(lhs.v[rowIndex], rhs.v[rowIndex], epsilon))
            return false;
    }
    return true;
}

AStringView NodeDisplayName(const ufbx_node* node)noexcept{
    if(!node)
        return s_NullNodeLabel;
    const AStringView name = UfbxStringView(node->name);
    return !name.empty() ? name : s_UnnamedNodeLabel;
}

Expected<::Vector<u16, Core::Alloc::ScratchArena>> BuildClusterJointMap(const MeshInstance& instance, const ImportOptions& options, const ufbx_skin_deformer& skin, Core::Alloc::ScratchArena& scratchArena, ExportContext& inOutContext){
    ::Vector<u16, Core::Alloc::ScratchArena> outClusterJoints(scratchArena);
    if(skin.clusters.count == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer contains no clusters"));
        return MakeUnexpected(Failure{});
    }
    if(skin.clusters.count > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer has too many clusters"));
        return MakeUnexpected(Failure{});
    }
    if(skin.clusters.count > Limit<usize>::s_Max - inOutContext.joints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer cluster count overflows skeleton joint capacity"));
        return MakeUnexpected(Failure{});
    }

    const usize reservedJointCount = inOutContext.joints.size() + skin.clusters.count;
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
        const SIMDMatrixDouble inverseBindDouble = LoadUfbxMatrix(inverseBind);
        const f64 determinant = static_cast<f64>(ufbx_matrix_determinant(&inverseBind));
        if(!FiniteUfbxMatrix(inverseBindDouble) || !IsFinite(determinant) || Abs(determinant) <= s_InvertibleMatrixDeterminantEpsilon){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin cluster inverse bind matrix is not finite and invertible"));
            return MakeUnexpected(Failure{});
        }

        const SIMDMatrix inverseBindValue = ConvertJointMatrix(inverseBindDouble);
        if(MatrixIsNaN(inverseBindValue) || MatrixIsInfinite(inverseBindValue)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin cluster inverse bind matrix is outside float range"));
            return MakeUnexpected(Failure{});
        }
        const ufbx_matrix bindPose = ufbx_matrix_invert(&inverseBind);
        const SIMDMatrixDouble bindPoseDouble = LoadUfbxMatrix(bindPose);
        if(!FiniteUfbxMatrix(bindPoseDouble)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin cluster bind pose matrix is not finite"));
            return MakeUnexpected(Failure{});
        }
        const SIMDMatrix bindPoseValue = ConvertJointMatrix(bindPoseDouble);
        if(MatrixIsNaN(bindPoseValue) || MatrixIsInfinite(bindPoseValue)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin cluster bind pose matrix is outside float range"));
            return MakeUnexpected(Failure{});
        }
        auto foundJoint = inOutContext.jointLookup.find(MakeNotNull(cluster->bone_node));
        if(foundJoint != inOutContext.jointLookup.end()){
            const usize jointIndex = static_cast<usize>(foundJoint.value());
            NWB_ASSERT(jointIndex < inOutContext.inverseBindMatrices.size());
            NWB_ASSERT(jointIndex < inOutContext.bindPoseMatrices.size());
            const SIMDMatrix existingInverseBind = LoadFloat(inOutContext.inverseBindMatrices[jointIndex]);
            if(!NearlyEqualJointMatrices(existingInverseBind, inverseBindValue)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected meshes bind skeleton joint '{}' with different inverse bind matrices"), ConvertString(NodeDisplayName(cluster->bone_node)));
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

Expected<SkinInfluenceCalculation> LoadInfluence(const ufbx_skin_deformer& skin, const Span<const u16> clusterJoints, const u32 logicalVertex){
    SkinInfluenceCalculation result{ VectorZero(), VectorZero() };
    if(logicalVertex >= skin.vertices.count){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer does not contain weights for every logical vertex"));
        return MakeUnexpected(Failure{});
    }

    const ufbx_skin_vertex skinVertex = skin.vertices.data[logicalVertex];
    if(skinVertex.weight_begin > skin.weights.count || skinVertex.num_weights > skin.weights.count - skinVertex.weight_begin){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin vertex weight range is out of bounds"));
        return MakeUnexpected(Failure{});
    }

    f64 weightSum = 0.0;
    u32 writtenInfluenceCount = 0u;
    for(u32 weightOffset = 0u; weightOffset < skinVertex.num_weights; ++weightOffset){
        if(writtenInfluenceCount == static_cast<u32>(Impl::s_SkinInfluenceJointCount))
            break;

        const ufbx_skin_weight& weight = skin.weights.data[skinVertex.weight_begin + weightOffset];
        const f64 value = static_cast<f64>(weight.weight);
        if(!IsFinite(value)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin contains a non-finite weight"));
            return MakeUnexpected(Failure{});
        }
        if(value <= 0.0)
            continue;
        if(weight.cluster_index >= clusterJoints.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin weight references an out-of-range cluster"));
            return MakeUnexpected(Failure{});
        }

        result.joints = VectorSetIntByIndex(result.joints, clusterJoints[weight.cluster_index], writtenInfluenceCount);
        result.weights = VectorSetByIndex(result.weights, static_cast<f32>(value), writtenInfluenceCount);
        weightSum += value;
        ++writtenInfluenceCount;
    }

    if(!IsFinite(weightSum) || weightSum <= 0.0){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skinned mesh contains a vertex with no positive skin weights"));
        return MakeUnexpected(Failure{});
    }

    const f32 normalization = static_cast<f32>(s_UniformScaleNumerator / weightSum);
    result.weights = VectorScale(result.weights, normalization);

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

