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

        Count
    };
};

struct BindingLayoutItem{
    static constexpr usize s_ByteSize = 8u;

    u32 slot;

    ResourceType::Enum type : 8;
    u8 reserved : 8;
    // Push constant byte size for PushConstants, 1 for VolatileConstantBuffer, or descriptor array size (>= 1) for other types.
    u16 size : 16;


public:
#define NWB_BINDING_LAYOUT_ITEM_INITIALIZER(TYPE_ENUM, FACTORY_NAME) \
    static constexpr BindingLayoutItem FACTORY_NAME(const u32 slot, const usize size)noexcept{ \
        BindingLayoutItem ret{}; \
        ret.slot = slot; \
        ret.type = ResourceType::TYPE_ENUM; \
        ret.size = static_cast<u16>(size); \
        return ret; \
    }
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(Texture_SRV, TextureSrv)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(Texture_UAV, TextureUav)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(TypedBuffer_SRV, TypedBufferSrv)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(TypedBuffer_UAV, TypedBufferUav)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(StructuredBuffer_SRV, StructuredBufferSrv)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(StructuredBuffer_UAV, StructuredBufferUav)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(RawBuffer_SRV, RawBufferSrv)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(RawBuffer_UAV, RawBufferUav)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(ConstantBuffer, ConstantBuffer)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(VolatileConstantBuffer, VolatileConstantBuffer)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(Sampler, Sampler)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(RayTracingAccelStruct, RayTracingAccelStruct)
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(PushConstants, PushConstants)
#undef NWB_BINDING_LAYOUT_ITEM_INITIALIZER


public:
    constexpr BindingLayoutItem& setSlot(u32 value)noexcept{ slot = value; return *this; }

    constexpr BindingLayoutItem& setType(ResourceType::Enum value)noexcept{ type = value; return *this; }

    constexpr BindingLayoutItem& setSize(u32 value)noexcept{ size = static_cast<u16>(value); return *this; }
};
inline b8 operator==(const BindingLayoutItem& lhs, const BindingLayoutItem& rhs)noexcept{
    return lhs.slot == rhs.slot && lhs.type == rhs.type && lhs.size == rhs.size;
}

inline b8 operator!=(const BindingLayoutItem& lhs, const BindingLayoutItem& rhs)noexcept{ return !(lhs == rhs); }
static_assert(sizeof(BindingLayoutItem) == BindingLayoutItem::s_ByteSize, "sizeof(BindingLayoutItem) is supposed to be 8 bytes");

struct BindingLayoutDesc{
    GraphicsVector<BindingLayoutItem> bindings;
    ShaderType::Mask visibility = ShaderType::None;


public:
    explicit BindingLayoutDesc(GraphicsArena& arena)noexcept
        : bindings(arena)
    {}


public:
    constexpr BindingLayoutDesc& setVisibility(ShaderType::Mask value)noexcept{ visibility = value; return *this; }

    BindingLayoutDesc& addItem(const BindingLayoutItem& value){ bindings.push_back(value); return *this; }
};

// BindlessLayoutType describes descriptor classes in the global heap.
// Slang bindings and host layouts must agree on explicit Vulkan descriptor-set and binding indices.
namespace BindlessLayoutType{
    static constexpr u8 s_BindlessLayoutTypeImmutableBase = 0;
    enum Enum : u8{
        Immutable = s_BindlessLayoutTypeImmutableBase, // Must use registerSpaces to define a fixed descriptor type

        MutableSrvUavCbv,   // Global non-sampler resource table
                            // Valid types: Texture_SRV/UAV, TypedBuffer_SRV/UAV, StructuredBuffer_SRV/UAV, RawBuffer_SRV/UAV, ConstantBuffer.

        MutableSampler,     // Global sampler table
                            // Valid descriptor types: Sampler
    };
};

// Bindless layouts describe a global heap array in the shader; `registerSpaces` selects its bindings.
// They are never per-pass tables.
struct BindlessLayoutDesc{
    FixedVector<BindingLayoutItem, s_MaxBindlessRegisterSpaces> registerSpaces;
    u32 maxCapacity = 0;
    u32 descriptorSetIndex = Limit<u32>::s_Max;

    // This resource-bearing layout takes an explicit SPIR-V descriptor set; push-only BindingLayouts take none.
    ShaderType::Mask visibility = ShaderType::None;
    BindlessLayoutType::Enum layoutType = BindlessLayoutType::Immutable;


public:
    constexpr BindlessLayoutDesc& setVisibility(ShaderType::Mask value)noexcept{ visibility = value; return *this; }

    constexpr BindlessLayoutDesc& setMaxCapacity(u32 value)noexcept{ maxCapacity = value; return *this; }

    constexpr BindlessLayoutDesc& addRegisterSpace(const BindingLayoutItem& value){ registerSpaces.push_back(value); return *this; }

    constexpr BindlessLayoutDesc& setLayoutType(BindlessLayoutType::Enum value)noexcept{ layoutType = value; return *this; }

    constexpr BindlessLayoutDesc& setDescriptorSetIndex(u32 value)noexcept{ descriptorSetIndex = value; return *this; }
};

using BindingLayoutHandle = GraphicsBackend::Handle<BindingLayout>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DescriptorWriteItem{
    static constexpr usize s_ByteSize = 40u;

    union{
        TextureSubresourceSet subresources; // valid for Texture_SRV, Texture_UAV
        BufferRange range; // valid for Buffer_SRV, Buffer_UAV, ConstantBuffer
        Array<u64, 2> rawData;
    };
    static_assert(sizeof(TextureSubresourceSet) == TextureSubresourceSet::s_ByteSize, "sizeof(TextureSubresourceSet) is supposed to be 16 bytes");
    static_assert(sizeof(BufferRange) == BufferRange::s_ByteSize, "sizeof(BufferRange) is supposed to be 16 bytes");

    void* resourceHandle;
    u32 slot;
    // Descriptor-array index; must be less than the matching BindingLayoutItem's size.
    u32 arrayElement;
    ResourceType::Enum type          : 8;
    TextureDimension::Enum dimension : 8;
    // valid for Texture_SRV, Texture_UAV
    Format::Enum format              : 8;
    // Valid for Texture_SRV, Texture_UAV, Buffer_SRV, and Buffer_UAV.
    u8 reserved                      : 8;


public:
    static DescriptorWriteItem Base(u32 slot, ResourceType::Enum type, void* resourceHandle, Format::Enum format, TextureDimension::Enum dimension)noexcept;

    static DescriptorWriteItem None(u32 slot = 0)noexcept{
        return Base(slot, ResourceType::None, nullptr, Format::UNKNOWN, TextureDimension::Unknown);
    }

    static DescriptorWriteItem TextureSrv(u32 slot, Texture* texture, Format::Enum format = Format::UNKNOWN, TextureSubresourceSet subresources = s_AllSubresources, TextureDimension::Enum dimension = TextureDimension::Unknown)noexcept;

    static DescriptorWriteItem TextureUav(
        u32 slot,
        Texture* texture,
        Format::Enum format = Format::UNKNOWN,
        TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, TextureSubresourceSet::s_AllArraySlices),
        TextureDimension::Enum dimension = TextureDimension::Unknown
    )noexcept;

    static DescriptorWriteItem TypedBufferSrv(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer)noexcept;

    static DescriptorWriteItem TypedBufferUav(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer)noexcept;

    static DescriptorWriteItem ConstantBuffer(u32 slot, Buffer* buffer, BufferRange range = s_EntireBuffer)noexcept;

    static DescriptorWriteItem Sampler(u32 slot, Core::Sampler* sampler)noexcept{
        return Base(slot, ResourceType::Sampler, sampler, Format::UNKNOWN, TextureDimension::Unknown);
    }

    static DescriptorWriteItem RayTracingAccelStruct(u32 slot, Core::RayTracingAccelStruct* as)noexcept{
        return Base(slot, ResourceType::RayTracingAccelStruct, as, Format::UNKNOWN, TextureDimension::Unknown);
    }

    static DescriptorWriteItem StructuredBufferSrv(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer)noexcept;

    static DescriptorWriteItem StructuredBufferUav(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer)noexcept;

    static DescriptorWriteItem RawBufferSrv(u32 slot, Buffer* buffer, BufferRange range = s_EntireBuffer)noexcept;

    static DescriptorWriteItem RawBufferUav(u32 slot, Buffer* buffer, BufferRange range = s_EntireBuffer)noexcept;

    static DescriptorWriteItem PushConstants(u32 slot, u32 byteSize)noexcept;


public:
    // Intentionally uninitialized to avoid clearing large descriptor arrays.
    DescriptorWriteItem()noexcept{}


public:

    constexpr DescriptorWriteItem& setFormat(Format::Enum value)noexcept{ format = value; return *this; }

    constexpr DescriptorWriteItem& setDimension(TextureDimension::Enum value)noexcept{ dimension = value; return *this; }

    constexpr DescriptorWriteItem& setSubresources(TextureSubresourceSet value)noexcept{ subresources = value; return *this; }

    constexpr DescriptorWriteItem& setRange(BufferRange value)noexcept{ range = value; return *this; }
};
inline b8 operator==(const DescriptorWriteItem& lhs, const DescriptorWriteItem& rhs)noexcept{
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

inline b8 operator!=(const DescriptorWriteItem& lhs, const DescriptorWriteItem& rhs)noexcept{ return !(lhs == rhs); }
static_assert(sizeof(DescriptorWriteItem) == DescriptorWriteItem::s_ByteSize, "sizeof(DescriptorWriteItem) is supposed to be 40 bytes");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

