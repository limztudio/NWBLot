// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "format.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace HeapType{
    enum Enum : u8{
        DeviceLocal,
        Upload,
        Readback,
    };
};

struct HeapDesc{
    u64 capacity = 0;
    Name debugName;
    HeapType::Enum type = HeapType::DeviceLocal;
};

typedef GraphicsBackend::Handle<Heap> HeapHandle;

struct MemoryRequirements{
    u64 size = 0;
    u64 alignment = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureDimension{
    enum Enum : u8{
        Unknown,
        Texture1D,
        Texture1DArray,
        Texture2D,
        Texture2DArray,
        TextureCube,
        TextureCubeArray,
        Texture2DMS,
        Texture2DMSArray,
        Texture3D,
    };
};

namespace CpuAccessMode{
    enum Enum : u8{
        None,
        Read,
        Write,
    };
};

struct StagingTextureMapping{
    void* data = nullptr;
    usize rowPitch = 0u;
};


// Sharing intent for multi-transport resources. Never exposes queue-family indices; a requested set becomes concurrent Vulkan sharing only for the distinct families the device created.
namespace ResourceQueueSharing{
    static constexpr u8 s_ResourceQueueSharingExclusiveBase = 0;
    enum Mask : u8{
        Exclusive = s_ResourceQueueSharingExclusiveBase,
        Graphics = 1 << 0,
        AsyncCompute = 1 << 1,
        Transfer = 1 << 2,
        GraphicsAndAsyncCompute = Graphics | AsyncCompute,
        GraphicsAndTransfer = Graphics | Transfer,
        AsyncComputeAndTransfer = AsyncCompute | Transfer,
        GraphicsAsyncComputeAndTransfer = Graphics | AsyncCompute | Transfer,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)

    [[nodiscard]] inline constexpr bool IsValid(const Mask sharing)noexcept{
        return (static_cast<u8>(sharing) & ~static_cast<u8>(GraphicsAsyncComputeAndTransfer)) == 0u;
    }
};

// Physical admission facts for one resource. The backend owns the family list; consumers retaining the snapshot past the call boundary must copy it.
struct ResourceQueueAdmissionSnapshot{
    const u32* queueFamilyIndices = nullptr;
    u32 queueFamilyIndexCount = 0u;
    ResourceQueueSharing::Mask admittedQueueClasses = ResourceQueueSharing::Exclusive;
    bool usesConcurrentSharing = false;

    [[nodiscard]] constexpr bool valid()const noexcept{
        return ResourceQueueSharing::IsValid(admittedQueueClasses)
            && (
                (
                    !usesConcurrentSharing
                    && queueFamilyIndexCount == 0u
                    && !queueFamilyIndices
                )
                || (
                    usesConcurrentSharing
                    && admittedQueueClasses != ResourceQueueSharing::Exclusive
                    && queueFamilyIndexCount >= 2u
                    && queueFamilyIndices
                )
            )
        ;
    }
};

namespace ResourceStates{
    static constexpr auto s_ResourceStatesUnknownBase = 0;
    enum Mask : u32{
        Unknown = s_ResourceStatesUnknownBase,
        Common = 1 << 0,
        ConstantBuffer = 1 << 1,
        VertexBuffer = 1 << 2,
        IndexBuffer = 1 << 3,
        IndirectArgument = 1 << 4,
        ShaderResource = 1 << 5,
        UnorderedAccess = 1 << 6,
        RenderTarget = 1 << 7,
        DepthWrite = 1 << 8,
        DepthRead = 1 << 9,
        StreamOut = 1 << 10,
        CopyDest = 1 << 11,
        CopySource = 1 << 12,
        ResolveDest = 1 << 13,
        ResolveSource = 1 << 14,
        Present = 1 << 15,
        AccelStructRead = 1 << 16,
        AccelStructWrite = 1 << 17,
        AccelStructBuildInput = 1 << 18,
        AccelStructBuildBlas = 1 << 19,
        ShadingRateSurface = 1 << 20,
        OpacityMicromapWrite = 1 << 21,
        OpacityMicromapBuildInput = 1 << 22,
        ConvertCoopVecMatrixInput = 1 << 23,
        ConvertCoopVecMatrixOutput = 1 << 24,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)

    [[nodiscard]] constexpr bool HasUnorderedAccess(const Mask states)noexcept{
        return (states & UnorderedAccess) != Unknown;
    }
};

typedef u32 MipLevel;
typedef u32 ArraySlice;

struct TextureDesc{
    Name name;
    Color clearValue;
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
    u32 arraySize = 1;
    u32 mipLevels = 1;
    u32 sampleCount = 1;
    u32 sampleQuality = 0;
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    Format::Enum format = Format::UNKNOWN;
    TextureDimension::Enum dimension = TextureDimension::Enum::Texture2D;
    bool isShaderResource = true;
    bool isRenderTarget = false;
    bool isUAV = false;
    bool isTypeless = false;
    bool isShadingRateSurface = false;

    // Indicates that the texture is created with no backing memory, and memory is bound to the texture later using bindTextureMemory.
    bool isVirtual = false;

    bool useClearValue = false;

    // If keepInitialState is true, command lists restore each used subresource to initialState before close. That retained native state becomes globally known only after the restoring command buffer is successfully submitted; a pre-submit cross-list consumer must import CommandListResourceStateHandoff instead.
    bool keepInitialState = false;

    constexpr TextureDesc& setWidth(u32 v)noexcept{ width = v; return *this; }
    constexpr TextureDesc& setHeight(u32 v)noexcept{ height = v; return *this; }
    constexpr TextureDesc& setDepth(u32 v)noexcept{ depth = v; return *this; }
    constexpr TextureDesc& setArraySize(u32 v)noexcept{ arraySize = v; return *this; }
    constexpr TextureDesc& setMipLevels(u32 v)noexcept{ mipLevels = v; return *this; }
    constexpr TextureDesc& setSampleCount(u32 v)noexcept{ sampleCount = v; return *this; }
    constexpr TextureDesc& setFormat(Format::Enum v)noexcept{ format = v; return *this; }
    constexpr TextureDesc& setDimension(TextureDimension::Enum v)noexcept{ dimension = v; return *this; }
    constexpr TextureDesc& setName(const Name& v)noexcept{ name = v; return *this; }
    constexpr TextureDesc& setInRenderTarget(bool v)noexcept{ isRenderTarget = v; return *this; }
    constexpr TextureDesc& setInUAV(bool v)noexcept{ isUAV = v; return *this; }
    constexpr TextureDesc& setInTypeless(bool v)noexcept{ isTypeless = v; return *this; }
    constexpr TextureDesc& setClearValue(const Color& v)noexcept{ clearValue = v; useClearValue = true; return *this; }
    constexpr TextureDesc& setUseClearValue(bool v)noexcept{ useClearValue = v; return *this; }
    constexpr TextureDesc& setInitialState(ResourceStates::Mask v)noexcept{ initialState = v; return *this; }
    constexpr TextureDesc& setKeepInitialState(bool v)noexcept{ keepInitialState = v; return *this; }
    constexpr TextureDesc& setQueueSharing(ResourceQueueSharing::Mask v)noexcept{ queueSharing = v; return *this; }
};

struct TextureSlice{
    static constexpr u32 s_AllDimensions = Limit<u32>::s_Max;

    u32 x = 0;
    u32 y = 0;
    u32 z = 0;

    // s_AllDimensions means the entire dimension is part of the region. resolve() will translate these values into actual dimensions.
    u32 width = s_AllDimensions;
    u32 height = s_AllDimensions;
    u32 depth = s_AllDimensions;

    MipLevel mipLevel = 0;
    ArraySlice arraySlice = 0;

    [[nodiscard]] TextureSlice resolve(const TextureDesc& desc)const;
    [[nodiscard]] TextureSlice resolve(u32 mipWidth, u32 mipHeight, u32 mipDepth)const noexcept;

    constexpr TextureSlice& setOrigin(u32 vx = 0, u32 vy = 0, u32 vz = 0)noexcept{ x = vx; y = vy; z = vz; return *this; }
    constexpr TextureSlice& setWidth(u32 value)noexcept{ width = value; return *this; }
    constexpr TextureSlice& setHeight(u32 value)noexcept{ height = value; return *this; }
    constexpr TextureSlice& setDepth(u32 value)noexcept{ depth = value; return *this; }
    constexpr TextureSlice& setSize(u32 vx = s_AllDimensions, u32 vy = s_AllDimensions, u32 vz = s_AllDimensions)noexcept{ width = vx; height = vy; depth = vz; return *this; }
    constexpr TextureSlice& setMipLevel(MipLevel level)noexcept{ mipLevel = level; return *this; }
    constexpr TextureSlice& setArraySlice(ArraySlice slice)noexcept{ arraySlice = slice; return *this; }
};

namespace TextureSubresourceMipResolve{
    static constexpr u8 s_TextureSubresourceMipResolveRangeBase = 0u;
    enum Enum : u8{
        Range = s_TextureSubresourceMipResolveRangeBase,
        Single,
    };
};

struct TextureSubresourceSet{
    static constexpr auto s_AllMipLevels = static_cast<MipLevel>(-1);
    static constexpr auto s_AllArraySlices = static_cast<ArraySlice>(-1);
    static constexpr usize s_ByteSize = 16u;

    MipLevel baseMipLevel = 0;
    MipLevel numMipLevels = 1;
    ArraySlice baseArraySlice = 0;
    ArraySlice numArraySlices = 1;

    [[nodiscard]] TextureSubresourceSet resolve(const TextureDesc& desc, TextureSubresourceMipResolve::Enum mipResolve)const noexcept;
    [[nodiscard]] bool isEntireTexture(const TextureDesc& desc)const noexcept;

    constexpr TextureSubresourceSet() = default;
    constexpr TextureSubresourceSet(
        MipLevel baseMipLevelValue,
        MipLevel numMipLevelsValue,
        ArraySlice baseArraySliceValue,
        ArraySlice numArraySlicesValue
    )noexcept
        : baseMipLevel(baseMipLevelValue)
        , numMipLevels(numMipLevelsValue)
        , baseArraySlice(baseArraySliceValue)
        , numArraySlices(numArraySlicesValue)
    {}

    constexpr TextureSubresourceSet& setBaseMipLevel(MipLevel value)noexcept{ baseMipLevel = value; return *this; }
    constexpr TextureSubresourceSet& setNumMipLevels(MipLevel value)noexcept{ numMipLevels = value; return *this; }
    constexpr TextureSubresourceSet& setMipLevels(MipLevel base, MipLevel num)noexcept{ baseMipLevel = base; numMipLevels = num; return *this; }
    constexpr TextureSubresourceSet& setBaseArraySlice(ArraySlice value)noexcept{ baseArraySlice = value; return *this; }
    constexpr TextureSubresourceSet& setNumArraySlices(ArraySlice value)noexcept{ numArraySlices = value; return *this; }
    constexpr TextureSubresourceSet& setArraySlices(ArraySlice base, ArraySlice num)noexcept{ baseArraySlice = base; numArraySlices = num; return *this; }

    [[nodiscard]] static constexpr u64 RangeEnd(const u32 base, const u32 count, const u32 all)noexcept{
        return count == all ? Limit<u64>::s_Max : static_cast<u64>(base) + static_cast<u64>(count);
    }

    [[nodiscard]] constexpr bool hasExtent()const noexcept{
        return numMipLevels != 0u && numArraySlices != 0u;
    }

    [[nodiscard]] constexpr u64 mipEnd()const noexcept{
        return RangeEnd(baseMipLevel, numMipLevels, s_AllMipLevels);
    }

    [[nodiscard]] constexpr u64 arrayEnd()const noexcept{
        return RangeEnd(baseArraySlice, numArraySlices, s_AllArraySlices);
    }

    [[nodiscard]] constexpr bool contains(const TextureSubresourceSet& inner)const noexcept{
        return baseMipLevel <= inner.baseMipLevel
            && mipEnd() >= inner.mipEnd()
            && baseArraySlice <= inner.baseArraySlice
            && arrayEnd() >= inner.arrayEnd()
        ;
    }

    [[nodiscard]] constexpr bool overlaps(const TextureSubresourceSet& other)const noexcept{
        return baseMipLevel < other.mipEnd()
            && other.baseMipLevel < mipEnd()
            && baseArraySlice < other.arrayEnd()
            && other.baseArraySlice < arrayEnd()
        ;
    }
};
inline bool operator==(const TextureSubresourceSet& lhs, const TextureSubresourceSet& rhs)noexcept{
    return
        lhs.baseMipLevel == rhs.baseMipLevel
        && lhs.numMipLevels == rhs.numMipLevels
        && lhs.baseArraySlice == rhs.baseArraySlice
        && lhs.numArraySlices == rhs.numArraySlices
    ;
}
inline bool operator!=(const TextureSubresourceSet& lhs, const TextureSubresourceSet& rhs)noexcept{ return !(lhs == rhs); }

inline constexpr auto s_AllSubresources = TextureSubresourceSet(0, TextureSubresourceSet::s_AllMipLevels, 0, TextureSubresourceSet::s_AllArraySlices);

typedef GraphicsBackend::Handle<Texture> TextureHandle;

typedef GraphicsBackend::Handle<StagingTexture> StagingTextureHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct VertexAttributeDesc{
    Name name;
    u32 arraySize = 1;
    u32 bufferIndex = 0;
    u32 offset = 0;
    u32 elementStride = 0;
    Format::Enum format = Format::UNKNOWN;
    bool isInstanced = false;

    constexpr VertexAttributeDesc& setFormat(Format::Enum value)noexcept{ format = value; return *this; }
    constexpr VertexAttributeDesc& setArraySize(u32 value)noexcept{ arraySize = value; return *this; }
    constexpr VertexAttributeDesc& setBufferIndex(u32 value)noexcept{ bufferIndex = value; return *this; }
    constexpr VertexAttributeDesc& setOffset(u32 value)noexcept{ offset = value; return *this; }
    constexpr VertexAttributeDesc& setElementStride(u32 value)noexcept{ elementStride = value; return *this; }
    constexpr VertexAttributeDesc& setName(const Name& value)noexcept{ name = value; return *this; }
    constexpr VertexAttributeDesc& setIsInstanced(bool value)noexcept{ isInstanced = value; return *this; }
};

typedef GraphicsBackend::Handle<InputLayout> InputLayoutHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BufferDesc{
    Name debugName;
    u64 byteSize = 0;
    u32 structStride = 0; // if non-zero it's structured
    u32 maxVersions = 0; // only valid and required to be nonzero for volatile buffers on backends that keep per-version state
    ResourceStates::Mask initialState = ResourceStates::Common;
    Format::Enum format = Format::UNKNOWN; // for typed buffer views
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    CpuAccessMode::Enum cpuAccess = CpuAccessMode::None;
    bool canHaveUAVs = false;
    bool canHaveTypedViews = false;
    bool canHaveRawViews = false;
    bool isVertexBuffer = false;
    bool isIndexBuffer = false;
    bool isConstantBuffer = false;
    bool isDrawIndirectArgs = false;
    bool isAccelStructBuildInput = false;
    bool isAccelStructStorage = false;
    bool isShaderBindingTable = false;

    // A dynamic/upload buffer whose contents only live in the current command list
    bool isVolatile = false;

    // Indicates that the buffer is created with no backing memory, and memory is bound to the buffer later using bindBufferMemory.
    bool isVirtual = false;

    // see TextureDesc::keepInitialState
    bool keepInitialState = false;

    constexpr BufferDesc& setByteSize(u64 value)noexcept{ byteSize = value; return *this; }
    constexpr BufferDesc& setStructStride(u32 value)noexcept{ structStride = value; return *this; }
    constexpr BufferDesc& setMaxVersions(u32 value)noexcept{ maxVersions = value; return *this; }
    constexpr BufferDesc& setFormat(Format::Enum value)noexcept{ format = value; return *this; }
    constexpr BufferDesc& setDebugName(const Name& value)noexcept{ debugName = value; return *this; }
    constexpr BufferDesc& setCanHaveUAVs(bool value)noexcept{ canHaveUAVs = value; return *this; }
    constexpr BufferDesc& setCanHaveTypedViews(bool value)noexcept{ canHaveTypedViews = value; return *this; }
    constexpr BufferDesc& setCanHaveRawViews(bool value)noexcept{ canHaveRawViews = value; return *this; }
    constexpr BufferDesc& setIsVertexBuffer(bool value)noexcept{ isVertexBuffer = value; return *this; }
    constexpr BufferDesc& setIsIndexBuffer(bool value)noexcept{ isIndexBuffer = value; return *this; }
    constexpr BufferDesc& setIsConstantBuffer(bool value)noexcept{ isConstantBuffer = value; return *this; }
    constexpr BufferDesc& setIsDrawIndirectArgs(bool value)noexcept{ isDrawIndirectArgs = value; return *this; }
    constexpr BufferDesc& setIsAccelStructBuildInput(bool value)noexcept{ isAccelStructBuildInput = value; return *this; }
    constexpr BufferDesc& setIsAccelStructStorage(bool value)noexcept{ isAccelStructStorage = value; return *this; }
    constexpr BufferDesc& setIsShaderBindingTable(bool value)noexcept{ isShaderBindingTable = value; return *this; }
    constexpr BufferDesc& setIsVolatile(bool value)noexcept{ isVolatile = value; return *this; }
    constexpr BufferDesc& setIsVirtual(bool value)noexcept{ isVirtual = value; return *this; }
    constexpr BufferDesc& setInitialState(ResourceStates::Mask value)noexcept{ initialState = value; return *this; }
    constexpr BufferDesc& setKeepInitialState(bool value)noexcept{ keepInitialState = value; return *this; }
    constexpr BufferDesc& setQueueSharing(ResourceQueueSharing::Mask value)noexcept{ queueSharing = value; return *this; }
    constexpr BufferDesc& setCpuAccess(CpuAccessMode::Enum value)noexcept{ cpuAccess = value; return *this; }

    // Equivalent to .setInitialState(initialStateValue).setKeepInitialState(true)
    constexpr BufferDesc& enableAutomaticStateTracking(ResourceStates::Mask initialStateValue)noexcept{
        initialState = initialStateValue;
        keepInitialState = true;
        return *this;
    }
};

struct BufferRange{
    // s_AllBytes marks an unbounded range; resolve() clamps it to the buffer's byte size.
    static constexpr u64 s_AllBytes = Limit<u64>::s_Max;
    static constexpr usize s_ByteSize = 16u;

    u64 byteOffset = 0;
    u64 byteSize = 0;

    BufferRange() = default;
    constexpr BufferRange(u64 byteOffsetValue, u64 byteSizeValue)noexcept
        : byteOffset(byteOffsetValue)
        , byteSize(byteSizeValue)
    {}

    [[nodiscard]] BufferRange resolve(const BufferDesc& desc)const noexcept;
    [[nodiscard]] constexpr bool hasExtent()const noexcept{
        return byteSize != 0u && byteOffset < s_AllBytes && (byteSize == s_AllBytes || byteSize <= s_AllBytes - byteOffset);
    }
    [[nodiscard]] constexpr u64 end()const noexcept{ return byteSize == s_AllBytes ? s_AllBytes : byteOffset + byteSize; }
    [[nodiscard]] constexpr bool overlaps(const BufferRange& other)const noexcept{
        return hasExtent() && other.hasExtent() && byteOffset < other.end() && other.byteOffset < end();
    }
    [[nodiscard]] constexpr bool contains(const BufferRange& other)const noexcept{
        return hasExtent() && other.hasExtent() && byteOffset <= other.byteOffset && end() >= other.end();
    }
    [[nodiscard]] BufferRange intersect(const BufferRange& other)const noexcept;
    [[nodiscard]] constexpr bool isEntireBuffer(const BufferDesc& desc)const noexcept{ return (!byteOffset) && (byteSize == s_AllBytes || byteSize == desc.byteSize); }
    constexpr bool operator==(const BufferRange& other)const noexcept{ return byteOffset == other.byteOffset && byteSize == other.byteSize; }

    constexpr BufferRange& setByteOffset(u64 value)noexcept{ byteOffset = value; return *this; }
    constexpr BufferRange& setByteSize(u64 value)noexcept{ byteSize = value; return *this; }
};

inline constexpr BufferRange s_EntireBuffer = BufferRange(0, BufferRange::s_AllBytes);

typedef GraphicsBackend::Handle<Buffer> BufferHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

