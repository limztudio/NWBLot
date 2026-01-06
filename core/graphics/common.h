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
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
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
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
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
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
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

    constexpr BufferRange& setByteOffset(u64 value){ byteOffset = value; return *this; }
    constexpr BufferRange& setByteSize(u64 value){ byteSize = value; return *this; }
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
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

namespace FastGeometryShaderFlags{
    enum Mask : u8{
        None                             = 0,

        ForceFastGS                      = 1 << 0,
        UseViewportMask                  = 1 << 1,
        OffsetTargetIndexByViewportIndex = 1 << 2,
        StrictApiOrder                   = 1 << 3,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
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
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

    struct BlendState{
        struct RenderTarget{
            BlendFactor::Enum srcBlend = BlendFactor::One;
            BlendFactor::Enum destBlend = BlendFactor::Zero;
            BlendOp::Enum blendOp = BlendOp::Add;
            BlendFactor::Enum srcBlendAlpha = BlendFactor::One;
            BlendFactor::Enum destBlendAlpha = BlendFactor::Zero;
            BlendOp::Enum blendOpAlpha = BlendOp::Add;
            ColorMask::Mask colorWriteMask = ColorMask::All;
            bool blendEnable = false;

            constexpr RenderTarget& setBlendEnable(bool enable){ blendEnable = enable; return *this; }
            constexpr RenderTarget& enableBlend(){ blendEnable = true; return *this; }
            constexpr RenderTarget& disableBlend(){ blendEnable = false; return *this; }
            constexpr RenderTarget& setSrcBlend(BlendFactor::Enum value){ srcBlend = value; return *this; }
            constexpr RenderTarget& setDestBlend(BlendFactor::Enum value){ destBlend = value; return *this; }
            constexpr RenderTarget& setBlendOp(BlendOp::Enum value){ blendOp = value; return *this; }
            constexpr RenderTarget& setSrcBlendAlpha(BlendFactor::Enum value){ srcBlendAlpha = value; return *this; }
            constexpr RenderTarget& setDestBlendAlpha(BlendFactor::Enum value){ destBlendAlpha = value; return *this; }
            constexpr RenderTarget& setBlendOpAlpha(BlendOp::Enum value){ blendOpAlpha = value; return *this; }
            constexpr RenderTarget& setColorWriteMask(ColorMask::Mask value){ colorWriteMask = value; return *this; }

            [[nodiscard]] bool usesConstantColor()const;
        };

        RenderTarget targets[s_maxRenderTargets];
        bool alphaToCoverageEnable = false;

        constexpr BlendState& setRenderTarget(u32 index, const RenderTarget& target){ targets[index] = target; return *this; }
        constexpr BlendState& setAlphaToCoverageEnable(bool enable){ alphaToCoverageEnable = enable; return *this; }
        constexpr BlendState& enableAlphaToCoverage(){ alphaToCoverageEnable = true; return *this; }
        constexpr BlendState& disableAlphaToCoverage(){ alphaToCoverageEnable = false; return *this; }

        [[nodiscard]] bool usesConstantColor(u32 numTargets)const;
    };
    constexpr bool operator==(const BlendState::RenderTarget& lhs, const BlendState::RenderTarget& rhs)noexcept{
        return 
            lhs.blendEnable == rhs.blendEnable
            && lhs.srcBlend == rhs.srcBlend
            && lhs.destBlend == rhs.destBlend
            && lhs.blendOp == rhs.blendOp
            && lhs.srcBlendAlpha == rhs.srcBlendAlpha
            && lhs.destBlendAlpha == rhs.destBlendAlpha
            && lhs.blendOpAlpha == rhs.blendOpAlpha
            && lhs.colorWriteMask == rhs.colorWriteMask
        ;
    }
    constexpr bool operator!=(const BlendState::RenderTarget& lhs, const BlendState::RenderTarget& rhs)noexcept{ return !(lhs == rhs); }
    constexpr bool operator==(const BlendState& lhs, const BlendState& rhs)noexcept{
        if(lhs.alphaToCoverageEnable != rhs.alphaToCoverageEnable)
            return false;

        for(u32 i = 0; i < s_maxRenderTargets; ++i){
            if(lhs.targets[i] != rhs.targets[i])
                return false;
        }

        return true;
    }
    constexpr bool operator!=(const BlendState& lhs, const BlendState& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raster State


namespace RasterFillMode{
    enum Enum : u8{
        Solid,
        Wireframe,
        
        // Vulkan names
        Fill = Solid,
        Line = Wireframe,
    };
};

namespace RasterCullMode{
    enum Enum : u8{
        Back,
        Front,
        None,
    };
};

struct RasterState{
    RasterFillMode::Enum fillMode = RasterFillMode::Solid;
    RasterCullMode::Enum cullMode = RasterCullMode::Back;
    bool frontCounterClockwise = false;
    bool depthClipEnable = false;
    bool scissorEnable = false;
    bool multisampleEnable = false;
    bool antialiasedLineEnable = false;
    i32 depthBias = 0;
    f32 depthBiasClamp = 0.f;
    f32 slopeScaledDepthBias = 0.f;

    // Extended rasterizer state supported by Maxwell
    u8 forcedSampleCount = 0;
    bool programmableSamplePositionsEnable = false;
    bool conservativeRasterEnable = false;
    bool quadFillEnable = false;
    char samplePositionsX[16]{};
    char samplePositionsY[16]{};
    
    constexpr RasterState& setFillMode(RasterFillMode::Enum value){ fillMode = value; return *this; }
    constexpr RasterState& setFillSolid(){ fillMode = RasterFillMode::Solid; return *this; }
    constexpr RasterState& setFillWireframe(){ fillMode = RasterFillMode::Wireframe; return *this; }
    constexpr RasterState& setCullMode(RasterCullMode::Enum value){ cullMode = value; return *this; }
    constexpr RasterState& setCullBack(){ cullMode = RasterCullMode::Back; return *this; }
    constexpr RasterState& setCullFront(){ cullMode = RasterCullMode::Front; return *this; }
    constexpr RasterState& setCullNone(){ cullMode = RasterCullMode::None; return *this; }
    constexpr RasterState& setFrontCounterClockwise(bool value){ frontCounterClockwise = value; return *this; }
    constexpr RasterState& setDepthClipEnable(bool value){ depthClipEnable = value; return *this; }
    constexpr RasterState& enableDepthClip(){ depthClipEnable = true; return *this; }
    constexpr RasterState& disableDepthClip(){ depthClipEnable = false; return *this; }
    constexpr RasterState& setScissorEnable(bool value){ scissorEnable = value; return *this; }
    constexpr RasterState& enableScissor(){ scissorEnable = true; return *this; }
    constexpr RasterState& disableScissor(){ scissorEnable = false; return *this; }
    constexpr RasterState& setMultisampleEnable(bool value){ multisampleEnable = value; return *this; }
    constexpr RasterState& enableMultisample(){ multisampleEnable = true; return *this; }
    constexpr RasterState& disableMultisample(){ multisampleEnable = false; return *this; }
    constexpr RasterState& setAntialiasedLineEnable(bool value){ antialiasedLineEnable = value; return *this; }
    constexpr RasterState& enableAntialiasedLine(){ antialiasedLineEnable = true; return *this; }
    constexpr RasterState& disableAntialiasedLine(){ antialiasedLineEnable = false; return *this; }
    constexpr RasterState& setDepthBias(i32 value){ depthBias = value; return *this; }
    constexpr RasterState& setDepthBiasClamp(f32 value){ depthBiasClamp = value; return *this; }
    constexpr RasterState& setSlopeScaleDepthBias(f32 value){ slopeScaledDepthBias = value; return *this; }
    constexpr RasterState& setForcedSampleCount(u8 value){ forcedSampleCount = value; return *this; }
    constexpr RasterState& setProgrammableSamplePositionsEnable(bool value){ programmableSamplePositionsEnable = value; return *this; }
    constexpr RasterState& enableProgrammableSamplePositions(){ programmableSamplePositionsEnable = true; return *this; }
    constexpr RasterState& disableProgrammableSamplePositions(){ programmableSamplePositionsEnable = false; return *this; }
    constexpr RasterState& setConservativeRasterEnable(bool value){ conservativeRasterEnable = value; return *this; }
    constexpr RasterState& enableConservativeRaster(){ conservativeRasterEnable = true; return *this; }
    constexpr RasterState& disableConservativeRaster(){ conservativeRasterEnable = false; return *this; }
    constexpr RasterState& setQuadFillEnable(bool value){ quadFillEnable = value; return *this; }
    constexpr RasterState& enableQuadFill(){ quadFillEnable = true; return *this; }
    constexpr RasterState& disableQuadFill(){ quadFillEnable = false; return *this; }
    constexpr RasterState& setSamplePositions(const char* x, const char* y, usize count){ for(usize i = 0; i < count; ++i){ samplePositionsX[i] = x[i]; samplePositionsY[i] = y[i]; } return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Depth Stencil State


namespace StencilOp{
    enum Enum : u8{
        Keep = 1,
        Zero = 2,
        Replace = 3,
        IncrementAndClamp = 4,
        DecrementAndClamp = 5,
        Invert = 6,
        IncrementAndWrap = 7,
        DecrementAndWrap = 8,
    };
};

namespace ComparisonFunc{
    enum Enum : u8{
        Never = 1,
        Less = 2,
        Equal = 3,
        LessOrEqual = 4,
        Greater = 5,
        NotEqual = 6,
        GreaterOrEqual = 7,
        Always = 8,
    };
};

struct DepthStencilState{
    struct StencilOpDesc{
        StencilOp::Enum failOp = StencilOp::Keep;
        StencilOp::Enum depthFailOp = StencilOp::Keep;
        StencilOp::Enum passOp = StencilOp::Keep;
        ComparisonFunc::Enum stencilFunc = ComparisonFunc::Always;

        constexpr StencilOpDesc& setFailOp(StencilOp::Enum value){ failOp = value; return *this; }
        constexpr StencilOpDesc& setDepthFailOp(StencilOp::Enum value){ depthFailOp = value; return *this; }
        constexpr StencilOpDesc& setPassOp(StencilOp::Enum value){ passOp = value; return *this; }
        constexpr StencilOpDesc& setStencilFunc(ComparisonFunc::Enum value){ stencilFunc = value; return *this; }
    };

    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    ComparisonFunc::Enum depthFunc = ComparisonFunc::Less;
    bool stencilEnable = false;
    u8 stencilReadMask = 0xff;
    u8 stencilWriteMask = 0xff;
    u8 stencilRefValue = 0;
    bool dynamicStencilRef = false;
    StencilOpDesc frontFaceStencil;
    StencilOpDesc backFaceStencil;

    constexpr DepthStencilState& setDepthTestEnable(bool value){ depthTestEnable = value; return *this; }
    constexpr DepthStencilState& enableDepthTest(){ depthTestEnable = true; return *this; }
    constexpr DepthStencilState& disableDepthTest(){ depthTestEnable = false; return *this; }
    constexpr DepthStencilState& setDepthWriteEnable(bool value){ depthWriteEnable = value; return *this; }
    constexpr DepthStencilState& enableDepthWrite(){ depthWriteEnable = true; return *this; }
    constexpr DepthStencilState& disableDepthWrite(){ depthWriteEnable = false; return *this; }
    constexpr DepthStencilState& setDepthFunc(ComparisonFunc::Enum value){ depthFunc = value; return *this; }
    constexpr DepthStencilState& setStencilEnable(bool value){ stencilEnable = value; return *this; }
    constexpr DepthStencilState& enableStencil(){ stencilEnable = true; return *this; }
    constexpr DepthStencilState& disableStencil(){ stencilEnable = false; return *this; }
    constexpr DepthStencilState& setStencilReadMask(u8 value){ stencilReadMask = value; return *this; }
    constexpr DepthStencilState& setStencilWriteMask(u8 value){ stencilWriteMask = value; return *this; }
    constexpr DepthStencilState& setStencilRefValue(u8 value){ stencilRefValue = value; return *this; }
    constexpr DepthStencilState& setFrontFaceStencil(const StencilOpDesc& value){ frontFaceStencil = value; return *this; }
    constexpr DepthStencilState& setBackFaceStencil(const StencilOpDesc& value){ backFaceStencil = value; return *this; }
    constexpr DepthStencilState& setDynamicStencilRef(bool value){ dynamicStencilRef = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Viewport State


struct ViewportState{
    //These are in pixels
    // note: you can only set each of these either in the PSO or per draw call in DrawArguments
    // it is not legal to have the same state set in both the PSO and DrawArguments
    // leaving these vectors empty means no state is set
    FixedVector<Viewport, s_maxViewports> viewports;
    FixedVector<Rect, s_maxViewports> scissorRects;

    ViewportState& addViewport(const Viewport& v){ viewports.push_back(v); return *this; }
    ViewportState& addScissorRect(const Rect& r){ scissorRects.push_back(r); return *this; }
    ViewportState& addViewportAndScissorRect(const Viewport& v){ return addViewport(v).addScissorRect(Rect(v)); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler


namespace SamplerAddressMode{
    enum Enum : u8{
        Clamp,
        Wrap,
        Border,
        Mirror,
        MirrorOnce,

        // Vulkan names
        ClampToEdge = Clamp,
        Repeat = Wrap,
        ClampToBorder = Border,
        MirroredRepeat = Mirror,
        MirrorClampToEdge = MirrorOnce,
    };
};

namespace SamplerReductionType{
    enum Enum : u8{
        Standard,
        Comparison,
        Minimum,
        Maximum,
    };
};

struct SamplerDesc{
    Color borderColor = 1.f;
    f32 maxAnisotropy = 1.f;
    f32 mipBias = 0.f;

    bool minFilter = true;
    bool magFilter = true;
    bool mipFilter = true;
    SamplerAddressMode::Enum addressU = SamplerAddressMode::Clamp;
    SamplerAddressMode::Enum addressV = SamplerAddressMode::Clamp;
    SamplerAddressMode::Enum addressW = SamplerAddressMode::Clamp;
    SamplerReductionType::Enum reductionType = SamplerReductionType::Standard;

    SamplerDesc& setBorderColor(const Color& color){ borderColor = color; return *this; }
    SamplerDesc& setMaxAnisotropy(f32 value){ maxAnisotropy = value; return *this; }
    SamplerDesc& setMipBias(f32 value){ mipBias = value; return *this; }
    SamplerDesc& setMinFilter(bool enable){ minFilter = enable; return *this; }
    SamplerDesc& setMagFilter(bool enable){ magFilter = enable; return *this; }
    SamplerDesc& setMipFilter(bool enable){ mipFilter = enable; return *this; }
    SamplerDesc& setAllFilters(bool enable){ minFilter = magFilter = mipFilter = enable; return *this; }
    SamplerDesc& setAddressU(SamplerAddressMode::Enum mode){ addressU = mode; return *this; }
    SamplerDesc& setAddressV(SamplerAddressMode::Enum mode){ addressV = mode; return *this; }
    SamplerDesc& setAddressW(SamplerAddressMode::Enum mode){ addressW = mode; return *this; }
    SamplerDesc& setAllAddressModes(SamplerAddressMode::Enum mode){ addressU = addressV = addressW = mode; return *this; }
    SamplerDesc& setReductionType(SamplerReductionType::Enum type){ reductionType = type; return *this; }
};

class ISampler : public IResource{
public:
    [[nodiscard]] virtual const SamplerDesc& getDescription()const = 0;
};
typedef RefCountPtr<ISampler, BlankDeleter<ISampler>> SamplerHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame buffer


struct FramebufferAttachment{
    ITexture* texture = nullptr;
    TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, 1);
    Format::Enum format = Format::UNKNOWN;
    bool isReadOnly = false;
        
    constexpr FramebufferAttachment& setTexture(ITexture* t){ texture = t; return *this; }
    constexpr FramebufferAttachment& setSubresources(TextureSubresourceSet value){ subresources = value; return *this; }
    constexpr FramebufferAttachment& setArraySlice(ArraySlice index){ subresources.baseArraySlice = index; subresources.numArraySlices = 1; return *this; }
    constexpr FramebufferAttachment& setArraySliceRange(ArraySlice index, ArraySlice count){ subresources.baseArraySlice = index; subresources.numArraySlices = count; return *this; }
    constexpr FramebufferAttachment& setMipLevel(MipLevel level){ subresources.baseMipLevel = level; subresources.numMipLevels = 1; return *this; }
    constexpr FramebufferAttachment& setFormat(Format::Enum f){ format = f; return *this; }
    constexpr FramebufferAttachment& setReadOnly(bool val){ isReadOnly = val; return *this; }

    [[nodiscard]] bool valid()const{ return texture != nullptr; }
};

struct FramebufferDesc{
    FixedVector<FramebufferAttachment, s_maxRenderTargets> colorAttachments;
    FramebufferAttachment depthAttachment;
    FramebufferAttachment shadingRateAttachment;

    FramebufferDesc& addColorAttachment(const FramebufferAttachment& a){ colorAttachments.push_back(a); return *this; }
    FramebufferDesc& addColorAttachment(ITexture* texture){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture)); return *this; }
    FramebufferDesc& addColorAttachment(ITexture* texture, TextureSubresourceSet subresources){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture).setSubresources(subresources)); return *this; }
    FramebufferDesc& setDepthAttachment(const FramebufferAttachment& d){ depthAttachment = d; return *this; }
    FramebufferDesc& setDepthAttachment(ITexture* texture){ depthAttachment = FramebufferAttachment().setTexture(texture); return *this; }
    FramebufferDesc& setDepthAttachment(ITexture* texture, TextureSubresourceSet subresources){ depthAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
    FramebufferDesc& setShadingRateAttachment(const FramebufferAttachment& d){ shadingRateAttachment = d; return *this; }
    FramebufferDesc& setShadingRateAttachment(ITexture* texture){ shadingRateAttachment = FramebufferAttachment().setTexture(texture); return *this; }
    FramebufferDesc& setShadingRateAttachment(ITexture* texture, TextureSubresourceSet subresources){ shadingRateAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
};

struct FramebufferInfo{
    FixedVector<Format::Enum, s_maxRenderTargets> colorFormats;
    Format::Enum depthFormat = Format::UNKNOWN;
    u32 sampleCount = 1;
    u32 sampleQuality = 0;

    FramebufferInfo() = default;
    FramebufferInfo(const FramebufferDesc& desc);

    FramebufferInfo& addColorFormat(Format::Enum format){ colorFormats.push_back(format); return *this; }
    FramebufferInfo& setDepthFormat(Format::Enum format){ depthFormat = format; return *this; }
    FramebufferInfo& setSampleCount(u32 count){ sampleCount = count; return *this; }
    FramebufferInfo& setSampleQuality(u32 quality){ sampleQuality = quality; return *this; }
};
inline bool operator==(const FramebufferInfo& lhs, const FramebufferInfo& rhs){
    if(lhs.sampleQuality != rhs.sampleQuality)
        return false;
    if(lhs.sampleCount != rhs.sampleCount)
        return false;
    if(lhs.depthFormat != rhs.depthFormat)
        return false;
    if(lhs.colorFormats.size() != rhs.colorFormats.size())
        return false;
    for(usize i = 0; i < lhs.colorFormats.size(); ++i){
        if(lhs.colorFormats[i] != rhs.colorFormats[i])
            return false;
    }
    return true;
}
inline bool operator!=(const FramebufferInfo& lhs, const FramebufferInfo& rhs){ return !(lhs == rhs); }

struct FramebufferInfoEx : FramebufferInfo{
    u32 width = 0;
    u32 height = 0;
    u32 arraySize = 1;

    FramebufferInfoEx() = default;
    FramebufferInfoEx(const FramebufferDesc& desc);

    FramebufferInfoEx& setWidth(u32 value){ width = value; return *this; }
    FramebufferInfoEx& setHeight(u32 value){ height = value; return *this; }
    FramebufferInfoEx& setArraySize(u32 value){ arraySize = value; return *this; }

    [[nodiscard]] Viewport getViewport(f32 minZ = 0.f, f32 maxZ = 1.f)const{ return Viewport(0, static_cast<f32>(width), 0, static_cast<f32>(height), minZ, maxZ); }
};

class IFramebuffer : public IResource{
public:
    [[nodiscard]] virtual const FramebufferDesc& getDescription()const = 0;
    [[nodiscard]] virtual const FramebufferInfoEx& getFramebufferInfo()const = 0;
};
typedef RefCountPtr<IFramebuffer, BlankDeleter<IFramebuffer>> FramebufferHandle;

namespace OpacityMicromapFormat{
    enum Enum : u8{
        OC1_2_State = 1,
        OC1_4_State = 2,
    };
};

namespace OpacityMicromapBuildFlags{
    enum Mask : u8{
        None = 0,
        
        FastTrace = 1 << 0,
        FastBuild = 1 << 1,
        AllowCompaction = 1 << 2,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

struct OpacityMicromapUsageCount{
    // Number of OMMs with the specified subdivision level and format.
    u32 count;
    // Micro triangle count is 4^N, where N is the subdivision level.
    u32 subdivisionLevel;
    // OMM input sub format.
    OpacityMicromapFormat::Enum format;
};

struct OpacityMicromapDesc{
#ifdef NWB_GRAPHICS_DEBUGGABLE
    AString debugName;
#endif
    bool trackLiveness = true;

    // OMM flags. Applies to all OMMs in array.
    OpacityMicromapBuildFlags::Mask flags;
    // OMM counts for each subdivision level and format combination in the inputs.
    Vector<OpacityMicromapUsageCount> counts;

    // Base pointer for raw OMM input data.
    // Individual OMMs must be 1B aligned, though natural alignment is recommended.
    // It's also recommended to try to organize OMMs together that are expected to be used spatially close together.
    IBuffer* inputBuffer = nullptr;
    u64 inputBufferOffset = 0;

    // One NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC entry per OMM.
    IBuffer* perOmmDescs = nullptr;
    u64 perOmmDescsOffset = 0;
    
    OpacityMicromapDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
    OpacityMicromapDesc& setFlags(OpacityMicromapBuildFlags::Mask value){ flags = value; return *this; }
    OpacityMicromapDesc& setCounts(const Vector<OpacityMicromapUsageCount>& value){ counts = value; return *this; }
    OpacityMicromapDesc& setInputBuffer(IBuffer* value){ inputBuffer = value; return *this; }
    OpacityMicromapDesc& setInputBufferOffset(u64 value){ inputBufferOffset = value; return *this; }
    OpacityMicromapDesc& setPerOmmDescs(IBuffer* value){ perOmmDescs = value; return *this; }
    OpacityMicromapDesc& setPerOmmDescsOffset(u64 value){ perOmmDescsOffset = value; return *this; }

#ifdef NWB_GRAPHICS_DEBUGGABLE
    OpacityMicromapDesc& setDebugName(const AString& value){ debugName = value; return *this; }
#endif
};

class IOpacityMicromap : public IResource{
public:
    [[nodiscard]] virtual const OpacityMicromapDesc& getDescription()const = 0;
    [[nodiscard]] virtual bool isCompacted()const = 0;
    [[nodiscard]] virtual u64 getDeviceAddress()const = 0;
};
typedef RefCountPtr<IOpacityMicromap, BlankDeleter<IOpacityMicromap>> OpacityMicromapHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AccelStruct


class IAccelStruct;

typedef f32 AffineTransform[12];

constexpr AffineTransform s_identityTransform = {
    1.f, 0.f, 0.f, 0.f,
    0.f, 1.f, 0.f, 0.f,
    0.f, 0.f, 1.f, 0.f
};

namespace GeometryFlags{
    enum Mask : u8{
        None = 0,

        Opaque = 1 << 0,
        NoDuplicateAnyHitInvocation = 1 << 1,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

namespace GeometryType{
    enum Enum : u8{
        Triangles = 0,
        AABBs = 1,
        Spheres = 2,
        Lss = 3,
    };
};

struct GeometryAABB{
    f32 minX;
    f32 minY;
    f32 minZ;
    f32 maxX;
    f32 maxY;
    f32 maxZ;
};

struct GeometryTriangles{
    IBuffer* indexBuffer = nullptr;   // make sure the first 2 fields in all Geometry 
    IBuffer* vertexBuffer = nullptr;  // structs are IBuffer* for easier debugging
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexFormat = Format::UNKNOWN;
    u64 indexOffset = 0;
    u64 vertexOffset = 0;
    u32 indexCount = 0;
    u32 vertexCount = 0;
    u32 vertexStride = 0;

    IOpacityMicromap* opacityMicromap = nullptr;
    IBuffer* ommIndexBuffer = nullptr;
    u64 ommIndexBufferOffset = 0;
    Format::Enum ommIndexFormat = Format::UNKNOWN;
    const OpacityMicromapUsageCount* pOmmUsageCounts = nullptr;
    u32 numOmmUsageCounts = 0;

    GeometryTriangles& setIndexBuffer(IBuffer* value){ indexBuffer = value; return *this; }
    GeometryTriangles& setVertexBuffer(IBuffer* value){ vertexBuffer = value; return *this; }
    GeometryTriangles& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    GeometryTriangles& setVertexFormat(Format::Enum value){ vertexFormat = value; return *this; }
    GeometryTriangles& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    GeometryTriangles& setVertexOffset(u64 value){ vertexOffset = value; return *this; }
    GeometryTriangles& setIndexCount(u32 value){ indexCount = value; return *this; }
    GeometryTriangles& setVertexCount(u32 value){ vertexCount = value; return *this; }
    GeometryTriangles& setVertexStride(u32 value){ vertexStride = value; return *this; }
    GeometryTriangles& setOpacityMicromap(IOpacityMicromap* value){ opacityMicromap = value; return *this; }
    GeometryTriangles& setOmmIndexBuffer(IBuffer* value){ ommIndexBuffer = value; return *this; }
    GeometryTriangles& setOmmIndexBufferOffset(u64 value){ ommIndexBufferOffset = value; return *this; }
    GeometryTriangles& setOmmIndexFormat(Format::Enum value){ ommIndexFormat = value; return *this; }
    GeometryTriangles& setPOmmUsageCounts(const OpacityMicromapUsageCount* value){ pOmmUsageCounts = value; return *this; }
    GeometryTriangles& setNumOmmUsageCounts(u32 value){ numOmmUsageCounts = value; return *this; }
};

struct GeometryAABBs{
    IBuffer* buffer = nullptr;
    IBuffer* unused = nullptr;
    u64 offset = 0;
    u32 count = 0;
    u32 stride = 0;

    GeometryAABBs& setBuffer(IBuffer* value){ buffer = value; return *this; }
    GeometryAABBs& setOffset(u64 value){ offset = value; return *this; }
    GeometryAABBs& setCount(u32 value){ count = value; return *this; }
    GeometryAABBs& setStride(u32 value){ stride = value; return *this; }
};

struct GeometrySpheres{
    IBuffer* indexBuffer = nullptr;
    IBuffer* vertexBuffer = nullptr;
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexPositionFormat = Format::UNKNOWN;
    Format::Enum vertexRadiusFormat = Format::UNKNOWN;
    u64 indexOffset = 0;
    u64 vertexPositionOffset = 0;
    u64 vertexRadiusOffset = 0;
    u32 indexCount = 0;
    u32 vertexCount = 0;
    u32 indexStride = 0;
    u32 vertexPositionStride = 0;
    u32 vertexRadiusStride = 0;

    GeometrySpheres& setIndexBuffer(IBuffer* value){ indexBuffer = value; return *this; }
    GeometrySpheres& setVertexBuffer(IBuffer* value){ vertexBuffer = value; return *this; }
    GeometrySpheres& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    GeometrySpheres& setVertexPositionFormat(Format::Enum value){ vertexPositionFormat = value; return *this; }
    GeometrySpheres& setVertexRadiusFormat(Format::Enum value){ vertexRadiusFormat = value; return *this; }
    GeometrySpheres& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    GeometrySpheres& setVertexPositionOffset(u64 value){ vertexPositionOffset = value; return *this; }
    GeometrySpheres& setVertexRadiusOffset(u64 value){ vertexRadiusOffset = value; return *this; }
    GeometrySpheres& setIndexCount(u32 value){ indexCount = value; return *this; }
    GeometrySpheres& setVertexCount(u32 value){ vertexCount = value; return *this; }
    GeometrySpheres& setIndexStride(u32 value){ indexStride = value; return *this; }
    GeometrySpheres& setVertexPositionStride(u32 value){ vertexPositionStride = value; return *this; }
    GeometrySpheres& setVertexRadiusStride(u32 value){ vertexRadiusStride = value; return *this; }
};

namespace GeometryLssPrimitiveFormat{
    enum Enum : u8{
        List = 0,
        SuccessiveImplicit = 1,
    };
};

namespace GeometryLssEndcapMode{
    enum Enum : u8{
        None = 0,
        Chained = 1,
    };
};

struct GeometryLss{
    IBuffer* indexBuffer = nullptr;
    IBuffer* vertexBuffer = nullptr;
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexPositionFormat = Format::UNKNOWN;
    Format::Enum vertexRadiusFormat = Format::UNKNOWN;
    u64 indexOffset = 0;
    u64 vertexPositionOffset = 0;
    u64 vertexRadiusOffset = 0;
    u32 indexCount = 0;
    u32 primitiveCount = 0;
    u32 vertexCount = 0;
    u32 indexStride = 0;
    u32 vertexPositionStride = 0;
    u32 vertexRadiusStride = 0;
    GeometryLssPrimitiveFormat::Enum primitiveFormat = GeometryLssPrimitiveFormat::List;
    GeometryLssEndcapMode::Enum endcapMode = GeometryLssEndcapMode::None;

    GeometryLss& setIndexBuffer(IBuffer* value){ indexBuffer = value; return *this; }
    GeometryLss& setVertexBuffer(IBuffer* value){ vertexBuffer = value; return *this; }
    GeometryLss& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    GeometryLss& setVertexPositionFormat(Format::Enum value){ vertexPositionFormat = value; return *this; }
    GeometryLss& setVertexRadiusFormat(Format::Enum value){ vertexRadiusFormat = value; return *this; }
    GeometryLss& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    GeometryLss& setVertexPositionOffset(u64 value){ vertexPositionOffset = value; return *this; }
    GeometryLss& setVertexRadiusOffset(u64 value){ vertexRadiusOffset = value; return *this; }
    GeometryLss& setIndexCount(u32 value){ indexCount = value; return *this; }
    GeometryLss& setPrimitiveCount(u32 value){ primitiveCount = value; return *this; }
    GeometryLss& setVertexCount(u32 value){ vertexCount = value; return *this; }
    GeometryLss& setIndexStride(u32 value){ indexStride = value; return *this; }
    GeometryLss& setVertexPositionStride(u32 value){ vertexPositionStride = value; return *this; }
    GeometryLss& setVertexRadiusStride(u32 value){ vertexRadiusStride = value; return *this; }
    GeometryLss& setPrimitiveFormat(GeometryLssPrimitiveFormat::Enum value){ primitiveFormat = value; return *this; }
    GeometryLss& setEndcapMode(GeometryLssEndcapMode::Enum value){ endcapMode = value; return *this; }
};

struct GeometryDesc{
    union GeomTypeUnion{
        GeometryTriangles triangles;
        GeometryAABBs aabbs;
        GeometrySpheres spheres;
        GeometryLss lss;
    } geometryData;

    bool useTransform = false;
    AffineTransform transform{};
    GeometryFlags::Mask flags = GeometryFlags::None;
    GeometryType::Enum geometryType = GeometryType::Triangles;

    GeometryDesc() : geometryData{} {}

    GeometryDesc& setTransform(const AffineTransform& value){ NWB_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); useTransform = true; return *this; }
    GeometryDesc& setFlags(GeometryFlags::Mask value){ flags = value; return *this; }
    GeometryDesc& setTriangles(const GeometryTriangles& value){ geometryData.triangles = value; geometryType = GeometryType::Triangles; return *this; }
    GeometryDesc& setAABBs(const GeometryAABBs& value){ geometryData.aabbs = value; geometryType = GeometryType::AABBs; return *this; }
    GeometryDesc& setSpheres(const GeometrySpheres& value){ geometryData.spheres = value; geometryType = GeometryType::Spheres; return *this; }
    GeometryDesc& setLss(const GeometryLss& value){ geometryData.lss = value; geometryType = GeometryType::Lss; return *this; }
};

namespace InstanceFlags{
    enum Mask : u32{
        None = 0,

        TriangleCullDisable = 1 << 0,
        TriangleFrontCounterclockwise = 1 << 1,
        ForceOpaque = 1 << 2,
        ForceNonOpaque = 1 << 3,
        ForceOMM2State = 1 << 4,
        DisableOMMs = 1 << 5,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

struct InstanceDesc{
    AffineTransform transform;
    u32 instanceID : 24;
    u32 instanceMask : 8;
    u32 instanceContributionToHitGroupIndex : 24;
    InstanceFlags::Mask flags : 8;
    union{
        IAccelStruct* bottomLevelAS; // for buildTopLevelAccelStruct
        u64 blasDeviceAddress;       // for buildTopLevelAccelStructFromBuffer - use IAccelStruct::getDeviceAddress()
    };

    InstanceDesc()
        : instanceID(0)
        , instanceMask(0)
        , instanceContributionToHitGroupIndex(0)
        , flags(InstanceFlags::None)
        , bottomLevelAS(nullptr)
    {
        setTransform(s_identityTransform);
    }

    InstanceDesc& setInstanceID(u32 value){ instanceID = value; return *this; }
    InstanceDesc& setInstanceContributionToHitGroupIndex(u32 value){ instanceContributionToHitGroupIndex = value; return *this; }
    InstanceDesc& setInstanceMask(u32 value){ instanceMask = value; return *this; }
    InstanceDesc& setTransform(const AffineTransform& value){ NWB_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); return *this; }
    InstanceDesc& setFlags(InstanceFlags::Mask value){ flags = value; return *this; }
    InstanceDesc& setBLAS(IAccelStruct* value){ bottomLevelAS = value; return *this; }
};
static_assert(sizeof(InstanceDesc) == 64, "sizeof(InstanceDesc) is supposed to be 64 bytes");
static_assert(sizeof(IndirectInstanceDesc) == sizeof(InstanceDesc));

namespace AccelStructBuildFlags{
    enum Mask : u8{
        None = 0,

        AllowUpdate = 1 << 0,
        AllowCompaction = 1 << 1,
        PreferFastTrace = 1 << 2,
        PreferFastBuild = 1 << 3,
        MinimizeMemory = 0x10,
        PerformUpdate = 0x20,

        // Removes the errors or warnings that NVRHI validation layer issues when a TLAS
        // includes an instance that points at a NULL BLAS or has a zero instance mask.
        // Only affects the validation layer, doesn't translate to Vk/DX12 AS build flags.
        AllowEmptyInstances = 0x80,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

struct AccelStructDesc{
    usize topLevelMaxInstances = 0; // only applies when isTopLevel = true
    Vector<GeometryDesc> bottomLevelGeometries; // only applies when isTopLevel = false
    AccelStructBuildFlags::Mask buildFlags = AccelStructBuildFlags::None;
#ifdef NWB_GRAPHICS_DEBUGGABLE
    AString debugName;
#endif
    bool trackLiveness = true;
    bool isTopLevel = false;
    bool isVirtual = false;

    AccelStructDesc& setTopLevelMaxInstances(usize value){ topLevelMaxInstances = value; isTopLevel = true; return *this; }
    AccelStructDesc& addBottomLevelGeometry(const GeometryDesc& value){ bottomLevelGeometries.push_back(value); isTopLevel = false; return *this; }
    AccelStructDesc& setBuildFlags(AccelStructBuildFlags::Mask value){ buildFlags = value; return *this; }
    AccelStructDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
    AccelStructDesc& setIsTopLevel(bool value){ isTopLevel = value; return *this; }
    AccelStructDesc& setIsVirtual(bool value){ isVirtual = value; return *this; }
    
#ifdef NWB_GRAPHICS_DEBUGGABLE
    AccelStructDesc& setDebugName(const AString& value){ debugName = value; return *this; }
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AccelStruct


class IAccelStruct : public IResource{
public:
    [[nodiscard]] virtual const AccelStructDesc& getDescription()const = 0;
    [[nodiscard]] virtual bool isCompacted()const = 0;
    [[nodiscard]] virtual uint64_t getDeviceAddress()const = 0;
};
typedef RefCountPtr<IAccelStruct, BlankDeleter<IAccelStruct>> AccelStructHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clusters


namespace ClusterOperationType{
    enum Enum : u8{
        Move,                       // Moves CLAS, CLAS Templates, or Cluster BLAS
        ClasBuild,                  // Builds CLAS from clusters of triangles
        ClasBuildTemplates,         // Builds CLAS templates from triangles
        ClasInstantiateTemplates,   // Instantiates CLAS templates
        BlasBuild,                  // Builds Cluster BLAS from CLAS
    };
};

namespace ClusterOperationMoveType{
    enum Enum : u8{
        BottomLevel,                // Moved objects are Clustered BLAS
        ClusterLevel,               // Moved objects are CLAS
        Template,                   // Moved objects are Cluster Templates
    };
};

namespace ClusterOperationMode{
    enum Enum : u8{
        ImplicitDestinations,       // Provide total buffer space, driver places results within, returns VAs and actual sizes
        ExplicitDestinations,       // Provide individual target VAs, driver places them there, returns actual sizes
        GetSizes,                   // Get minimum size per element
    };
};

namespace ClusterOperationFlags{
    enum Mask : u8{
        None = 0,

        FastTrace = 1 << 0,
        FastBuild = 1 << 1,
        NoOverlap = 1 << 2,
        AllowOMM = 1 << 3,
    };
    
    inline Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    inline Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    inline Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    inline bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    inline bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    inline bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

namespace ClusterOperationIndexFormat{
    enum Enum : u8{
        IndexFormat8bit = 1,
        IndexFormat16bit = 2,
        IndexFormat32bit = 4,
    };
};

struct ClusterOperationSizeInfo{
    u64 resultMaxSizeInBytes = 0;
    u64 scratchSizeInBytes = 0;
};

struct ClusterOperationMoveParams{
    ClusterOperationMoveType::Enum type;
    u32 maxBytes = 0;
};

struct ClusterOperationClasBuildParams{
    // See D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC for accepted formats and how they are interpreted
    Format::Enum vertexFormat = Format::RGB32_FLOAT;

    // Index of the last geometry in a single CLAS
    u32 maxGeometryIndex = 0;

    // Maximum number of unique geometries in a single CLAS
    u32 maxUniqueGeometryCount = 1;

    // Maximum number of triangles in a single CLAS
    u32 maxTriangleCount = 0;

    // Maximum number of vertices in a single CLAS
    u32 maxVertexCount = 0;

    // Maximum number of triangles summed over all CLAS (in the current cluster operation)
    u32 maxTotalTriangleCount = 0;

    // Maximum number of vertices summed over all CLAS (in the current cluster operation)
    u32 maxTotalVertexCount = 0;

    // Minimum number of bits to be truncated in vertex positions across all CLAS (in the current cluster operation)
    u32 minPositionTruncateBitCount = 0;
};

struct ClusterOperationBlasBuildParams{
    // Maximum number of CLAS references in a single BLAS
    u32 maxClasPerBlasCount = 0;

    // Maximum number of CLAS references summed over all BLAS (in the current cluster operation)
    u32 maxTotalClasCount = 0;
};

struct ClusterOperationParams{
    // Maximum number of acceleration structures (or templates) to build/instantiate/move
    u32 maxArgCount = 0;

    ClusterOperationType::Enum type;
    ClusterOperationMode::Enum mode;
    ClusterOperationFlags::Mask flags;

    ClusterOperationMoveParams move;
    ClusterOperationClasBuildParams clas;
    ClusterOperationBlasBuildParams blas;
};

struct ClusterOperationDesc{
    ClusterOperationParams params;

    u64 scratchSizeInBytes = 0;                             // Size of scratch resource returned by getClusterOperationSizeInfo() scratchSizeInBytes 

    // Input Resources
    IBuffer* inIndirectArgCountBuffer = nullptr;            // Buffer containing the number of AS to build, instantiate, or move
    u64 inIndirectArgCountOffsetInBytes = 0;                // Offset (in bytes) to where the count is in the inIndirectArgCountBuffer 
    IBuffer* inIndirectArgsBuffer = nullptr;                // Buffer of descriptor array of format IndirectTriangleClasArgs, IndirectTriangleTemplateArgs, IndirectInstantiateTemplateArgs
    u64 inIndirectArgsOffsetInBytes = 0;                    // Offset (in bytes) to where the descriptor array starts inIndirectArgsBuffer

    // In/Out Resources
    IBuffer* inOutAddressesBuffer = nullptr;                // Array of addresseses of CLAS, CLAS Templates, or BLAS
    u64 inOutAddressesOffsetInBytes = 0;                    // Offset (in bytes) to where the addresses array starts in inOutAddressesBuffer

    // Output Resources
    IBuffer* outSizesBuffer = nullptr;                      // Sizes (in bytes) of CLAS, CLAS Templates, or BLAS
    u64 outSizesOffsetInBytes = 0;                          // Offset (in bytes) to where the output sizes array starts in outSizesBuffer
    IBuffer* outAccelerationStructuresBuffer = nullptr;     // Destination buffer for CLAS, CLAS Template, or BLAS data. Size must be calculated with getOperationSizeInfo or with the outSizesBuffer result of OperationMode::GetSizes
    u64 outAccelerationStructuresOffsetInBytes = 0;         // Offset (in bytes) to where the output acceleration structures starts in outAccelerationStructuresBuffer
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Layouts


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

