// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "descriptor_image_policy.h"
#include "descriptor_class_policy.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DescriptorImagePolicy::Validate(const GpuDescriptorClass::Enum descriptorClass, const DescriptorWriteItem& writeItem, const TextureDesc& textureDesc){
    if(textureDesc.sampleCount != 1u){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: GpuDescriptorHeap::write rejected a multisampled bindless image."));
        return false;
    }

    if(DescriptorClassPolicy::IsSampledImageClass(descriptorClass)){
        const TextureDimension::Enum dimension = writeItem.dimension != TextureDimension::Unknown ? writeItem.dimension : textureDesc.dimension;
        if(dimension != DescriptorClassPolicy::GetSampledImageDimension(descriptorClass)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: GpuDescriptorHeap::write rejected sampled-image dimension {} for class {}."), static_cast<u32>(dimension), static_cast<u32>(descriptorClass));
            return false;
        }

        const Format::Enum format = writeItem.format != Format::UNKNOWN ? writeItem.format : textureDesc.format;
        if(format == Format::UNKNOWN || format >= Format::Count){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: GpuDescriptorHeap::write rejected an invalid sampled-image format."));
            return false;
        }
        const FormatInfo& formatInfo = GetFormatInfo(format);
        const b8 uintClass = descriptorClass == GpuDescriptorClass::SampledImage2DArrayUint;
        if((uintClass && (formatInfo.kind != FormatKind::Integer || formatInfo.isSigned)) || (!uintClass && formatInfo.kind == FormatKind::Integer)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: GpuDescriptorHeap::write rejected format {} for sampled-image class {}."), static_cast<u32>(format), static_cast<u32>(descriptorClass));
            return false;
        }
    }
    else{
        const TextureDimension::Enum dimension = writeItem.dimension != TextureDimension::Unknown ? writeItem.dimension : textureDesc.dimension;
        if(dimension != TextureDimension::Texture2D && dimension != TextureDimension::Texture2DArray && dimension != TextureDimension::Texture3D){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: GpuDescriptorHeap::write rejected texture dimension {} for StorageImage."), static_cast<u32>(dimension));
            return false;
        }

        const Format::Enum format = writeItem.format != Format::UNKNOWN ? writeItem.format : textureDesc.format;
        if(format == Format::UNKNOWN || format >= Format::Count){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: GpuDescriptorHeap::write rejected an invalid StorageImage format."));
            return false;
        }
        const FormatInfo& formatInfo = GetFormatInfo(format);
        const b8 unsignedInteger = formatInfo.kind == FormatKind::Integer && !formatInfo.isSigned;
        const b8 floatOrNormalized = formatInfo.kind == FormatKind::Float || formatInfo.kind == FormatKind::Normalized;
        if(Format::IsBlockCompressedFormat(format) || formatInfo.hasDepth || formatInfo.hasStencil || (!unsignedInteger && !floatOrNormalized) || (dimension == TextureDimension::Texture3D && !floatOrNormalized)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: GpuDescriptorHeap::write rejected format {} for StorageImage dimension {}."), static_cast<u32>(format), static_cast<u32>(dimension));
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

