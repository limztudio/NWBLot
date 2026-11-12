// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cluster_operation_input.h"
#include "raytracing_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkClusterAccelerationStructureInputInfoNV ClusterOperationInput::nativeInfo()noexcept{
    auto native = info;
    switch(info.opType){
    case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV:
        native.opInput.pMoveObjects = &move;
        break;
    case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV:
        native.opInput.pClustersBottomLevel = &blas;
        break;
    default:
        native.opInput.pTriangleClusters = &cluster;
        break;
    }
    return native;
}

Expected<ClusterOperationInput> ClusterOperationInput::Build(const RayTracingClusterOperationParams& params, TStringView operationName){
    ClusterOperationInput input;
    VkClusterAccelerationStructureOpTypeNV opType;
    switch(params.type){
    case RayTracingClusterOperationType::Move:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV;
        break;
    case RayTracingClusterOperationType::ClasBuild:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
        break;
    case RayTracingClusterOperationType::ClasBuildTemplates:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV;
        break;
    case RayTracingClusterOperationType::ClasInstantiateTemplates:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
        break;
    case RayTracingClusterOperationType::BlasBuild:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
        break;
    default:
        return MakeUnexpected(Failure{});
    }

    VkClusterAccelerationStructureOpModeNV opMode;
    switch(params.mode){
    case RayTracingClusterOperationMode::ImplicitDestinations:
        opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
        break;
    case RayTracingClusterOperationMode::ExplicitDestinations:
        opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV;
        break;
    case RayTracingClusterOperationMode::GetSizes:
        opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV;
        break;
    default:
        return MakeUnexpected(Failure{});
    }

    VkBuildAccelerationStructureFlagsKHR opFlags = 0;
    if(params.flags & RayTracingClusterOperationFlags::FastTrace)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(!(params.flags & RayTracingClusterOperationFlags::FastTrace) && (params.flags & RayTracingClusterOperationFlags::FastBuild))
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if(params.flags & RayTracingClusterOperationFlags::AllowOMM)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_CLUSTER_OPACITY_MICROMAPS_BIT_NV;

    input.info = MakeVkStruct<VkClusterAccelerationStructureInputInfoNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV);
    input.info.maxAccelerationStructureCount = params.maxArgCount;
    input.info.flags = opFlags;
    input.info.opType = opType;
    input.info.opMode = opMode;

    input.move = MakeVkStruct<VkClusterAccelerationStructureMoveObjectsInputNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV);
    input.cluster = MakeVkStruct<VkClusterAccelerationStructureTriangleClusterInputNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV);
    input.blas = MakeVkStruct<VkClusterAccelerationStructureClustersBottomLevelInputNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV);

    switch(params.type){
    case RayTracingClusterOperationType::Move:{
        VkClusterAccelerationStructureTypeNV moveType;
        switch(params.move.type){
        case RayTracingClusterOperationMoveType::BottomLevel:  moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV; break;
        case RayTracingClusterOperationMoveType::ClusterLevel: moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_NV; break;
        case RayTracingClusterOperationMoveType::Template:     moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_TEMPLATE_NV; break;
        default: return MakeUnexpected(Failure{});
        }
        input.move.type = moveType;
        input.move.noMoveOverlap = (params.flags & RayTracingClusterOperationFlags::NoOverlap) ? VK_TRUE : VK_FALSE;
        input.move.maxMovedBytes = params.move.maxBytes;
        break;
    }
    case RayTracingClusterOperationType::ClasBuild:
    case RayTracingClusterOperationType::ClasBuildTemplates:
    case RayTracingClusterOperationType::ClasInstantiateTemplates:{
        const VkFormat vertexFormat = ConvertFormat(params.clas.vertexFormat);
        if(vertexFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to {}: vertex format is unsupported"), operationName);
            return MakeUnexpected(Failure{});
        }
        input.cluster.vertexFormat = vertexFormat;
        input.cluster.maxGeometryIndexValue = params.clas.maxGeometryIndex;
        input.cluster.maxClusterUniqueGeometryCount = params.clas.maxUniqueGeometryCount;
        input.cluster.maxClusterTriangleCount = params.clas.maxTriangleCount;
        input.cluster.maxClusterVertexCount = params.clas.maxVertexCount;
        input.cluster.maxTotalTriangleCount = params.clas.maxTotalTriangleCount;
        input.cluster.maxTotalVertexCount = params.clas.maxTotalVertexCount;
        input.cluster.minPositionTruncateBitCount = params.clas.minPositionTruncateBitCount;
        break;
    }
    case RayTracingClusterOperationType::BlasBuild:{
        input.blas.maxClusterCountPerAccelerationStructure = params.blas.maxClasPerBlasCount;
        input.blas.maxTotalClusterCount = params.blas.maxTotalClasCount;
        break;
    }
    default:
        break;
    }

    return input;
}

Expected<RayTracingClusterOperationSizeInfo> ClusterOperationQuery::Query(const VulkanContext& context, const RayTracingClusterOperationParams& params){
    if(!context.extensions.nvClusterAccelerationStructure || !context.clusterAccelerationStructureFeatureEnabled || !context.deviceDispatch.vkGetClusterAccelerationStructureBuildSizesNV)
        return MakeUnexpected(Failure{});
    constexpr u32 s_SupportedOperationFlags =
        static_cast<u32>(RayTracingClusterOperationFlags::FastTrace)
        | static_cast<u32>(RayTracingClusterOperationFlags::FastBuild)
        | static_cast<u32>(RayTracingClusterOperationFlags::NoOverlap)
        | static_cast<u32>(RayTracingClusterOperationFlags::AllowOMM)
    ;
    if(
        params.maxArgCount == 0u
        || (static_cast<u32>(params.flags) & ~s_SupportedOperationFlags) != 0u
        || ((params.flags & RayTracingClusterOperationFlags::FastTrace) && (params.flags & RayTracingClusterOperationFlags::FastBuild))
        || ((params.flags & RayTracingClusterOperationFlags::NoOverlap) && params.type != RayTracingClusterOperationType::Move)
    )
        return MakeUnexpected(Failure{});
    if((params.flags & RayTracingClusterOperationFlags::AllowOMM) && (!context.extensions.extOpacityMicromap || !context.opacityMicromapFeatureEnabled))
        return MakeUnexpected(Failure{});

    auto input = ClusterOperationInput::Build(params, FOUNDATION_TEXT("query cluster operation sizes"));
    if(!input)
        return MakeUnexpected(Failure{});
    const auto inputInfo = input->nativeInfo();

    if(params.type == RayTracingClusterOperationType::ClasBuild || params.type == RayTracingClusterOperationType::ClasBuildTemplates || params.type == RayTracingClusterOperationType::ClasInstantiateTemplates){
        const auto& properties = context.nvClusterAccelerationStructureProperties;
        if(
            params.clas.maxTriangleCount > properties.maxTrianglesPerCluster
            || params.clas.maxVertexCount > properties.maxVerticesPerCluster
            || params.clas.maxGeometryIndex > properties.maxClusterGeometryIndex
            || params.clas.minPositionTruncateBitCount > VulkanDetail::s_MaxPositionTruncateBitCount
        )
            return MakeUnexpected(Failure{});

        VkFormatProperties formatProperties{};
        context.instanceDispatch.vkGetPhysicalDeviceFormatProperties(context.physicalDevice, input->cluster.vertexFormat, &formatProperties);
        if((formatProperties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR) == 0u)
            return MakeUnexpected(Failure{});
    }

    auto vkSizeInfo = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    context.deviceDispatch.vkGetClusterAccelerationStructureBuildSizesNV(context.device, &inputInfo, &vkSizeInfo);

    return RayTracingClusterOperationSizeInfo{ .resultMaxSizeInBytes = vkSizeInfo.accelerationStructureSize, .scratchSizeInBytes = vkSizeInfo.buildScratchSize, };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

