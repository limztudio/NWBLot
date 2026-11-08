// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optional_device_feature_policy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace VulkanDetail{


OptionalDeviceFeatureSet OptionalDeviceFeaturePolicy::makeRequested()noexcept{
    OptionalDeviceFeatureSet features;

    features.accelerationStructure.accelerationStructure = VK_TRUE;

    features.rayTracingPipeline.rayTracingPipeline = VK_TRUE;
    features.rayTracingPipeline.rayTraversalPrimitiveCulling = VK_TRUE;

    features.rayQuery.rayQuery = VK_TRUE;

    features.opacityMicromap.micromap = VK_TRUE;

    features.clusterAccelerationStructure.clusterAccelerationStructure = VK_TRUE;

    features.rayTracingInvocationReorder.rayTracingInvocationReorder = VK_TRUE;
    features.rayTracingInvocationReorderExt.rayTracingInvocationReorder = VK_TRUE;

    features.rayTracingLinearSweptSpheres.spheres = VK_FALSE;
    features.rayTracingLinearSweptSpheres.linearSweptSpheres = VK_FALSE;

    features.meshShader.meshShader = VK_TRUE;

    features.fragmentShadingRate.pipelineFragmentShadingRate = VK_TRUE;
    features.fragmentShadingRate.primitiveFragmentShadingRate = VK_TRUE;
    features.fragmentShadingRate.attachmentFragmentShadingRate = VK_TRUE;

    // Descriptor-buffer feature support is mandatory.
    features.descriptorBuffer.descriptorBuffer = VK_TRUE;

    features.deviceFault.deviceFault = VK_TRUE;
    features.deviceFault.deviceFaultVendorBinary = VK_TRUE;

    features.textureCompressionAstcHdr.textureCompressionASTC_HDR = VK_TRUE;

    return features;
}
void* OptionalDeviceFeaturePolicy::featureStruct(OptionalDeviceFeatureSet& features, DeviceExtensionFeature::Enum feature)noexcept{
    switch(feature){
    case DeviceExtensionFeature::AccelerationStructure: return &features.accelerationStructure;
    case DeviceExtensionFeature::RayTracingPipeline: return &features.rayTracingPipeline;
    case DeviceExtensionFeature::RayQuery: return &features.rayQuery;
    case DeviceExtensionFeature::OpacityMicromap: return &features.opacityMicromap;
    case DeviceExtensionFeature::ClusterAccelerationStructure: return &features.clusterAccelerationStructure;
    case DeviceExtensionFeature::RayTracingInvocationReorder: return &features.rayTracingInvocationReorder;
    case DeviceExtensionFeature::RayTracingInvocationReorderExt: return &features.rayTracingInvocationReorderExt;
    case DeviceExtensionFeature::RayTracingLinearSweptSpheres: return &features.rayTracingLinearSweptSpheres;
    case DeviceExtensionFeature::MeshShader: return &features.meshShader;
    case DeviceExtensionFeature::FragmentShadingRate: return &features.fragmentShadingRate;
    case DeviceExtensionFeature::DescriptorBuffer: return &features.descriptorBuffer;
    case DeviceExtensionFeature::DeviceFault: return &features.deviceFault;
    case DeviceExtensionFeature::TextureCompressionAstcHdr: return &features.textureCompressionAstcHdr;
    case DeviceExtensionFeature::None:
    case DeviceExtensionFeature::Count:
    default:
        return nullptr;
    }
}
b8 OptionalDeviceFeaturePolicy::supportsRequested(const OptionalDeviceFeatureSet& requested, const OptionalDeviceFeatureSet& supported, DeviceExtensionFeature::Enum feature)noexcept{
    switch(feature){
    case DeviceExtensionFeature::AccelerationStructure:
        return SupportsRequestedValue(requested.accelerationStructure.accelerationStructure, supported.accelerationStructure.accelerationStructure);
    case DeviceExtensionFeature::RayTracingPipeline:
        return
            SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipeline, supported.rayTracingPipeline.rayTracingPipeline)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplay, supported.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplay)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplayMixed, supported.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplayMixed)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipelineTraceRaysIndirect, supported.rayTracingPipeline.rayTracingPipelineTraceRaysIndirect)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTraversalPrimitiveCulling, supported.rayTracingPipeline.rayTraversalPrimitiveCulling)
        ;
    case DeviceExtensionFeature::RayQuery:
        return SupportsRequestedValue(requested.rayQuery.rayQuery, supported.rayQuery.rayQuery);
    case DeviceExtensionFeature::OpacityMicromap:
        return SupportsRequestedValue(requested.opacityMicromap.micromap, supported.opacityMicromap.micromap);
    case DeviceExtensionFeature::ClusterAccelerationStructure:
        return SupportsRequestedValue(requested.clusterAccelerationStructure.clusterAccelerationStructure, supported.clusterAccelerationStructure.clusterAccelerationStructure);
    case DeviceExtensionFeature::RayTracingInvocationReorder:
        return SupportsRequestedValue(requested.rayTracingInvocationReorder.rayTracingInvocationReorder, supported.rayTracingInvocationReorder.rayTracingInvocationReorder);
    case DeviceExtensionFeature::RayTracingInvocationReorderExt:
        return SupportsRequestedValue(requested.rayTracingInvocationReorderExt.rayTracingInvocationReorder, supported.rayTracingInvocationReorderExt.rayTracingInvocationReorder);
    case DeviceExtensionFeature::RayTracingLinearSweptSpheres:
        return supported.rayTracingLinearSweptSpheres.spheres == VK_TRUE || supported.rayTracingLinearSweptSpheres.linearSweptSpheres == VK_TRUE;
    case DeviceExtensionFeature::MeshShader:
        return SupportsRequestedValue(requested.meshShader.taskShader, supported.meshShader.taskShader) && SupportsRequestedValue(requested.meshShader.meshShader, supported.meshShader.meshShader);
    case DeviceExtensionFeature::FragmentShadingRate:
        return
            SupportsRequestedValue(requested.fragmentShadingRate.pipelineFragmentShadingRate, supported.fragmentShadingRate.pipelineFragmentShadingRate)
            && SupportsRequestedValue(requested.fragmentShadingRate.primitiveFragmentShadingRate, supported.fragmentShadingRate.primitiveFragmentShadingRate)
            && SupportsRequestedValue(requested.fragmentShadingRate.attachmentFragmentShadingRate, supported.fragmentShadingRate.attachmentFragmentShadingRate)
        ;
    case DeviceExtensionFeature::DescriptorBuffer:
        return SupportsRequestedValue(requested.descriptorBuffer.descriptorBuffer, supported.descriptorBuffer.descriptorBuffer);
    case DeviceExtensionFeature::DeviceFault:
        return SupportsRequestedValue(requested.deviceFault.deviceFault, supported.deviceFault.deviceFault);
    case DeviceExtensionFeature::TextureCompressionAstcHdr:
        return SupportsRequestedValue(requested.textureCompressionAstcHdr.textureCompressionASTC_HDR, supported.textureCompressionAstcHdr.textureCompressionASTC_HDR);
    case DeviceExtensionFeature::None:
    case DeviceExtensionFeature::Count:
    default:
        return true;
    }
}
void OptionalDeviceFeaturePolicy::finalizeEnablement(OptionalDeviceFeatureSet& enabled, const OptionalDeviceFeatureSet& supported)noexcept{
    enabled.meshShader.taskShader = supported.meshShader.taskShader;
    enabled.rayTracingLinearSweptSpheres.spheres = supported.rayTracingLinearSweptSpheres.spheres;
    enabled.rayTracingLinearSweptSpheres.linearSweptSpheres = supported.rayTracingLinearSweptSpheres.linearSweptSpheres;
    enabled.deviceFault.deviceFaultVendorBinary = supported.deviceFault.deviceFaultVendorBinary;
}
void OptionalDeviceFeaturePolicy::appendFeature(DeviceExtensionFeature::Enum feature, void*& pNext, OptionalDeviceFeatureSet& features, Array<b8, static_cast<usize>(DeviceExtensionFeature::Count)>& appended)noexcept{
    if(feature == DeviceExtensionFeature::None || feature == DeviceExtensionFeature::Count)
        return;

    const usize featureIndex = static_cast<usize>(feature);
    if(appended[featureIndex])
        return;

    if(void* featureStruct = OptionalDeviceFeaturePolicy::featureStruct(features, feature)){
        AppendFeatureStruct(pNext, *MakeNotNull(static_cast<VkBaseOutStructure*>(featureStruct)));
        appended[featureIndex] = true;
    }
}


};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
