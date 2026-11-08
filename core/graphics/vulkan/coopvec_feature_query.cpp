// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "coopvec_feature_query.h"
#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


Expected<CooperativeVectorDeviceFeatures> CooperativeVectorFeatureQuery::Query(VulkanContext& context, CpuTaskScheduler& scheduler){
    VkResult res = VK_SUCCESS;

    CooperativeVectorDeviceFeatures output(context.objectArena);

    if(!context.extensions.nvCooperativeVector || !context.coopVecFeatures.cooperativeVector)
        return output;

    uint32_t propertyCount = 0;
    res = context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(context.physicalDevice, &propertyCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query cooperative vector property count: {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }
    if(propertyCount == 0)
        return output;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CooperativeVectorQueryArena);
    Vector<VkCooperativeVectorPropertiesNV, Alloc::ScratchArena> properties(propertyCount, scratchArena);
    for(u32 i = 0u; i < propertyCount; ++i){
        properties[i].sType = VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV;
        properties[i].pNext = nullptr;
    }

    res = context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(context.physicalDevice, &propertyCount, properties.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query cooperative vector properties: {}"), ResultToString(res));
        return MakeUnexpected(Failure{});
    }

    output.matMulFormats.resize(propertyCount);
    auto fillMatMulFormat = [&](usize i){
        const auto& prop = properties[i];
        CooperativeVectorMatMulFormatCombo& combo = output.matMulFormats[i];
        combo.inputType = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.inputType));
        combo.inputInterpretation = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.inputInterpretation));
        combo.matrixInterpretation = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.matrixInterpretation));
        combo.biasInterpretation = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.biasInterpretation));
        combo.outputType = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.resultType));
        combo.transposeSupported = prop.transpose != VK_FALSE;
    };

    if(scheduler.isParallelEnabled() && propertyCount >= s_ParallelCoopVecThreshold){
        const auto parallelWork = scheduler.parallelFor(static_cast<usize>(0), propertyCount, fillMatMulFormat);
        if(!parallelWork){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: cooperative vector feature preparation rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
            return MakeUnexpected(Failure{});
        }
    }
    else{
        for(usize i = 0u; i < propertyCount; ++i)
            fillMatMulFormat(i);
    }

    output.trainingFloat16 = context.coopVecFeatures.cooperativeVectorTraining != VK_FALSE && context.coopVecProperties.cooperativeVectorTrainingFloat16Accumulation != VK_FALSE;
    output.trainingFloat32 = context.coopVecFeatures.cooperativeVectorTraining != VK_FALSE && context.coopVecProperties.cooperativeVectorTrainingFloat32Accumulation != VK_FALSE;

    return output;
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
