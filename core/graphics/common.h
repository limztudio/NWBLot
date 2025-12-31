// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/common/common.h>
#include <core/alloc/assetPool.h>

#include "basic.h"
#include "shader.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_maxRenderTargets = 8;
static constexpr u32 s_maxViewports = 16;
static constexpr u32 s_maxVertexAttributes = 16;
static constexpr u32 s_maxBindingLayouts = 8;
static constexpr u32 s_maxBindlessRegisterSpaces = 16;
static constexpr u32 s_maxVolatileConstantBuffersPerLayout = 6;
static constexpr u32 s_maxVolatileConstantBuffers = 32;
static constexpr u32 s_maxPushConstantSize = 128;
static constexpr u32 s_constantBufferOffsetSizeAlignment = 256;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Basic types

struct Color{
    f32 r, g, b, a;
    
    constexpr Color()noexcept : r(0), g(0), b(0), a(0) {}
    constexpr Color(f32 c)noexcept : r(c), g(c), b(c), a(c) {}
    constexpr Color(f32 _r, f32 _g, f32 _b, f32 _a)noexcept : r(_r), g(_g), b(_b), a(_a) {}
};
inline bool operator==(const Color& lhs, const Color& rhs)noexcept{
    return (lhs.r == rhs.r)
    && (lhs.g == rhs.g)
    && (lhs.b == rhs.b)
    && (lhs.a == rhs.a)
    ;
}
inline bool operator!=(const Color& lhs, const Color& rhs)noexcept{ return !(lhs == rhs); }


struct Viewport{
    f32 minX, maxX;
    f32 minY, maxY;
    f32 minZ, maxZ;
    
    constexpr Viewport()noexcept : minX(0), maxX(0), minY(0), maxY(0), minZ(0), maxZ(0) {}
    constexpr Viewport(f32 width, f32 height)noexcept : minX(0), maxX(width), minY(0), maxY(height), minZ(0), maxZ(1) {}
    constexpr Viewport(f32 _minX, f32 _maxX, f32 _minY, f32 _maxY, f32 _minZ, f32 _maxZ)noexcept : minX(_minX), maxX(_maxX), minY(_minY), maxY(_maxY), minZ(_minZ), maxZ(_maxZ) {}
    
    [[nodiscard]] constexpr f32 width()const noexcept{ return maxX - minX; }
    [[nodiscard]] constexpr f32 height()const noexcept{ return maxY - minY; }
};
inline bool operator==(const Viewport& lhs, const Viewport& rhs)noexcept{
    return (lhs.minX == rhs.minX) && (lhs.maxX == rhs.maxX)
    && (lhs.minY == rhs.minY) && (lhs.maxY == rhs.maxY)
    && (lhs.minZ == rhs.minZ) && (lhs.maxZ == rhs.maxZ)
    ;
}
inline bool operator!=(const Viewport& lhs, const Viewport& rhs)noexcept{ return !(lhs == rhs); }


struct Rect{
    i32 minX, maxX;
    i32 minY, maxY;
    
    constexpr Rect()noexcept : minX(0), maxX(0), minY(0), maxY(0) {}
    constexpr Rect(i32 width, i32 height)noexcept : minX(0), maxX(width), minY(0), maxY(height) {}
    constexpr Rect(i32 _minX, i32 _maxX, i32 _minY, i32 _maxY)noexcept : minX(_minX), maxX(_maxX), minY(_minY), maxY(_maxY) {}
    constexpr explicit Rect(const Viewport& viewport)noexcept : minX(static_cast<i32>(floor(viewport.minX))), maxX(static_cast<i32>(ceil(viewport.maxX))), minY(static_cast<i32>(floor(viewport.minY))), maxY(static_cast<i32>(ceil(viewport.maxY))) {}
    
    [[nodiscard]] constexpr i32 width()const noexcept{ return maxX - minX; }
    [[nodiscard]] constexpr i32 height()const noexcept{ return maxY - minY; }
};
inline bool operator==(const Rect& lhs, const Rect& rhs)noexcept{
    return (lhs.minX == rhs.minX) && (lhs.maxX == rhs.maxX)
    && (lhs.minY == rhs.minY) && (lhs.maxY == rhs.maxY)
    ;
}
inline bool operator!=(const Rect& lhs, const Rect& rhs)noexcept{ return !(lhs == rhs); }


namespace Format{
    enum Enum : u8{
        UNKNOWN,

        R8_UINT,
        R8_SINT,
        R8_UNORM,
        R8_SNORM,
        RG8_UINT,
        RG8_SINT,
        RG8_UNORM,
        RG8_SNORM,
        R16_UINT,
        R16_SINT,
        R16_UNORM,
        R16_SNORM,
        R16_FLOAT,
        BGRA4_UNORM,
        B5G6R5_UNORM,
        B5G5R5A1_UNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_UNORM,
        RGBA8_SNORM,
        BGRA8_UNORM,
        BGRX8_UNORM,
        SRGBA8_UNORM,
        SBGRA8_UNORM,
        SBGRX8_UNORM,
        R10G10B10A2_UNORM,
        R11G11B10_FLOAT,
        RG16_UINT,
        RG16_SINT,
        RG16_UNORM,
        RG16_SNORM,
        RG16_FLOAT,
        R32_UINT,
        R32_SINT,
        R32_FLOAT,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_FLOAT,
        RGBA16_UNORM,
        RGBA16_SNORM,
        RG32_UINT,
        RG32_SINT,
        RG32_FLOAT,
        RGB32_UINT,
        RGB32_SINT,
        RGB32_FLOAT,
        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_FLOAT,
        
        D16,
        D24S8,
        X24G8_UINT,
        D32,
        D32S8,
        X32G8_UINT,

        BC1_UNORM,
        BC1_UNORM_SRGB,
        BC2_UNORM,
        BC2_UNORM_SRGB,
        BC3_UNORM,
        BC3_UNORM_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_UNORM_SRGB,
        
        // ASTC_4x4_UNORM,
        // ASTC_4x4_UNORM_SRGB,
        // ASTC_4x4_FLOAT,
        // ASTC_5x4_UNORM,
        // ASTC_5x4_UNORM_SRGB,
        // ASTC_5x4_FLOAT,
        // ASTC_5x5_UNORM,
        // ASTC_5x5_UNORM_SRGB,
        // ASTC_5x5_FLOAT,
        // ASTC_6x5_UNORM,
        // ASTC_6x5_UNORM_SRGB,
        // ASTC_6x5_FLOAT,
        // ASTC_6x6_UNORM,
        // ASTC_6x6_UNORM_SRGB,
        // ASTC_6x6_FLOAT,
        // ASTC_8x5_UNORM,
        // ASTC_8x5_UNORM_SRGB,
        // ASTC_8x5_FLOAT,
        // ASTC_8x6_UNORM,
        // ASTC_8x6_UNORM_SRGB,
        // ASTC_8x6_FLOAT,
        // ASTC_8x8_UNORM,
        // ASTC_8x8_UNORM_SRGB,
        // ASTC_8x8_FLOAT,
        // ASTC_10x5_UNORM,
        // ASTC_10x5_UNORM_SRGB,
        // ASTC_10x5_FLOAT,
        // ASTC_10x6_UNORM,
        // ASTC_10x6_UNORM_SRGB,
        // ASTC_10x6_FLOAT,
        // ASTC_10x8_UNORM,
        // ASTC_10x8_UNORM_SRGB,
        // ASTC_10x8_FLOAT,
        // ASTC_10x10_UNORM,
        // ASTC_10x10_UNORM_SRGB,
        // ASTC_10x10_FLOAT,
        // ASTC_12x10_UNORM,
        // ASTC_12x10_UNORM_SRGB,
        // ASTC_12x10_FLOAT,
        // ASTC_12x12_UNORM,
        // ASTC_12x12_UNORM_SRGB,
        // ASTC_12x12_FLOAT,
        
        kCount
    };
};

namespace FormatKind{
    enum Enum : u8{
        Integer,
        Normalized,
        Float,
        DepthStencil,

        kCount
    };
};

struct FormatInfo{
    Format::Enum format;
    const char* name;
    u8 bytesPerBlock;
    u8 blockSize;
    FormatKind::Enum kind;
    bool hasRed : 1;
    bool hasGreen : 1;
    bool hasBlue : 1;
    bool hasAlpha : 1;
    bool hasDepth : 1;
    bool hasStencil : 1;
    bool isSigned : 1;
    bool isSRGB : 1;
};

const FormatInfo& GetFormatInfo(Format::Enum format)noexcept;

namespace FormatSupport{
    enum Mask : u32{
        None = 0,
        
        Buffer = 1 << 0,
        IndexBuffer = 1 << 1,
        VertexBuffer = 1 << 2,
        
        Texture = 1 << 3,
        DepthStencil = 1 << 4,
        RenderTarget = 1 << 5,
        Blendable = 1 << 6,
        
        ShaderLoad = 1 << 7,
        ShaderSample = 1 << 8,
        ShaderUavLoad = 1 << 9,
        ShaderUavStore = 1 << 10,
        ShaderAtomicCounter = 1 << 11,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value) noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value) noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Heap


namespace HeapType{
    enum Enum : u8{
        DeviceLocal,
        Upload,
        Readback,
    };
};

struct HeapDesc{
    u64 capacity  = 0;
    HeapType::Enum type;
#if defined(NWB_DEBUG)
    AString debugName;
#endif
};

class IHeap : public IResource{
public:
    [[nodiscard]] virtual const HeapDesc& getDescription()const = 0;
};
typedef RefCountPtr<IHeap, BlankDeleter<IHeap>> HeapHandle;

struct MemoryRequirements{
    u64 size = 0;
    u64 alignment = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture


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

namespace ResourceStates{
    enum Mask : u32{
        Unknown = 0,
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
    
    inline Mask operator|(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value) noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value) noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

typedef u32 MipLevel;
typedef u32 ArraySlice;

namespace SharedResourceFlags{
    enum Mask : u32{
        None                = 0,

        // D3D11: adds D3D11_RESOURCE_MISC_SHARED
        // D3D12: adds D3D12_HEAP_FLAG_SHARED
        // Vulkan: adds vk::ExternalMemoryImageCreateInfo and vk::ExportMemoryAllocateInfo/vk::ExternalMemoryBufferCreateInfo
        Shared              = 1 << 0,

        // D3D11: adds (D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
        // D3D12, Vulkan: ignored
        Shared_NTHandle     = 1 << 1,

        // D3D12: adds D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER and D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER
        // D3D11, Vulkan: ignored
        Shared_CrossAdapter = 1 << 2,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value) noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value) noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

struct TextureDesc{
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
    u32 arraySize = 1;
    u32 mipLevels = 1;
    u32 sampleCount = 1;
    u32 sampleQuality = 0;
    Format::Enum format = Format::UNKNOWN;
    TextureDimension::Enum dimension = TextureDimension::Enum::Texture2D;
#ifdef NWB_GRAPHICS_DEBUGGABLE
    AString name;
#endif
    
    bool isShaderResource = true; // Thi is initialised to 'true' for backward compatibility
    bool isRenderTarget = false;
    bool isUAV = false;
    bool isTypeless = false;
    bool isShadingRateSurface = false;
    
    SharedResourceFlags::Mask sharedResourceFlags = SharedResourceFlags::None;
    
    // Indicates that the texture is created with no backing memory,
    // and memory is bound to the texture later using bindTextureMemory.
    // On DX12, the texture resource is created at the time of memory binding.
    bool isVirtual = false;
    bool isTiled = false;
    
    Color clearValue;
    bool useClearValue = false;
    
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    
    // If keepInitialState is true, command lists that use the texture will automatically
    // begin tracking the texture from the initial state and transition it to the initial state 
    // on command list close.
    bool keepInitialState = false;
    
    constexpr TextureDesc& setWidth(u32 v)noexcept{ width = v; return *this; }
    constexpr TextureDesc& setHeight(u32 v)noexcept{ height = v; return *this; }
    constexpr TextureDesc& setDepth(u32 v)noexcept{ depth = v; return *this; }
    constexpr TextureDesc& setArraySize(u32 v)noexcept{ arraySize = v; return *this; }
    constexpr TextureDesc& setMipLevels(u32 v)noexcept{ mipLevels = v; return *this; }
    constexpr TextureDesc& setSampleCount(u32 v)noexcept{ sampleCount = v; return *this; }
    constexpr TextureDesc& setFormat(Format::Enum v)noexcept{ format = v; return *this; }
    constexpr TextureDesc& setDimension(TextureDimension::Enum v)noexcept{ dimension = v; return *this; }
#ifdef NWB_GRAPHICS_DEBUGGABLE
    TextureDesc& setName(const AString& v)noexcept{ name = v; return *this; }
#endif
    constexpr TextureDesc& setInRenderTarget(bool v)noexcept{ isRenderTarget = v; return *this; }
    constexpr TextureDesc& setInUAV(bool v)noexcept{ isUAV = v; return *this; }
    constexpr TextureDesc& setInTypeless(bool v)noexcept{ isTypeless = v; return *this; }
    constexpr TextureDesc& setClearValue(const Color& v)noexcept{ clearValue = v; useClearValue = true; return *this; }
    constexpr TextureDesc& setUseClearValue(bool v)noexcept{ useClearValue = v; return *this; }
    constexpr TextureDesc& setInitialState(ResourceStates::Mask v)noexcept{ initialState = v; return *this; }
    constexpr TextureDesc& setKeepInitialState(bool v)noexcept{ keepInitialState = v; return *this; }
    constexpr TextureDesc& setSharedResourceFlags(SharedResourceFlags::Mask v)noexcept{ sharedResourceFlags = v; return *this; }
};

struct TextureSlice{
    u32 x = 0;
    u32 y = 0;
    u32 z = 0;
    
    // -1 means the entire dimension is part of the region
    // resolve() will translate these values into actual dimensions
    auto width = static_cast<u32>(-1);
    auto height = static_cast<u32>(-1);
    auto depth = static_cast<u32>(-1);
    
    MipLevel mipLevel = 0;
    ArraySlice arraySlice = 0;
    
    [[nodiscard]] TextureSlice resolve(const TextureDesc& desc)const;
    
    constexpr TextureSlice& setOrigin(u32 vx = 0, u32 vy = 0, u32 vz = 0){ x = vx; y = vy; z = vz; return *this; }
    constexpr TextureSlice& setWidth(u32 value){ width = value; return *this; }
    constexpr TextureSlice& setHeight(u32 value){ height = value; return *this; }
    constexpr TextureSlice& setDepth(u32 value){ depth = value; return *this; }
    constexpr TextureSlice& setSize(u32 vx = static_cast<u32>(-1), u32 vy = static_cast<u32>(-1), u32 vz = static_cast<u32>(-1)){ width = vx; height = vy; depth = vz; return *this; }
    constexpr TextureSlice& setMipLevel(MipLevel level){ mipLevel = level; return *this; }
    constexpr TextureSlice& setArraySlice(ArraySlice slice){ arraySlice = slice; return *this; }
};

struct TextureSubresourceSet{
    static constexpr auto AllMipLevels = static_cast<MipLevel>(-1);
    static constexpr auto AllArraySlices = static_cast<ArraySlice>(-1);
    
    MipLevel baseMipLevel = 0;
    MipLevel numMipLevels = 1;
    ArraySlice baseArraySlice = 0;
    ArraySlice numArraySlices = 1;
    
    [[nodiscard]] TextureSubresourceSet resolve(const TextureDesc& desc, bool singleMipLevel)const;
    [[nodiscard]] bool isEntireTexture(const TextureDesc& desc)const;
    
    constexpr TextureSubresourceSet() = default;
    constexpr TextureSubresourceSet(MipLevel _baseMipLevel, MipLevel _numMipLevels, ArraySlice _baseArraySlice, ArraySlice _numArraySlices)
    : baseMipLevel(_baseMipLevel)
    , numMipLevels(_numMipLevels)
    , baseArraySlice(_baseArraySlice)
    , numArraySlices(_numArraySlices)
    {}
    
    constexpr TextureSubresourceSet& setBaseMipLevel(MipLevel value){ baseMipLevel = value; return *this; }
    constexpr TextureSubresourceSet& setNumMipLevels(MipLevel value){ numMipLevels = value; return *this; }
    constexpr TextureSubresourceSet& setMipLevels(MipLevel base, MipLevel num){ baseMipLevel = base; numMipLevels = num; return *this; }
    constexpr TextureSubresourceSet& setBaseArraySlice(ArraySlice value){ baseArraySlice = value; return *this; }
    constexpr TextureSubresourceSet& setNumArraySlices(ArraySlice value){ numArraySlices = value; return *this; }
    constexpr TextureSubresourceSet& setArraySlices(ArraySlice base, ArraySlice num){ baseArraySlice = base; numArraySlices = num; return *this; }

};
inline bool operator==(const TextureSubresourceSet& lhs, const TextureSubresourceSet& rhs)noexcept{
    return lhs.baseMipLevel == rhs.baseMipLevel
        && lhs.numMipLevels == rhs.numMipLevels
        && lhs.baseArraySlice == rhs.baseArraySlice
        && lhs.numArraySlices == rhs.numArraySlices
        ;
}
inline bool operator!=(const TextureSubresourceSet& lhs, const TextureSubresourceSet& rhs)noexcept{ return !(lhs == rhs); }

static constexpr auto s_allSubresources = TextureSubresourceSet(0, TextureSubresourceSet::AllMipLevels, 0, TextureSubresourceSet::AllArraySlices);

class ITexture : public IResource{
public:
    [[nodiscard]] virtual const TextureDesc& getDescription()const = 0;
    
    // Similar to getNativeObject, returns a native view for a specified set of subresources. Returns nullptr if unavailable.
    virtual Object getNativeView(ObjectType objectType, Format::Enum format = Format::UNKNOWN, TextureSubresourceSet subresources = s_allSubresources, TextureDimension::Enum dimension = TextureDimension::Unknown, bool isReadOnlyDSV = false) = 0;
};
typedef RefCountPtr<ITexture, BlankDeleter<ITexture>> TextureHandle;

class IStagingTexture : public IResource{
public:
    [[nodiscard]] virtual const TextureDesc& getDescription()const = 0;
};
typedef RefCountPtr<IStagingTexture, BlankDeleter<IStagingTexture>> StagingTextureHandle;

struct TiledTextureCoordinate{
    u16 mipLevel = 0;
    u16 arrayLevel = 0;
    u32 x = 0;
    u32 y = 0;
    u32 z = 0;
};
struct TiledTextureRegion{
    u32 tilesNum = 0;
    u32 width = 0;
    u32 height = 0;
    u32 depth = 0;
};

struct TextureTilesMapping{
    TiledTextureCoordinate* tiledTextureCoordinates = nullptr;
    TiledTextureRegion* tiledTextureRegions = nullptr;
    u64* byteOffsets = nullptr;
    u32 numTextureRegions = 0;
    IHeap* heap = nullptr;
};

struct PackedMipDesc{
    u32 numStandardMips = 0;
    u32 numPackedMips = 0;
    u32 numTilesForPackedMips = 0;
    u32 startTileIndexInOverallResource = 0;
};

struct TileShape{
    u32 widthInTexels = 0;
    u32 heightInTexels = 0;
    u32 depthInTexels = 0;
};

struct SubresourceTiling{
    u32 widthInTiles = 0;
    u32 heightInTiles = 0;
    u32 depthInTiles = 0;
    u32 startTileIndexInOverallResource = 0;
};

namespace SamplerFeedbackFormat{
    enum Enum : u8{
        MinMipOpaque = 0x0,
        MipRegionUsedOpaque = 0x1,
    };
};

struct SamplerFeedbackTextureDesc{
    SamplerFeedbackFormat::Enum samplerFeedbackFormat = SamplerFeedbackFormat::MinMipOpaque;
    u32 samplerFeedbackMipRegionX = 0;
    u32 samplerFeedbackMipRegionY = 0;
    u32 samplerFeedbackMipRegionZ = 0;
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    bool keepInitialState = false;
};

class ISamplerFeedbackTexture : public IResource{
public:
    [[nodiscard]] virtual const SamplerFeedbackTextureDesc& getDescription()const = 0;
    
    virtual TextureHandle getPairedTexture() = 0;
};
typedef RefCountPtr<ISamplerFeedbackTexture, BlankDeleter<ISamplerFeedbackTexture>> SamplerFeedbackTextureHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input Layout


struct VertexAttributeDesc{
    Format::Enum format = Format::UNKNOWN;
    u32 arraySize = 1;
    u32 bufferIndex = 0;
    u32 offset = 0;
    u32 elementStride = 0;
#ifdef NWB_GRAPHICS_DEBUGGABLE
    AString name;
#endif
    bool isInstanced = false;
    
    constexpr VertexAttributeDesc& setFormat(Format::Enum value){ format = value; return *this; }
    constexpr VertexAttributeDesc& setArraySize(u32 value){ arraySize = value; return *this; }
    constexpr VertexAttributeDesc& setBufferIndex(u32 value){ bufferIndex = value; return *this; }
    constexpr VertexAttributeDesc& setOffset(u32 value){ offset = value; return *this; }
    constexpr VertexAttributeDesc& setElementStride(u32 value){ elementStride = value; return *this; }
    constexpr VertexAttributeDesc& setIsInstanced(bool value){ isInstanced = value; return *this; }
#ifdef NWB_GRAPHICS_DEBUGGABLE
    VertexAttributeDesc& setName(const AString& value){ name = value; return *this; }
#endif
};

class IInputLayout : public IResource{
public:
    [[nodiscard]] virtual const VertexAttributeDesc* getAttributeDescription(u32 index)const = 0;
    [[nodiscard]] virtual u32 getNumAttributes()const = 0;
};
typedef RefCountPtr<IInputLayout, BlankDeleter<IInputLayout>> InputLayoutHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffer


struct BufferDesc{
    u64 byteSize = 0;
    u32 structStride = 0; // if non-zero it's structured
    u32 maxVersions = 0; // only valid and required to be nonzero for volatile buffers on Vulkan
    Format::Enum format = Format::UNKNOWN; // for typed buffer views
#ifdef NWB_GRAPHICS_DEBUGGABLE
    AString debugName;
#endif
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

    // Indicates that the buffer is created with no backing memory,
    // and memory is bound to the buffer later using bindBufferMemory.
    bool isVirtual = false;

    ResourceStates::Mask initialState = ResourceStates::Common;

    // see TextureDesc::keepInitialState
    bool keepInitialState = false;

    CpuAccessMode::Enum cpuAccess = CpuAccessMode::None;

    SharedResourceFlags::Mask sharedResourceFlags = SharedResourceFlags::None;
    
    constexpr BufferDesc& setByteSize(u64 value){ byteSize = value; return *this; }
    constexpr BufferDesc& setStructStride(u32 value){ structStride = value; return *this; }
    constexpr BufferDesc& setMaxVersions(u32 value){ maxVersions = value; return *this; }
    constexpr BufferDesc& setFormat(Format::Enum value){ format = value; return *this; }
    constexpr BufferDesc& setCanHaveUAVs(bool value){ canHaveUAVs = value; return *this; }
    constexpr BufferDesc& setCanHaveTypedViews(bool value){ canHaveTypedViews = value; return *this; }
    constexpr BufferDesc& setCanHaveRawViews(bool value){ canHaveRawViews = value; return *this; }
    constexpr BufferDesc& setIsVertexBuffer(bool value){ isVertexBuffer = value; return *this; }
    constexpr BufferDesc& setIsIndexBuffer(bool value){ isIndexBuffer = value; return *this; }
    constexpr BufferDesc& setIsConstantBuffer(bool value){ isConstantBuffer = value; return *this; }
    constexpr BufferDesc& setIsDrawIndirectArgs(bool value){ isDrawIndirectArgs = value; return *this; }
    constexpr BufferDesc& setIsAccelStructBuildInput(bool value){ isAccelStructBuildInput = value; return *this; }
    constexpr BufferDesc& setIsAccelStructStorage(bool value){ isAccelStructStorage = value; return *this; }
    constexpr BufferDesc& setIsShaderBindingTable(bool value){ isShaderBindingTable = value; return *this; }
    constexpr BufferDesc& setIsVolatile(bool value){ isVolatile = value; return *this; }
    constexpr BufferDesc& setIsVirtual(bool value){ isVirtual = value; return *this; }
    constexpr BufferDesc& setInitialState(ResourceStates::Mask value){ initialState = value; return *this; }
    constexpr BufferDesc& setKeepInitialState(bool value){ keepInitialState = value; return *this; }
    constexpr BufferDesc& setCpuAccess(CpuAccessMode::Enum value){ cpuAccess = value; return *this; }

    // Equivalent to .setInitialState(_initialState).setKeepInitialState(true)
    constexpr BufferDesc& enableAutomaticStateTracking(ResourceStates::Mask _initialState){
        initialState = _initialState;
        keepInitialState = true;
        return *this;
    }
    
#ifdef NWB_GRAPHICS_DEBUGGABLE
    BufferDesc& setDebugName(const AString& value){ debugName = value; return *this; }
#endif
};

struct BufferRange{
    u64 byteOffset = 0;
    u64 byteSize = 0;
        
    BufferRange() = default;
    constexpr BufferRange(u64 _byteOffset, u64 _byteSize)
        : byteOffset(_byteOffset)
        , byteSize(_byteSize)
    {}

    [[nodiscard]] BufferRange resolve(const BufferDesc& desc)const;
    [[nodiscard]] constexpr bool isEntireBuffer(const BufferDesc& desc)const{ return (!byteOffset) && (byteSize == static_cast<u64>(-1) || byteSize == desc.byteSize); }
    constexpr bool operator==(const BufferRange& other)const{ return byteOffset == other.byteOffset && byteSize == other.byteSize; }

    constexpr BufferRange& setByteOffset(uint64_t value){ byteOffset = value; return *this; }
    constexpr BufferRange& setByteSize(uint64_t value){ byteSize = value; return *this; }
};

static constexpr BufferRange s_entireBuffer = BufferRange(0, static_cast<u64>(-1));

class IBuffer : public IResource{
public:
    [[nodiscard]] virtual const BufferDesc& getDescription()const = 0;
    
    [[nodiscard]] virtual GpuVirtualAddress getGpuVirtualAddress()const = 0;
};
typedef RefCountPtr<IBuffer, BlankDeleter<IBuffer>> BufferHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader


namespace ShaderType{
    enum Mask : u16{
        None            = 0x0000,
        
        Compute         = 0x0020,

        Vertex          = 0x0001,
        Hull            = 0x0002,
        Domain          = 0x0004,
        Geometry        = 0x0008,
        Pixel           = 0x0010,
        Amplification   = 0x0040,
        Mesh            = 0x0080,
        AllGraphics     = 0x00DF,

        RayGeneration   = 0x0100,
        AnyHit          = 0x0200,
        ClosestHit      = 0x0400,
        Miss            = 0x0800,
        Intersection    = 0x1000,
        Callable        = 0x2000,
        AllRayTracing   = 0x3F00,

        All             = 0x3FFF,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value) noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value) noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

namespace FastGeometryShaderFlags{
    enum Mask : u8{
        None                             = 0,

        ForceFastGS                      = 1 << 0,
        UseViewportMask                  = 1 << 1,
        OffsetTargetIndexByViewportIndex = 1 << 2,
        StrictApiOrder                   = 1 << 3,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value) noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value) noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

struct CustomSemantic{
    enum Enum : u8{
        Undefined = 0,
        XRight = 1,
        ViewportMask = 2,
    };

    Enum type;
    AString name;
        
    constexpr CustomSemantic& setType(Enum value){ type = value; return *this; }
    CustomSemantic& setName(const AString& value){ name = value; return *this; }
};

struct ShaderDesc{
    ShaderType::Mask shaderType = ShaderType::None;
#ifdef NWB_GRAPHICS_DEBUGGABLE
    AString debugName;
#endif
    AString entryName = "main";

    i32 hlslExtensionsUAV = -1;

    bool useSpecificShaderExt = false;
    u32 numCustomSemantics = 0;
    CustomSemantic* pCustomSemantics = nullptr;

    FastGeometryShaderFlags::Mask fastGSFlags = FastGeometryShaderFlags::None;
    u32* pCoordinateSwizzling = nullptr;

    constexpr ShaderDesc& setShaderType(ShaderType::Mask value){ shaderType = value; return *this; }
    constexpr ShaderDesc& setHlslExtensionsUAV(i32 value){ hlslExtensionsUAV = value; return *this; }
    constexpr ShaderDesc& setUseSpecificShaderExt(bool value){ useSpecificShaderExt = value; return *this; }
    constexpr ShaderDesc& setCustomSemantics(u32 count, CustomSemantic* data){ numCustomSemantics = count; pCustomSemantics = data; return *this; }
    constexpr ShaderDesc& setFastGSFlags(FastGeometryShaderFlags::Mask value){ fastGSFlags = value; return *this; }
    constexpr ShaderDesc& setCoordinateSwizzling(u32* value){ pCoordinateSwizzling = value; return *this; }
    
    ShaderDesc& setEntryName(const AString& value){ entryName = value; return *this; }
#ifdef NWB_GRAPHICS_DEBUGGABLE
    ShaderDesc& setDebugName(const AString& value){ debugName = value; return *this; }
#endif
};

struct ShaderSpecialization{
    u32 constantID = 0;
    union{
        u32 u = 0;
        i32 i;
        f32 f;
    } value;

    static constexpr ShaderSpecialization U32(u32 constantID, u32 u){
        ShaderSpecialization s;
        s.constantID = constantID;
        s.value.u = u;
        return s;
    }
    static constexpr ShaderSpecialization I32(u32 constantID, i32 i){
        ShaderSpecialization s;
        s.constantID = constantID;
        s.value.i = i;
        return s;
    }
    static constexpr ShaderSpecialization F32(u32 constantID, f32 f){
        ShaderSpecialization s;
        s.constantID = constantID;
        s.value.f = f;
        return s;
    }
};

class IShader : public IResource{
public:
    [[nodiscard]] virtual const ShaderDesc& getDescription()const = 0;

    virtual void getBytecode(const void** ppBytecode, usize* pSize)const = 0;
};
typedef RefCountPtr<IShader, BlankDeleter<IShader>> ShaderHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Library


class IShaderLibrary : public IResource{
public:
    virtual void getBytecode(const void** ppBytecode, usize* pSize)const = 0;
    virtual ShaderHandle getShader(const char* entryName, ShaderType::Mask shaderType) = 0;
};
typedef RefCountPtr<IShaderLibrary, BlankDeleter<IShaderLibrary>> ShaderLibraryHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Blend State


namespace BlendFactor{
    enum Enum : u8{
        Zero = 1,
        One = 2,
        SrcColor = 3,
        InvSrcColor = 4,
        SrcAlpha = 5,
        InvSrcAlpha = 6,
        DstAlpha  = 7,
        InvDstAlpha = 8,
        DstColor = 9,
        InvDstColor = 10,
        SrcAlphaSaturate = 11,
        ConstantColor = 14,
        InvConstantColor = 15,
        Src1Color = 16,
        InvSrc1Color = 17,
        Src1Alpha = 18,
        InvSrc1Alpha = 19,

        // Vulkan names
        OneMinusSrcColor = InvSrcColor,
        OneMinusSrcAlpha = InvSrcAlpha,
        OneMinusDstAlpha = InvDstAlpha,
        OneMinusDstColor = InvDstColor,
        OneMinusConstantColor = InvConstantColor,
        OneMinusSrc1Color = InvSrc1Color,
        OneMinusSrc1Alpha = InvSrc1Alpha,
    };
};

namespace BlendOp{
    enum Enum : u8{
        Add = 1,
       Subtract = 2,
       ReverseSubtract = 3,
       Min = 4,
       Max = 5,
    };
};

namespace ColorMask{
    enum Mask : u8{
        None = 0,

        Red = 1 << 0,
        Green = 1 << 1,
        Blue = 1 << 2,
        Alpha = 1 << 3,

        All = 0xF,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs) noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value) noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value) noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs) noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

