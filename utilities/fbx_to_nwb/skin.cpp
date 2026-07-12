
#include "skin.h"

#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FbxSkinDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f64 s_UniformScaleNumerator = 1.0;
static constexpr f64 s_InvertibleMatrixDeterminantEpsilon = 0.00000001;
static constexpr f32 s_JointMatrixRowEpsilon = 0.0001f;

ufbx_matrix MakeInverseUniformScaleMatrix(const f64 scale){
    ufbx_matrix matrix = {};
    const f64 inverseScale = s_UniformScaleNumerator / scale;
    matrix.m00 = inverseScale;
    matrix.m11 = inverseScale;
    matrix.m22 = inverseScale;
    return matrix;
}

bool FiniteUfbxMatrix(const ufbx_matrix& matrix){
    for(usize i = 0u; i < 12u; ++i){
        if(!IsFinite(static_cast<f64>(matrix.v[i])))
            return false;
    }
    return true;
}

JointMatrix ToJointMatrix(const ufbx_matrix& matrix){
    JointMatrix result{};
    result.rows[0] = Vec4{
        static_cast<f32>(matrix.m00),
        static_cast<f32>(matrix.m01),
        static_cast<f32>(matrix.m02),
        static_cast<f32>(matrix.m03),
    };
    result.rows[1] = Vec4{
        static_cast<f32>(matrix.m10),
        static_cast<f32>(matrix.m11),
        static_cast<f32>(matrix.m12),
        static_cast<f32>(matrix.m13),
    };
    result.rows[2] = Vec4{
        static_cast<f32>(matrix.m20),
        static_cast<f32>(matrix.m21),
        static_cast<f32>(matrix.m22),
        static_cast<f32>(matrix.m23),
    };
    return result;
}

bool NearlyEqualJointMatrixRows(const SIMDVector (&lhsRows)[s_JointMatrixRowCount], const SIMDVector (&rhsRows)[s_JointMatrixRowCount]){
    const SIMDVector epsilon = VectorReplicate(s_JointMatrixRowEpsilon);
    for(usize rowIndex = 0u; rowIndex < s_JointMatrixRowCount; ++rowIndex){
        const SIMDVector difference = VectorAbs(VectorSubtract(lhsRows[rowIndex], rhsRows[rowIndex]));
        if(!Vector4LessOrEqual(difference, epsilon))
            return false;
    }
    return true;
}

AString NodeDisplayName(const ufbx_node* node){
    if(!node)
        return "<null>";
    AString name;
    if(node->name.data && node->name.length != 0u)
        name.assign(node->name.data, node->name.length);
    if(name.empty())
        return "<unnamed node>";
    return name;
}

ufbx_matrix BuildMeshFromOutputMatrix(const MeshInstance& instance, const ImportOptions& options){
    const ufbx_matrix scaleInverse = MakeInverseUniformScaleMatrix(options.scale);
    if(!options.bakeTransforms)
        return scaleInverse;

    const ufbx_matrix nodeMeshToWorldInverse = ufbx_matrix_invert(&instance.node->geometry_to_world);
    return ufbx_matrix_mul(&nodeMeshToWorldInverse, &scaleInverse);
}

ufbx_matrix BuildOutputInverseBindMatrix(
    const ufbx_skin_cluster& cluster,
    const MeshInstance& instance,
    const ImportOptions& options
){
    const ufbx_matrix meshFromOutput = BuildMeshFromOutputMatrix(instance, options);
    return ufbx_matrix_mul(&cluster.geometry_to_bone, &meshFromOutput);
}

bool FindOrAddJoint(
    ExportContext& context,
    ufbx_skin_cluster* cluster,
    const ufbx_matrix& inverseBind,
    u16& outJoint
){
    outJoint = 0u;
    if(!cluster || !cluster->bone_node){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin cluster is missing a bone node"));
        return false;
    }
    if(!FiniteUfbxMatrix(inverseBind) || Abs(static_cast<f64>(ufbx_matrix_determinant(&inverseBind))) <= s_InvertibleMatrixDeterminantEpsilon){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin cluster inverse bind matrix is not finite and invertible"));
        return false;
    }

    const JointMatrix convertedMatrix = ToJointMatrix(inverseBind);
    const ufbx_matrix bindPose = ufbx_matrix_invert(&inverseBind);
    if(!FiniteUfbxMatrix(bindPose)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin cluster bind pose matrix is not finite"));
        return false;
    }
    const JointMatrix convertedBindPose = ToJointMatrix(bindPose);
    auto foundJoint = context.jointLookup.find(cluster->bone_node);
    if(foundJoint != context.jointLookup.end()){
        const usize jointIndex = static_cast<usize>(foundJoint.value());
        NWB_ASSERT(jointIndex < context.inverseBindMatrices.size());
        NWB_ASSERT(jointIndex < context.bindPoseMatrices.size());
        const SIMDVector existingInverseBindRows[s_JointMatrixRowCount] = {
            LoadFloat(context.inverseBindMatrices[jointIndex].rows[0u]),
            LoadFloat(context.inverseBindMatrices[jointIndex].rows[1u]),
            LoadFloat(context.inverseBindMatrices[jointIndex].rows[2u]),
        };
        const SIMDVector convertedInverseBindRows[s_JointMatrixRowCount] = {
            LoadFloat(convertedMatrix.rows[0u]),
            LoadFloat(convertedMatrix.rows[1u]),
            LoadFloat(convertedMatrix.rows[2u]),
        };
        if(!NearlyEqualJointMatrixRows(existingInverseBindRows, convertedInverseBindRows)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected meshes bind skeleton joint '{}' with different inverse bind matrices")
                , StringConvert(NodeDisplayName(cluster->bone_node))
            );
            return false;
        }

        outJoint = foundJoint.value();
        return true;
    }

    if(context.joints.size() >= s_MaxSkeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skeleton has more than {} joints"), s_MaxSkeletonJointCount);
        return false;
    }

    outJoint = static_cast<u16>(context.joints.size());
    context.joints.push_back(cluster->bone_node);
    context.bindPoseMatrices.push_back(convertedBindPose);
    context.inverseBindMatrices.push_back(convertedMatrix);
    context.jointLookup.emplace(cluster->bone_node, outJoint);
    return true;
}

bool BuildClusterJointMap(
    const MeshInstance& instance,
    const ImportOptions& options,
    ufbx_skin_deformer* skin,
    ExportContext& context,
    UtilityVector<u16>& outClusterJoints
){
    outClusterJoints.clear();
    if(!skin){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skinned mesh requires a skin deformer"));
        return false;
    }
    if(skin->clusters.count == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer contains no clusters"));
        return false;
    }
    if(skin->clusters.count > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer has too many clusters"));
        return false;
    }
    if(skin->clusters.count > Limit<usize>::s_Max - context.joints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer cluster count overflows skeleton joint capacity"));
        return false;
    }

    const usize reservedJointCount = context.joints.size() + skin->clusters.count;
    context.joints.reserve(reservedJointCount);
    context.bindPoseMatrices.reserve(reservedJointCount);
    context.inverseBindMatrices.reserve(reservedJointCount);
    context.jointLookup.reserve(reservedJointCount);
    outClusterJoints.reserve(skin->clusters.count);
    for(usize clusterIndex = 0u; clusterIndex < skin->clusters.count; ++clusterIndex){
        ufbx_skin_cluster* cluster = skin->clusters.data[clusterIndex];
        if(!cluster){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer contains a null cluster"));
            return false;
        }

        const ufbx_matrix inverseBind = BuildOutputInverseBindMatrix(*cluster, instance, options);
        u16 joint = 0u;
        if(!FindOrAddJoint(context, cluster, inverseBind, joint))
            return false;
        outClusterJoints.push_back(joint);
    }

    return true;
}

bool BuildInfluence(
    ufbx_skin_deformer* skin,
    const UtilityVector<u16>& clusterJoints,
    const u32 logicalVertex,
    MeshSkinInfluence& outInfluence
){
    outInfluence = MeshSkinInfluence{};
    if(!skin || logicalVertex >= skin->vertices.count){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin deformer does not contain weights for every logical vertex"));
        return false;
    }

    const ufbx_skin_vertex skinVertex = skin->vertices.data[logicalVertex];
    if(
        skinVertex.weight_begin > skin->weights.count
        || skinVertex.num_weights > skin->weights.count - skinVertex.weight_begin
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin vertex weight range is out of bounds"));
        return false;
    }

    f64 weightSum = 0.0;
    u32 writtenInfluenceCount = 0u;
    for(u32 weightOffset = 0u; weightOffset < skinVertex.num_weights; ++weightOffset){
        if(writtenInfluenceCount == 4u)
            break;

        const ufbx_skin_weight& weight = skin->weights.data[skinVertex.weight_begin + weightOffset];
        const f64 value = static_cast<f64>(weight.weight);
        if(!IsFinite(value)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin contains a non-finite weight"));
            return false;
        }
        if(value <= 0.0)
            continue;
        if(weight.cluster_index >= clusterJoints.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skin weight references an out-of-range cluster"));
            return false;
        }

        outInfluence.joint[writtenInfluenceCount] = clusterJoints[weight.cluster_index];
        outInfluence.weight[writtenInfluenceCount] = static_cast<f32>(value);
        weightSum += value;
        ++writtenInfluenceCount;
    }

    if(!IsFinite(weightSum) || weightSum <= 0.0){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skinned mesh contains a vertex with no positive skin weights"));
        return false;
    }

    const f32 inverseWeightSum = static_cast<f32>(1.0 / weightSum);
    for(f32& weight : outInfluence.weight)
        weight *= inverseWeightSum;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

