// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "binding.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorWriteItem DescriptorWriteItem::Base(const u32 slot, const ResourceType::Enum type, void* const resourceHandle, const Format::Enum format, const TextureDimension::Enum dimension)noexcept{
    DescriptorWriteItem result;
    result.slot = slot;
    result.arrayElement = 0;
    result.type = type;
    result.resourceHandle = resourceHandle;
    result.format = format;
    result.dimension = dimension;
    result.rawData[0] = 0;
    result.rawData[1] = 0;
    result.reserved = 0;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::TextureSrv(const u32 slot, Texture* const texture, const Format::Enum format, const TextureSubresourceSet subresources, const TextureDimension::Enum dimension)noexcept{
    DescriptorWriteItem result = Base(slot, ResourceType::TextureSrv, texture, format, dimension);
    result.subresources = subresources;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::TextureUav(
    const u32 slot,
    Texture* const texture,
    const Format::Enum format,
    const TextureSubresourceSet subresources,
    const TextureDimension::Enum dimension
)noexcept{
    DescriptorWriteItem result = Base(slot, ResourceType::TextureUav, texture, format, dimension);
    result.subresources = subresources;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::TypedBufferSrv(const u32 slot, Buffer* const buffer, const Format::Enum format, const BufferRange range)noexcept{
    DescriptorWriteItem result = Base(slot, ResourceType::TypedBufferSrv, buffer, format, TextureDimension::Unknown);
    result.range = range;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::TypedBufferUav(const u32 slot, Buffer* const buffer, const Format::Enum format, const BufferRange range)noexcept{
    DescriptorWriteItem result = Base(slot, ResourceType::TypedBufferUav, buffer, format, TextureDimension::Unknown);
    result.range = range;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::ConstantBuffer(const u32 slot, Buffer* const buffer, const BufferRange range)noexcept{
    const b8 isVolatile = buffer && buffer->getCreationDescription().isVolatile;

    DescriptorWriteItem result = Base(slot, isVolatile ? ResourceType::VolatileConstantBuffer : ResourceType::ConstantBuffer, buffer, Format::UNKNOWN, TextureDimension::Unknown);
    result.range = range;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::StructuredBufferSrv(const u32 slot, Buffer* const buffer, const Format::Enum format, const BufferRange range)noexcept{
    DescriptorWriteItem result = Base(slot, ResourceType::StructuredBufferSrv, buffer, format, TextureDimension::Unknown);
    result.range = range;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::StructuredBufferUav(const u32 slot, Buffer* const buffer, const Format::Enum format, const BufferRange range)noexcept{
    DescriptorWriteItem result = Base(slot, ResourceType::StructuredBufferUav, buffer, format, TextureDimension::Unknown);
    result.range = range;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::RawBufferSrv(const u32 slot, Buffer* const buffer, const BufferRange range)noexcept{
    DescriptorWriteItem result = Base(slot, ResourceType::RawBufferSrv, buffer, Format::UNKNOWN, TextureDimension::Unknown);
    result.range = range;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::RawBufferUav(const u32 slot, Buffer* const buffer, const BufferRange range)noexcept{
    DescriptorWriteItem result = Base(slot, ResourceType::RawBufferUav, buffer, Format::UNKNOWN, TextureDimension::Unknown);
    result.range = range;
    return result;
}

DescriptorWriteItem DescriptorWriteItem::PushConstants(const u32 slot, const u32 byteSize)noexcept{
    DescriptorWriteItem result = Base(slot, ResourceType::PushConstants, nullptr, Format::UNKNOWN, TextureDimension::Unknown);
    result.range.byteOffset = 0;
    result.range.byteSize = byteSize;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

