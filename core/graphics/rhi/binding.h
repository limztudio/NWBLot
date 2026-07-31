// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "raytracing.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ResourceType{
    enum Enum : u8{
        None,

        Texture_SRV,
        Texture_UAV,
        TypedBuffer_SRV,
        TypedBuffer_UAV,
        StructuredBuffer_SRV,
        StructuredBuffer_UAV,
        RawBuffer_SRV,
        RawBuffer_UAV,
        ConstantBuffer,
        VolatileConstantBuffer,
        Sampler,
        RayTracingAccelStruct,
        PushConstants,

        kCount
    };
};

struct BindingLayoutItem{
    static constexpr usize s_ByteSize = 8u;

    u32 slot;

    ResourceType::Enum type : 8;
    u8 reserved : 8;
    // Push constant byte size when (type == PushConstants)
    // Descriptor array size (1 or more) for all other resource types
    // Must be 1 for VolatileConstantBuffer
    u16 size : 16;

    constexpr BindingLayoutItem& setSlot(u32 value){ slot = value; return *this; }
    constexpr BindingLayoutItem& setType(ResourceType::Enum value){ type = value; return *this; }
    constexpr BindingLayoutItem& setSize(u32 value){ size = static_cast<u16>(value); return *this; }

    constexpr u32 getArraySize()const{ return (type == ResourceType::PushConstants) ? 1 : size; }

#define NWB_BINDING_LAYOUT_ITEM_INITIALIZER(TYPE_ENUM) \
    static constexpr BindingLayoutItem TYPE_ENUM(const u32 slot, const usize size){ \
        BindingLayoutItem ret{}; \
        ret.slot = slot; \
        ret.type = ResourceType::TYPE_ENUM; \
        ret.size = static_cast<u16>(size); \
        return ret; \
    }
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(Texture_SRV)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(Texture_UAV)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(TypedBuffer_SRV)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(TypedBuffer_UAV)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(StructuredBuffer_SRV)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(StructuredBuffer_UAV)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(RawBuffer_SRV)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(RawBuffer_UAV)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(ConstantBuffer)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(VolatileConstantBuffer)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(Sampler)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(RayTracingAccelStruct)
    static constexpr BindingLayoutItem PushConstants(const u32 slot, const usize size){
        BindingLayoutItem ret{};
        ret.slot = slot;
        ret.type = ResourceType::PushConstants;
        ret.size = static_cast<u16>(size);
        return ret;
    }
#undef NWB_BINDING_LAYOUT_ITEM_INITIALIZER
};
inline bool operator==(const BindingLayoutItem& lhs, const BindingLayoutItem& rhs){
    return lhs.slot == rhs.slot && lhs.type == rhs.type && lhs.size == rhs.size;
}
inline bool operator!=(const BindingLayoutItem& lhs, const BindingLayoutItem& rhs){ return !(lhs == rhs); }
static_assert(sizeof(BindingLayoutItem) == BindingLayoutItem::s_ByteSize, "sizeof(BindingLayoutItem) is supposed to be 8 bytes");

struct BindingLayoutDesc{
    GraphicsVector<BindingLayoutItem> bindings;
    ShaderType::Mask visibility = ShaderType::None;

    explicit BindingLayoutDesc(GraphicsArena& arena)
        : bindings(arena)
    {}

    constexpr BindingLayoutDesc& setVisibility(ShaderType::Mask value){ visibility = value; return *this; }
    BindingLayoutDesc& addItem(const BindingLayoutItem& value){ bindings.push_back(value); return *this; }
};

// BindlessLayoutType describes the SPIR-V bindings DXC emits for the renderer's global ResourceDescriptorHeap and
// SamplerDescriptorHeap layouts. The shader must use the same reserved descriptor-set index as the heap layout.
// https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#resourcedescriptorheaps-samplerdescriptorheaps
namespace BindlessLayoutType{
    enum Enum : u8{
        Immutable = 0,      // Must use registerSpaces to define a fixed descriptor type

        MutableSrvUavCbv,   // Corresponds to SPIRV binding -fvk-bind-resource-heap (Counter resources ResourceDescriptorHeap)
                            // Valid descriptor types: Texture_SRV, Texture_UAV, TypedBuffer_SRV, TypedBuffer_UAV,
                            // StructuredBuffer_SRV, StructuredBuffer_UAV, RawBuffer_SRV, RawBuffer_UAV, ConstantBuffer

        MutableSampler,     // Corresponds to SPIRV binding -fvk-bind-sampler-heap (SamplerDescriptorHeap)
                            // Valid descriptor types: Sampler
    };
};

// Bindless layouts describe a global descriptor-heap array in the shader. The `registerSpaces` vector specifies
// which bindings the heap exposes, with the resource type derived from each entry. They are never per-pass tables.
struct BindlessLayoutDesc{
    FixedVector<BindingLayoutItem, s_MaxBindlessRegisterSpaces> registerSpaces;
    u32 maxCapacity = 0;

    // This resource-bearing layout occupies an explicit SPIR-V descriptor set in a multi-layout pipeline. The global
    // bindless heap uses reserved high sets so it cannot collide with push-constant-only pipeline-local sets.
    u32 descriptorSetIndex = Limit<u32>::s_Max;

    ShaderType::Mask visibility = ShaderType::None;
    BindlessLayoutType::Enum layoutType = BindlessLayoutType::Immutable;

    constexpr BindlessLayoutDesc& setVisibility(ShaderType::Mask value){ visibility = value; return *this; }
    constexpr BindlessLayoutDesc& setMaxCapacity(u32 value){ maxCapacity = value; return *this; }
    constexpr BindlessLayoutDesc& addRegisterSpace(const BindingLayoutItem& value){ registerSpaces.push_back(value); return *this; }
    constexpr BindlessLayoutDesc& setLayoutType(BindlessLayoutType::Enum value){ layoutType = value; return *this; }
    constexpr BindlessLayoutDesc& setDescriptorSetIndex(u32 value){ descriptorSetIndex = value; return *this; }
};

typedef GraphicsBackend::Handle<BindingLayout> BindingLayoutHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Descriptor writes


struct DescriptorWriteItem{
    static constexpr usize s_ByteSize = 40u;

    void* resourceHandle;

    u32 slot;

    // Specifies the index in a binding array.
    // Must be less than the 'size' property of the matching BindingLayoutItem.
    // Specifies the index into the descriptor array generated for an HLSL resource array.
    u32 arrayElement;

    ResourceType::Enum type          : 8;
    TextureDimension::Enum dimension : 8; // valid for Texture_SRV, Texture_UAV
    Format::Enum format              : 8; // valid for Texture_SRV, Texture_UAV, Buffer_SRV, Buffer_UAV
    u8 reserved                      : 8;

    u32 reserved2;

    union{
        TextureSubresourceSet subresources; // valid for Texture_SRV, Texture_UAV
        BufferRange range; // valid for Buffer_SRV, Buffer_UAV, ConstantBuffer
        u64 rawData[2];
    };
    static_assert(sizeof(TextureSubresourceSet) == TextureSubresourceSet::s_ByteSize, "sizeof(TextureSubresourceSet) is supposed to be 16 bytes");
    static_assert(sizeof(BufferRange) == BufferRange::s_ByteSize, "sizeof(BufferRange) is supposed to be 16 bytes");

    // Default constructor that doesn't initialize anything for performance:
    // DescriptorWriteItem's are stored in large statically sized arrays.
    DescriptorWriteItem(){}

    constexpr DescriptorWriteItem& setArrayElement(u32 value){ arrayElement = value; return *this; }
    constexpr DescriptorWriteItem& setFormat(Format::Enum value){ format = value; return *this; }
    constexpr DescriptorWriteItem& setDimension(TextureDimension::Enum value){ dimension = value; return *this; }
    constexpr DescriptorWriteItem& setSubresources(TextureSubresourceSet value){ subresources = value; return *this; }
    constexpr DescriptorWriteItem& setRange(BufferRange value){ range = value; return *this; }

    static DescriptorWriteItem Base(u32 slot, ResourceType::Enum type, void* resourceHandle, Format::Enum format, TextureDimension::Enum dimension){
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
        result.reserved2 = 0;
        return result;
    }

    static DescriptorWriteItem None(u32 slot = 0){
        return Base(slot, ResourceType::None, nullptr, Format::UNKNOWN, TextureDimension::Unknown);
    }
    static DescriptorWriteItem Texture_SRV(u32 slot, Texture* texture, Format::Enum format = Format::UNKNOWN, TextureSubresourceSet subresources = s_AllSubresources, TextureDimension::Enum dimension = TextureDimension::Unknown){
        DescriptorWriteItem result = Base(slot, ResourceType::Texture_SRV, texture, format, dimension);
        result.subresources = subresources;
        return result;
    }
    static DescriptorWriteItem Texture_UAV(u32 slot, Texture* texture, Format::Enum format = Format::UNKNOWN, TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, TextureSubresourceSet::AllArraySlices), TextureDimension::Enum dimension = TextureDimension::Unknown){
        DescriptorWriteItem result = Base(slot, ResourceType::Texture_UAV, texture, format, dimension);
        result.subresources = subresources;
        return result;
    }
    static DescriptorWriteItem TypedBuffer_SRV(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer){
        DescriptorWriteItem result = Base(slot, ResourceType::TypedBuffer_SRV, buffer, format, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static DescriptorWriteItem TypedBuffer_UAV(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer){
        DescriptorWriteItem result = Base(slot, ResourceType::TypedBuffer_UAV, buffer, format, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static DescriptorWriteItem ConstantBuffer(u32 slot, Buffer* buffer, BufferRange range = s_EntireBuffer);
    static DescriptorWriteItem Sampler(u32 slot, Sampler* sampler){
        return Base(slot, ResourceType::Sampler, sampler, Format::UNKNOWN, TextureDimension::Unknown);
    }
    static DescriptorWriteItem RayTracingAccelStruct(u32 slot, RayTracingAccelStruct* as){
        return Base(slot, ResourceType::RayTracingAccelStruct, as, Format::UNKNOWN, TextureDimension::Unknown);
    }
    static DescriptorWriteItem StructuredBuffer_SRV(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer){
        DescriptorWriteItem result = Base(slot, ResourceType::StructuredBuffer_SRV, buffer, format, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static DescriptorWriteItem StructuredBuffer_UAV(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer){
        DescriptorWriteItem result = Base(slot, ResourceType::StructuredBuffer_UAV, buffer, format, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static DescriptorWriteItem RawBuffer_SRV(u32 slot, Buffer* buffer, BufferRange range = s_EntireBuffer){
        DescriptorWriteItem result = Base(slot, ResourceType::RawBuffer_SRV, buffer, Format::UNKNOWN, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static DescriptorWriteItem RawBuffer_UAV(u32 slot, Buffer* buffer, BufferRange range = s_EntireBuffer){
        DescriptorWriteItem result = Base(slot, ResourceType::RawBuffer_UAV, buffer, Format::UNKNOWN, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static DescriptorWriteItem PushConstants(u32 slot, u32 byteSize){
        DescriptorWriteItem result = Base(slot, ResourceType::PushConstants, nullptr, Format::UNKNOWN, TextureDimension::Unknown);
        result.range.byteOffset = 0;
        result.range.byteSize = byteSize;
        return result;
    }
};
inline bool operator==(const DescriptorWriteItem& lhs, const DescriptorWriteItem& rhs){
    return
        lhs.resourceHandle == rhs.resourceHandle
        && lhs.slot == rhs.slot
        && lhs.arrayElement == rhs.arrayElement
        && lhs.type == rhs.type
        && lhs.dimension == rhs.dimension
        && lhs.format == rhs.format
        && lhs.rawData[0] == rhs.rawData[0]
        && lhs.rawData[1] == rhs.rawData[1]
    ;
}
inline bool operator!=(const DescriptorWriteItem& lhs, const DescriptorWriteItem& rhs){ return !(lhs == rhs); }
static_assert(sizeof(DescriptorWriteItem) == DescriptorWriteItem::s_ByteSize, "sizeof(DescriptorWriteItem) is supposed to be 40 bytes");

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

