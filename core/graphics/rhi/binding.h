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
        SamplerFeedbackTexture_UAV,

        kCount
    };
};

struct BindingLayoutItem{
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
    NWB_BINDING_LAYOUT_ITEM_INITIALIZER(SamplerFeedbackTexture_UAV)
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
static_assert(sizeof(BindingLayoutItem) == 8, "sizeof(BindingLayoutItem) is supposed to be 8 bytes");

struct BindingOffsets{
    u32 shaderResource = s_BindingOffsetShaderResource;
    u32 sampler = s_BindingOffsetSampler;
    u32 constantBuffer = s_BindingOffsetConstantBuffer;
    u32 unorderedAccess = s_BindingOffsetUnorderedAccess;

    constexpr BindingOffsets& setShaderResourceOffset(u32 value){ shaderResource = value; return *this; }
    constexpr BindingOffsets& setSamplerOffset(u32 value){ sampler = value; return *this; }
    constexpr BindingOffsets& setConstantBufferOffset(u32 value){ constantBuffer = value; return *this; }
    constexpr BindingOffsets& setUnorderedAccessViewOffset(u32 value){ unorderedAccess = value; return *this; }
};

struct BindingLayoutDesc{
    GraphicsVector<BindingLayoutItem> bindings;
    BindingOffsets bindingOffsets;

    // DXC maps HLSL register spaces to SPIR-V descriptor sets, so this can be used as the descriptor set index.
    // Set `registerSpaceIsDescriptorSet` to enable that mapping explicitly.
    u32 registerSpace = 0;
    ShaderType::Mask visibility = ShaderType::None;

    // This flag controls the behavior for pipelines that use multiple binding layouts.
    // When true, the layout uses `registerSpace` as its SPIR-V descriptor set index. Layouts in the same
    // pipeline must not reuse a descriptor set index.
    bool registerSpaceIsDescriptorSet = false;

    explicit BindingLayoutDesc(GraphicsArena& arena)
        : bindings(arena)
    {}

    constexpr BindingLayoutDesc& setVisibility(ShaderType::Mask value){ visibility = value; return *this; }
    constexpr BindingLayoutDesc& setRegisterSpace(u32 value){ registerSpace = value; return *this; }
    constexpr BindingLayoutDesc& setRegisterSpaceIsDescriptorSet(bool value){ registerSpaceIsDescriptorSet = value; return *this; }
    // Shortcut for .setRegisterSpace(value).setRegisterSpaceIsDescriptorSet(true)
    constexpr BindingLayoutDesc& setRegisterSpaceAndDescriptorSet(u32 value){ registerSpace = value; registerSpaceIsDescriptorSet = true; return *this; }
    BindingLayoutDesc& addItem(const BindingLayoutItem& value){ bindings.push_back(value); return *this; }
    constexpr BindingLayoutDesc& setBindingOffsets(const BindingOffsets& value){ bindingOffsets = value; return *this; }
};

// BindlessDescriptorType describes the SPIR-V bindings DXC emits for HLSL ResourceDescriptorHeap and SamplerDescriptorHeap.
// The shader must be compiled with the same descriptor set index that is passed into setState.
// https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#resourcedescriptorheaps-samplerdescriptorheaps
namespace BindlessLayoutType{
    enum Enum : u8{
        Immutable = 0,      // Must use registerSpaces to define a fixed descriptor type

        MutableSrvUavCbv,   // Corresponds to SPIRV binding -fvk-bind-resource-heap (Counter resources ResourceDescriptorHeap)
                            // Valid descriptor types: Texture_SRV, Texture_UAV, TypedBuffer_SRV, TypedBuffer_UAV,
                            // StructuredBuffer_SRV, StructuredBuffer_UAV, RawBuffer_SRV, RawBuffer_UAV, ConstantBuffer

        MutableCounters,    // Corresponds to SPIRV binding -fvk-bind-counter-heap (Counter resources accessed via ResourceDescriptorHeap)
                            // Valid descriptor types: StructuredBuffer_UAV

        MutableSampler,     // Corresponds to SPIRV binding -fvk-bind-sampler-heap (SamplerDescriptorHeap)
                            // Valid descriptor types: Sampler
    };
};

// Bindless layouts allow applications to attach a descriptor table to an unbounded
// resource array in the shader. The size of the array is not known ahead of time.
// The same table can be bound to multiple HLSL register spaces in order to access
// different types of resources stored in the table through different arrays.
// The `registerSpaces` vector specifies which spaces the table will be bound to,
// with the table type (SRV or UAV) derived from the resource type assigned to each space.
struct BindlessLayoutDesc{
    FixedVector<BindingLayoutItem, s_MaxBindlessRegisterSpaces> registerSpaces;
    u32 firstSlot = 0;
    u32 maxCapacity = 0;
    ShaderType::Mask visibility = ShaderType::None;
    BindlessLayoutType::Enum layoutType = BindlessLayoutType::Immutable;

    constexpr BindlessLayoutDesc& setVisibility(ShaderType::Mask value){ visibility = value; return *this; }
    constexpr BindlessLayoutDesc& setFirstSlot(u32 value){ firstSlot = value; return *this; }
    constexpr BindlessLayoutDesc& setMaxCapacity(u32 value){ maxCapacity = value; return *this; }
    constexpr BindlessLayoutDesc& addRegisterSpace(const BindingLayoutItem& value){ registerSpaces.push_back(value); return *this; }
    constexpr BindlessLayoutDesc& setLayoutType(BindlessLayoutType::Enum value){ layoutType = value; return *this; }
};

typedef GraphicsBackend::Handle<BindingLayout> BindingLayoutHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Sets


struct BindingSetItem{
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
    static_assert(sizeof(TextureSubresourceSet) == 16, "sizeof(TextureSubresourceSet) is supposed to be 16 bytes");
    static_assert(sizeof(BufferRange) == 16, "sizeof(BufferRange) is supposed to be 16 bytes");

    // Default constructor that doesn't initialize anything for performance:
    // BindingSetItem's are stored in large statically sized arrays.
    BindingSetItem(){}

    constexpr BindingSetItem& setArrayElement(u32 value){ arrayElement = value; return *this; }
    constexpr BindingSetItem& setFormat(Format::Enum value){ format = value; return *this; }
    constexpr BindingSetItem& setDimension(TextureDimension::Enum value){ dimension = value; return *this; }
    constexpr BindingSetItem& setSubresources(TextureSubresourceSet value){ subresources = value; return *this; }
    constexpr BindingSetItem& setRange(BufferRange value){ range = value; return *this; }

    static BindingSetItem Base(u32 slot, ResourceType::Enum type, void* resourceHandle, Format::Enum format, TextureDimension::Enum dimension){
        BindingSetItem result;
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

    static BindingSetItem None(u32 slot = 0){
        return Base(slot, ResourceType::None, nullptr, Format::UNKNOWN, TextureDimension::Unknown);
    }
    static BindingSetItem Texture_SRV(u32 slot, Texture* texture, Format::Enum format = Format::UNKNOWN, TextureSubresourceSet subresources = s_AllSubresources, TextureDimension::Enum dimension = TextureDimension::Unknown){
        BindingSetItem result = Base(slot, ResourceType::Texture_SRV, texture, format, dimension);
        result.subresources = subresources;
        return result;
    }
    static BindingSetItem Texture_UAV(u32 slot, Texture* texture, Format::Enum format = Format::UNKNOWN, TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, TextureSubresourceSet::AllArraySlices), TextureDimension::Enum dimension = TextureDimension::Unknown){
        BindingSetItem result = Base(slot, ResourceType::Texture_UAV, texture, format, dimension);
        result.subresources = subresources;
        return result;
    }
    static BindingSetItem TypedBuffer_SRV(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer){
        BindingSetItem result = Base(slot, ResourceType::TypedBuffer_SRV, buffer, format, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static BindingSetItem TypedBuffer_UAV(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer){
        BindingSetItem result = Base(slot, ResourceType::TypedBuffer_UAV, buffer, format, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static BindingSetItem ConstantBuffer(u32 slot, Buffer* buffer, BufferRange range = s_EntireBuffer);
    static BindingSetItem Sampler(u32 slot, Sampler* sampler){
        return Base(slot, ResourceType::Sampler, sampler, Format::UNKNOWN, TextureDimension::Unknown);
    }
    static BindingSetItem RayTracingAccelStruct(u32 slot, RayTracingAccelStruct* as){
        return Base(slot, ResourceType::RayTracingAccelStruct, as, Format::UNKNOWN, TextureDimension::Unknown);
    }
    static BindingSetItem StructuredBuffer_SRV(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer){
        BindingSetItem result = Base(slot, ResourceType::StructuredBuffer_SRV, buffer, format, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static BindingSetItem StructuredBuffer_UAV(u32 slot, Buffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_EntireBuffer){
        BindingSetItem result = Base(slot, ResourceType::StructuredBuffer_UAV, buffer, format, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static BindingSetItem RawBuffer_SRV(u32 slot, Buffer* buffer, BufferRange range = s_EntireBuffer){
        BindingSetItem result = Base(slot, ResourceType::RawBuffer_SRV, buffer, Format::UNKNOWN, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static BindingSetItem RawBuffer_UAV(u32 slot, Buffer* buffer, BufferRange range = s_EntireBuffer){
        BindingSetItem result = Base(slot, ResourceType::RawBuffer_UAV, buffer, Format::UNKNOWN, TextureDimension::Unknown);
        result.range = range;
        return result;
    }
    static BindingSetItem PushConstants(u32 slot, u32 byteSize){
        BindingSetItem result = Base(slot, ResourceType::PushConstants, nullptr, Format::UNKNOWN, TextureDimension::Unknown);
        result.range.byteOffset = 0;
        result.range.byteSize = byteSize;
        return result;
    }
    static BindingSetItem SamplerFeedbackTexture_UAV(u32 slot, SamplerFeedbackTexture* texture){
        BindingSetItem result = Base(slot, ResourceType::SamplerFeedbackTexture_UAV, texture, Format::UNKNOWN, TextureDimension::Unknown);
        result.subresources = s_AllSubresources;
        return result;
    }
};
inline bool operator==(const BindingSetItem& lhs, const BindingSetItem& rhs){
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
inline bool operator!=(const BindingSetItem& lhs, const BindingSetItem& rhs){ return !(lhs == rhs); }
static_assert(sizeof(BindingSetItem) == 40, "sizeof(BindingSetItem) is supposed to be 40 bytes");

struct BindingSetDesc{
    GraphicsVector<BindingSetItem> bindings;

    // Enables automatic liveness tracking of this binding set by command lists.
    // When disabled, the caller must keep the binding set and referenced resources alive until all commands using the binding set have finished.
    bool trackLiveness = true;

    explicit BindingSetDesc(GraphicsArena& arena)
        : bindings(arena)
    {}

    BindingSetDesc& addItem(const BindingSetItem& value){ bindings.push_back(value); return *this; }
    constexpr BindingSetDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
};
inline bool operator==(const BindingSetDesc& lhs, const BindingSetDesc& rhs){
    if(lhs.trackLiveness != rhs.trackLiveness)
        return false;
    if(lhs.bindings.size() != rhs.bindings.size())
        return false;
    for(usize i = 0; i < lhs.bindings.size(); ++i){
        if(lhs.bindings[i] != rhs.bindings[i])
            return false;
    }
    return true;
}
inline bool operator!=(const BindingSetDesc& lhs, const BindingSetDesc& rhs){ return !(lhs == rhs); }

typedef GraphicsBackend::Handle<BindingSet> BindingSetHandle;

// Descriptor tables are bare, without extra mappings, state, or liveness tracking.
// Unlike binding sets, descriptor tables are mutable - moreover, modification is the only way to populate them.
// They can be grown or shrunk, and they are not tied to any binding layout.
// All tracking is off, so applications should use descriptor tables with great care.
typedef GraphicsBackend::Handle<DescriptorTable> DescriptorTableHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

