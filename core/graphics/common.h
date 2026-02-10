// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/common/common.h>

#include "basic.h"
#include "shader.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_core{
    template <typename T>
    void hashCombine(usize& seed, const T& v){
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
};


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
    return 
    (lhs.r == rhs.r)
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
    return 
    (lhs.minX == rhs.minX) && (lhs.maxX == rhs.maxX)
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
    return 
    (lhs.minX == rhs.minX) && (lhs.maxX == rhs.maxX)
    && (lhs.minY == rhs.minY) && (lhs.maxY == rhs.maxY)
    ;
}
inline bool operator!=(const Rect& lhs, const Rect& rhs)noexcept{ return !(lhs == rhs); }


namespace GraphicsAPI{
    enum Enum : u8{
        VULKAN,
    };
};

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
    const Name* name;
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
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
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
    u64 capacity = 0;
    HeapType::Enum type;
    Name debugName;
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
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
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
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
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
    Name name;
    
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
    constexpr TextureDesc& setName(const Name& v)noexcept{ name = v; return *this; }
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
    Name name;
    bool isInstanced = false;
    
    constexpr VertexAttributeDesc& setFormat(Format::Enum value){ format = value; return *this; }
    constexpr VertexAttributeDesc& setArraySize(u32 value){ arraySize = value; return *this; }
    constexpr VertexAttributeDesc& setBufferIndex(u32 value){ bufferIndex = value; return *this; }
    constexpr VertexAttributeDesc& setOffset(u32 value){ offset = value; return *this; }
    constexpr VertexAttributeDesc& setElementStride(u32 value){ elementStride = value; return *this; }
    constexpr VertexAttributeDesc& setName(const Name& value){ name = value; return *this; }
    constexpr VertexAttributeDesc& setIsInstanced(bool value){ isInstanced = value; return *this; }
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
    Name debugName;
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
    constexpr BufferDesc& setDebugName(const Name& value){ debugName = value; return *this; }
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
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

namespace FastGeometryShaderFlags{
    enum Mask : u8{
        None                             = 0,

        ForceFastGS                      = 1 << 0,
        UseViewportMask                  = 1 << 1,
        OffsetTargetIndexByViewportIndex = 1 << 2,
        StrictApiOrder                   = 1 << 3,
    };
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

struct CustomSemantic{
    enum Enum : u8{
        Undefined = 0,
        XRight = 1,
        ViewportMask = 2,
    };

    Enum type;
    Name name;
        
    constexpr CustomSemantic& setType(Enum value){ type = value; return *this; }
    constexpr CustomSemantic& setName(const Name& value){ name = value; return *this; }
};

struct ShaderDesc{
    ShaderType::Mask shaderType = ShaderType::None;
    Name debugName;
    Name entryName = "main";

    i32 hlslExtensionsUAV = -1;

    bool useSpecificShaderExt = false;
    u32 numCustomSemantics = 0;
    CustomSemantic* pCustomSemantics = nullptr;

    FastGeometryShaderFlags::Mask fastGSFlags = FastGeometryShaderFlags::None;
    u32* pCoordinateSwizzling = nullptr;

    constexpr ShaderDesc& setShaderType(ShaderType::Mask value){ shaderType = value; return *this; }
    constexpr ShaderDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    constexpr ShaderDesc& setEntryName(const Name& value){ entryName = value; return *this; }
    constexpr ShaderDesc& setHlslExtensionsUAV(i32 value){ hlslExtensionsUAV = value; return *this; }
    constexpr ShaderDesc& setUseSpecificShaderExt(bool value){ useSpecificShaderExt = value; return *this; }
    constexpr ShaderDesc& setCustomSemantics(u32 count, CustomSemantic* data){ numCustomSemantics = count; pCustomSemantics = data; return *this; }
    constexpr ShaderDesc& setFastGSFlags(FastGeometryShaderFlags::Mask value){ fastGSFlags = value; return *this; }
    constexpr ShaderDesc& setCoordinateSwizzling(u32* value){ pCoordinateSwizzling = value; return *this; }
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
    virtual ShaderHandle getShader(const Name& entryName, ShaderType::Mask shaderType) = 0;
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
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
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
    constexpr RasterState& setSamplePositions(const i8* x, const i8* y, usize count){ for(usize i = 0; i < count; ++i){ samplePositionsX[i] = x[i]; samplePositionsY[i] = y[i]; } return *this; }
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

    constexpr ViewportState& addViewport(const Viewport& v){ viewports.push_back(v); return *this; }
    constexpr ViewportState& addScissorRect(const Rect& r){ scissorRects.push_back(r); return *this; }
    constexpr ViewportState& addViewportAndScissorRect(const Viewport& v){ return addViewport(v).addScissorRect(Rect(v)); }
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

    constexpr SamplerDesc& setBorderColor(const Color& color){ borderColor = color; return *this; }
    constexpr SamplerDesc& setMaxAnisotropy(f32 value){ maxAnisotropy = value; return *this; }
    constexpr SamplerDesc& setMipBias(f32 value){ mipBias = value; return *this; }
    constexpr SamplerDesc& setMinFilter(bool enable){ minFilter = enable; return *this; }
    constexpr SamplerDesc& setMagFilter(bool enable){ magFilter = enable; return *this; }
    constexpr SamplerDesc& setMipFilter(bool enable){ mipFilter = enable; return *this; }
    constexpr SamplerDesc& setAllFilters(bool enable){ minFilter = magFilter = mipFilter = enable; return *this; }
    constexpr SamplerDesc& setAddressU(SamplerAddressMode::Enum mode){ addressU = mode; return *this; }
    constexpr SamplerDesc& setAddressV(SamplerAddressMode::Enum mode){ addressV = mode; return *this; }
    constexpr SamplerDesc& setAddressW(SamplerAddressMode::Enum mode){ addressW = mode; return *this; }
    constexpr SamplerDesc& setAllAddressModes(SamplerAddressMode::Enum mode){ addressU = addressV = addressW = mode; return *this; }
    constexpr SamplerDesc& setReductionType(SamplerReductionType::Enum type){ reductionType = type; return *this; }
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

    [[nodiscard]] constexpr bool valid()const{ return texture != nullptr; }
};

struct FramebufferDesc{
    FixedVector<FramebufferAttachment, s_maxRenderTargets> colorAttachments;
    FramebufferAttachment depthAttachment;
    FramebufferAttachment shadingRateAttachment;

    constexpr FramebufferDesc& addColorAttachment(const FramebufferAttachment& a){ colorAttachments.push_back(a); return *this; }
    constexpr FramebufferDesc& addColorAttachment(ITexture* texture){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture)); return *this; }
    constexpr FramebufferDesc& addColorAttachment(ITexture* texture, TextureSubresourceSet subresources){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture).setSubresources(subresources)); return *this; }
    constexpr FramebufferDesc& setDepthAttachment(const FramebufferAttachment& d){ depthAttachment = d; return *this; }
    constexpr FramebufferDesc& setDepthAttachment(ITexture* texture){ depthAttachment = FramebufferAttachment().setTexture(texture); return *this; }
    constexpr FramebufferDesc& setDepthAttachment(ITexture* texture, TextureSubresourceSet subresources){ depthAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
    constexpr FramebufferDesc& setShadingRateAttachment(const FramebufferAttachment& d){ shadingRateAttachment = d; return *this; }
    constexpr FramebufferDesc& setShadingRateAttachment(ITexture* texture){ shadingRateAttachment = FramebufferAttachment().setTexture(texture); return *this; }
    constexpr FramebufferDesc& setShadingRateAttachment(ITexture* texture, TextureSubresourceSet subresources){ shadingRateAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
};

struct FramebufferInfo{
    FixedVector<Format::Enum, s_maxRenderTargets> colorFormats;
    Format::Enum depthFormat = Format::UNKNOWN;
    u32 sampleCount = 1;
    u32 sampleQuality = 0;

    FramebufferInfo() = default;
    FramebufferInfo(const FramebufferDesc& desc);

    constexpr FramebufferInfo& addColorFormat(Format::Enum format){ colorFormats.push_back(format); return *this; }
    constexpr FramebufferInfo& setDepthFormat(Format::Enum format){ depthFormat = format; return *this; }
    constexpr FramebufferInfo& setSampleCount(u32 count){ sampleCount = count; return *this; }
    constexpr FramebufferInfo& setSampleQuality(u32 quality){ sampleQuality = quality; return *this; }
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

    constexpr FramebufferInfoEx& setWidth(u32 value){ width = value; return *this; }
    constexpr FramebufferInfoEx& setHeight(u32 value){ height = value; return *this; }
    constexpr FramebufferInfoEx& setArraySize(u32 value){ arraySize = value; return *this; }

    [[nodiscard]] constexpr Viewport getViewport(f32 minZ = 0.f, f32 maxZ = 1.f)const{ return Viewport(0, static_cast<f32>(width), 0, static_cast<f32>(height), minZ, maxZ); }
};

class IFramebuffer : public IResource{
public:
    [[nodiscard]] virtual const FramebufferDesc& getDescription()const = 0;
    [[nodiscard]] virtual const FramebufferInfoEx& getFramebufferInfo()const = 0;
};
typedef RefCountPtr<IFramebuffer, BlankDeleter<IFramebuffer>> FramebufferHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing Opacity Micromap


namespace OpacityMicromapFormat{
    enum Enum : u8{
        OC1_2_State = 1,
        OC1_4_State = 2,
    };
};

namespace RayTracingOpacityMicromapBuildFlags{
    enum Mask : u8{
        None = 0,
        
        FastTrace = 1 << 0,
        FastBuild = 1 << 1,
        AllowCompaction = 1 << 2,
    };
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

struct RayTracingOpacityMicromapUsageCount{
    // Number of OMMs with the specified subdivision level and format.
    u32 count;
    // Micro triangle count is 4^N, where N is the subdivision level.
    u32 subdivisionLevel;
    // OMM input sub format.
    OpacityMicromapFormat::Enum format;
};

struct RayTracingOpacityMicromapDesc{
    Name debugName;
    bool trackLiveness = true;

    // OMM flags. Applies to all OMMs in array.
    RayTracingOpacityMicromapBuildFlags::Mask flags;
    // OMM counts for each subdivision level and format combination in the inputs.
    Vector<RayTracingOpacityMicromapUsageCount> counts;

    // Base pointer for raw OMM input data.
    // Individual OMMs must be 1B aligned, though natural alignment is recommended.
    // It's also recommended to try to organize OMMs together that are expected to be used spatially close together.
    IBuffer* inputBuffer = nullptr;
    u64 inputBufferOffset = 0;

    // One NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC entry per OMM.
    IBuffer* perOmmDescs = nullptr;
    u64 perOmmDescsOffset = 0;
    
    constexpr RayTracingOpacityMicromapDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setFlags(RayTracingOpacityMicromapBuildFlags::Mask value){ flags = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setCounts(const Vector<RayTracingOpacityMicromapUsageCount>& value){ counts = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setInputBuffer(IBuffer* value){ inputBuffer = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setInputBufferOffset(u64 value){ inputBufferOffset = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setPerOmmDescs(IBuffer* value){ perOmmDescs = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setPerOmmDescsOffset(u64 value){ perOmmDescsOffset = value; return *this; }
};

class IRayTracingOpacityMicromap : public IResource{
public:
    [[nodiscard]] virtual const RayTracingOpacityMicromapDesc& getDescription()const = 0;
    [[nodiscard]] virtual bool isCompacted()const = 0;
    [[nodiscard]] virtual u64 getDeviceAddress()const = 0;
};
typedef RefCountPtr<IRayTracingOpacityMicromap, BlankDeleter<IRayTracingOpacityMicromap>> RayTracingOpacityMicromapHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing AccelStruct


class IRayTracingAccelStruct;

typedef f32 AffineTransform[12];

constexpr AffineTransform s_identityTransform = {
    1.f, 0.f, 0.f, 0.f,
    0.f, 1.f, 0.f, 0.f,
    0.f, 0.f, 1.f, 0.f
};

namespace RayTracingGeometryFlags{
    enum Mask : u8{
        None = 0,

        Opaque = 1 << 0,
        NoDuplicateAnyHitInvocation = 1 << 1,
    };
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

namespace RayTracingGeometryType{
    enum Enum : u8{
        Triangles = 0,
        AABBs = 1,
        Spheres = 2,
        Lss = 3,
    };
};

struct RayTracingGeometryAABB{
    f32 minX;
    f32 minY;
    f32 minZ;
    f32 maxX;
    f32 maxY;
    f32 maxZ;
};

struct RayTracingGeometryTriangles{
    IBuffer* indexBuffer = nullptr;   // make sure the first 2 fields in all Geometry 
    IBuffer* vertexBuffer = nullptr;  // structs are IBuffer* for easier debugging
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexFormat = Format::UNKNOWN;
    u64 indexOffset = 0;
    u64 vertexOffset = 0;
    u32 indexCount = 0;
    u32 vertexCount = 0;
    u32 vertexStride = 0;

    IRayTracingOpacityMicromap* opacityMicromap = nullptr;
    IBuffer* ommIndexBuffer = nullptr;
    u64 ommIndexBufferOffset = 0;
    Format::Enum ommIndexFormat = Format::UNKNOWN;
    const RayTracingOpacityMicromapUsageCount* pOmmUsageCounts = nullptr;
    u32 numOmmUsageCounts = 0;

    constexpr RayTracingGeometryTriangles& setIndexBuffer(IBuffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexBuffer(IBuffer* value){ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexFormat(Format::Enum value){ vertexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexOffset(u64 value){ vertexOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexStride(u32 value){ vertexStride = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOpacityMicromap(IRayTracingOpacityMicromap* value){ opacityMicromap = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOmmIndexBuffer(IBuffer* value){ ommIndexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOmmIndexBufferOffset(u64 value){ ommIndexBufferOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOmmIndexFormat(Format::Enum value){ ommIndexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setPOmmUsageCounts(const RayTracingOpacityMicromapUsageCount* value){ pOmmUsageCounts = value; return *this; }
    constexpr RayTracingGeometryTriangles& setNumOmmUsageCounts(u32 value){ numOmmUsageCounts = value; return *this; }
};

struct RayTracingGeometryAABBs{
    IBuffer* buffer = nullptr;
    IBuffer* unused = nullptr;
    u64 offset = 0;
    u32 count = 0;
    u32 stride = 0;

    constexpr RayTracingGeometryAABBs& setBuffer(IBuffer* value){ buffer = value; return *this; }
    constexpr RayTracingGeometryAABBs& setOffset(u64 value){ offset = value; return *this; }
    constexpr RayTracingGeometryAABBs& setCount(u32 value){ count = value; return *this; }
    constexpr RayTracingGeometryAABBs& setStride(u32 value){ stride = value; return *this; }
};

struct RayTracingGeometrySpheres{
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

    constexpr RayTracingGeometrySpheres& setIndexBuffer(IBuffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexBuffer(IBuffer* value){ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometrySpheres& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexPositionFormat(Format::Enum value){ vertexPositionFormat = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexRadiusFormat(Format::Enum value){ vertexRadiusFormat = value; return *this; }
    constexpr RayTracingGeometrySpheres& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexPositionOffset(u64 value){ vertexPositionOffset = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexRadiusOffset(u64 value){ vertexRadiusOffset = value; return *this; }
    constexpr RayTracingGeometrySpheres& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr RayTracingGeometrySpheres& setIndexStride(u32 value){ indexStride = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexPositionStride(u32 value){ vertexPositionStride = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexRadiusStride(u32 value){ vertexRadiusStride = value; return *this; }
};

namespace RayTracingGeometryLssPrimitiveFormat{
    enum Enum : u8{
        List = 0,
        SuccessiveImplicit = 1,
    };
};

namespace RayTracingGeometryLssEndcapMode{
    enum Enum : u8{
        None = 0,
        Chained = 1,
    };
};

struct RayTracingGeometryLss{
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
    RayTracingGeometryLssPrimitiveFormat::Enum primitiveFormat = RayTracingGeometryLssPrimitiveFormat::List;
    RayTracingGeometryLssEndcapMode::Enum endcapMode = RayTracingGeometryLssEndcapMode::None;

    constexpr RayTracingGeometryLss& setIndexBuffer(IBuffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexBuffer(IBuffer* value){ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometryLss& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexPositionFormat(Format::Enum value){ vertexPositionFormat = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexRadiusFormat(Format::Enum value){ vertexRadiusFormat = value; return *this; }
    constexpr RayTracingGeometryLss& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexPositionOffset(u64 value){ vertexPositionOffset = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexRadiusOffset(u64 value){ vertexRadiusOffset = value; return *this; }
    constexpr RayTracingGeometryLss& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr RayTracingGeometryLss& setPrimitiveCount(u32 value){ primitiveCount = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr RayTracingGeometryLss& setIndexStride(u32 value){ indexStride = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexPositionStride(u32 value){ vertexPositionStride = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexRadiusStride(u32 value){ vertexRadiusStride = value; return *this; }
    constexpr RayTracingGeometryLss& setPrimitiveFormat(RayTracingGeometryLssPrimitiveFormat::Enum value){ primitiveFormat = value; return *this; }
    constexpr RayTracingGeometryLss& setEndcapMode(RayTracingGeometryLssEndcapMode::Enum value){ endcapMode = value; return *this; }
};

struct RayTracingGeometryDesc{
    union GeomTypeUnion{
        RayTracingGeometryTriangles triangles;
        RayTracingGeometryAABBs aabbs;
        RayTracingGeometrySpheres spheres;
        RayTracingGeometryLss lss;
    } geometryData;

    bool useTransform = false;
    AffineTransform transform{};
    RayTracingGeometryFlags::Mask flags = RayTracingGeometryFlags::None;
    RayTracingGeometryType::Enum geometryType = RayTracingGeometryType::Triangles;

    RayTracingGeometryDesc() : geometryData{} {}

    constexpr RayTracingGeometryDesc& setTransform(const AffineTransform& value){ NWB_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); useTransform = true; return *this; }
    constexpr RayTracingGeometryDesc& setFlags(RayTracingGeometryFlags::Mask value){ flags = value; return *this; }
    constexpr RayTracingGeometryDesc& setTriangles(const RayTracingGeometryTriangles& value){ geometryData.triangles = value; geometryType = RayTracingGeometryType::Triangles; return *this; }
    constexpr RayTracingGeometryDesc& setAABBs(const RayTracingGeometryAABBs& value){ geometryData.aabbs = value; geometryType = RayTracingGeometryType::AABBs; return *this; }
    constexpr RayTracingGeometryDesc& setSpheres(const RayTracingGeometrySpheres& value){ geometryData.spheres = value; geometryType = RayTracingGeometryType::Spheres; return *this; }
    constexpr RayTracingGeometryDesc& setLss(const RayTracingGeometryLss& value){ geometryData.lss = value; geometryType = RayTracingGeometryType::Lss; return *this; }
};

namespace RayTracingInstanceFlags{
    enum Mask : u32{
        None = 0,

        TriangleCullDisable = 1 << 0,
        TriangleFrontCounterclockwise = 1 << 1,
        ForceOpaque = 1 << 2,
        ForceNonOpaque = 1 << 3,
        ForceOMM2State = 1 << 4,
        DisableOMMs = 1 << 5,
    };
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

struct RayTracingInstanceDesc{
    AffineTransform transform;
    u32 instanceID : 24;
    u32 instanceMask : 8;
    u32 instanceContributionToHitGroupIndex : 24;
    RayTracingInstanceFlags::Mask flags : 8;
    union{
        IRayTracingAccelStruct* bottomLevelAS; // for buildTopLevelAccelStruct
        u64 blasDeviceAddress;       // for buildTopLevelAccelStructFromBuffer - use IAccelStruct::getDeviceAddress()
    };

    RayTracingInstanceDesc()
        : instanceID(0)
        , instanceMask(0)
        , instanceContributionToHitGroupIndex(0)
        , flags(RayTracingInstanceFlags::None)
        , bottomLevelAS(nullptr)
    {
        setTransform(s_identityTransform);
    }

    constexpr RayTracingInstanceDesc& setInstanceID(u32 value){ instanceID = value; return *this; }
    constexpr RayTracingInstanceDesc& setInstanceContributionToHitGroupIndex(u32 value){ instanceContributionToHitGroupIndex = value; return *this; }
    constexpr RayTracingInstanceDesc& setInstanceMask(u32 value){ instanceMask = value; return *this; }
    constexpr RayTracingInstanceDesc& setTransform(const AffineTransform& value){ NWB_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); return *this; }
    constexpr RayTracingInstanceDesc& setFlags(RayTracingInstanceFlags::Mask value){ flags = value; return *this; }
    constexpr RayTracingInstanceDesc& setBLAS(IRayTracingAccelStruct* value){ bottomLevelAS = value; return *this; }
};
static_assert(sizeof(RayTracingInstanceDesc) == 64, "sizeof(InstanceDesc) is supposed to be 64 bytes");
static_assert(sizeof(IndirectInstanceDesc) == sizeof(RayTracingInstanceDesc));

namespace RayTracingAccelStructBuildFlags{
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
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

struct RayTracingAccelStructDesc{
    usize topLevelMaxInstances = 0; // only applies when isTopLevel = true
    Vector<RayTracingGeometryDesc> bottomLevelGeometries; // only applies when isTopLevel = false
    RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None;
    Name debugName;
    bool trackLiveness = true;
    bool isTopLevel = false;
    bool isVirtual = false;

    constexpr RayTracingAccelStructDesc& setTopLevelMaxInstances(usize value){ topLevelMaxInstances = value; isTopLevel = true; return *this; }
    constexpr RayTracingAccelStructDesc& addBottomLevelGeometry(const RayTracingGeometryDesc& value){ bottomLevelGeometries.push_back(value); isTopLevel = false; return *this; }
    constexpr RayTracingAccelStructDesc& setBuildFlags(RayTracingAccelStructBuildFlags::Mask value){ buildFlags = value; return *this; }
    constexpr RayTracingAccelStructDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    constexpr RayTracingAccelStructDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
    constexpr RayTracingAccelStructDesc& setIsTopLevel(bool value){ isTopLevel = value; return *this; }
    constexpr RayTracingAccelStructDesc& setIsVirtual(bool value){ isVirtual = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing AccelStruct


class IRayTracingAccelStruct : public IResource{
public:
    [[nodiscard]] virtual const RayTracingAccelStructDesc& getDescription()const = 0;
    [[nodiscard]] virtual bool isCompacted()const = 0;
    [[nodiscard]] virtual u64 getDeviceAddress()const = 0;
};
typedef RefCountPtr<IRayTracingAccelStruct, BlankDeleter<IRayTracingAccelStruct>> RayTracingAccelStructHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing Clusters


namespace RayTracingClusterOperationType{
    enum Enum : u8{
        Move,                       // Moves CLAS, CLAS Templates, or Cluster BLAS
        ClasBuild,                  // Builds CLAS from clusters of triangles
        ClasBuildTemplates,         // Builds CLAS templates from triangles
        ClasInstantiateTemplates,   // Instantiates CLAS templates
        BlasBuild,                  // Builds Cluster BLAS from CLAS
    };
};

namespace RayTracingClusterOperationMoveType{
    enum Enum : u8{
        BottomLevel,                // Moved objects are Clustered BLAS
        ClusterLevel,               // Moved objects are CLAS
        Template,                   // Moved objects are Cluster Templates
    };
};

namespace RayTracingClusterOperationMode{
    enum Enum : u8{
        ImplicitDestinations,       // Provide total buffer space, driver places results within, returns VAs and actual sizes
        ExplicitDestinations,       // Provide individual target VAs, driver places them there, returns actual sizes
        GetSizes,                   // Get minimum size per element
    };
};

namespace RayTracingClusterOperationFlags{
    enum Mask : u8{
        None = 0,

        FastTrace = 1 << 0,
        FastBuild = 1 << 1,
        NoOverlap = 1 << 2,
        AllowOMM = 1 << 3,
    };
    
    constexpr Mask operator|(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); }
    constexpr Mask operator&(Mask lhs, Mask rhs)noexcept{ return static_cast<Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); }
    constexpr Mask operator~(Mask value)noexcept{ return static_cast<Mask>(~static_cast<u32>(value)); }
    constexpr bool operator!(Mask value)noexcept{ return static_cast<u32>(value) == 0; }
    constexpr bool operator==(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); }
    constexpr bool operator!=(Mask lhs, Mask rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }
};

namespace RayTracingClusterOperationIndexFormat{
    enum Enum : u8{
        IndexFormat8bit = 1,
        IndexFormat16bit = 2,
        IndexFormat32bit = 4,
    };
};

struct RayTracingClusterOperationSizeInfo{
    u64 resultMaxSizeInBytes = 0;
    u64 scratchSizeInBytes = 0;
};

struct RayTracingClusterOperationMoveParams{
    RayTracingClusterOperationMoveType::Enum type;
    u32 maxBytes = 0;
};

struct RayTracingClusterOperationClasBuildParams{
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

struct RayTracingClusterOperationBlasBuildParams{
    // Maximum number of CLAS references in a single BLAS
    u32 maxClasPerBlasCount = 0;

    // Maximum number of CLAS references summed over all BLAS (in the current cluster operation)
    u32 maxTotalClasCount = 0;
};

struct RayTracingClusterOperationParams{
    // Maximum number of acceleration structures (or templates) to build/instantiate/move
    u32 maxArgCount = 0;

    RayTracingClusterOperationType::Enum type;
    RayTracingClusterOperationMode::Enum mode;
    RayTracingClusterOperationFlags::Mask flags;

    RayTracingClusterOperationMoveParams move;
    RayTracingClusterOperationClasBuildParams clas;
    RayTracingClusterOperationBlasBuildParams blas;
};

struct RayTracingClusterOperationDesc{
    RayTracingClusterOperationParams params;

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

struct BindingLayoutItem{
    u32 slot;
    
    ResourceType::Enum type : 8;
    u8 unused : 8;
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
        ret.size = 1; \
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
    return 
        lhs.slot == rhs.slot
        && lhs.type == rhs.type
        && lhs.size == rhs.size
    ;
}
inline bool operator!=(const BindingLayoutItem& lhs, const BindingLayoutItem& rhs){ return !(lhs == rhs); }
static_assert(sizeof(BindingLayoutItem) == 8, "sizeof(BindingLayoutItem) is supposed to be 8 bytes");

struct VulkanBindingOffsets{
    u32 shaderResource = 0;
    u32 sampler = 128;
    u32 constantBuffer = 256;
    u32 unorderedAccess = 384;

    constexpr VulkanBindingOffsets& setShaderResourceOffset(u32 value){ shaderResource = value; return *this; }
    constexpr VulkanBindingOffsets& setSamplerOffset(u32 value){ sampler = value; return *this; }
    constexpr VulkanBindingOffsets& setConstantBufferOffset(u32 value){ constantBuffer = value; return *this; }
    constexpr VulkanBindingOffsets& setUnorderedAccessViewOffset(u32 value){ unorderedAccess = value; return *this; }
};

struct BindingLayoutDesc{
    ShaderType::Mask visibility = ShaderType::None;

    // On DX11, the registerSpace is ignored, and all bindings are placed in the same space.
    // On DX12, it controls the register space of the bindings.
    // On Vulkan, DXC maps register spaces to descriptor sets by default, so this can be used to
    // determine the descriptor set index for the binding layout.
    // In order to use this behavior, you must set `registerSpaceIsDescriptorSet` to true. See below.
    u32 registerSpace = 0;

    // This flag controls the behavior for pipelines that use multiple binding layouts.
    // It must be set to the same value for _all_ of the binding layouts in a pipeline.
    // - When it's set to `false`, the `registerSpace` parameter only affects the DX12 implementation,
    //   and the validation layer will report an error when non-zero `registerSpace` is used with other APIs.
    // - When it's set to `true` the parameter also affects the Vulkan implementation, allowing any
    //   layout to occupy any register space or descriptor set, regardless of their order in the pipeline.
    //   However, a consequence of DXC mapping the descriptor set index to register space is that you may
    //   not have more than one `BindingLayout` using the same `registerSpace` value in the same pipeline.
    // - When it's set to different values for the layouts in a pipeline, the validation layer will report
    //   an error.
    bool registerSpaceIsDescriptorSet = false;

    Vector<BindingLayoutItem> bindings;
    VulkanBindingOffsets bindingOffsets;

    constexpr BindingLayoutDesc& setVisibility(ShaderType::Mask value){ visibility = value; return *this; }
    constexpr BindingLayoutDesc& setRegisterSpace(u32 value){ registerSpace = value; return *this; }
    constexpr BindingLayoutDesc& setRegisterSpaceIsDescriptorSet(bool value){ registerSpaceIsDescriptorSet = value; return *this; }
    // Shortcut for .setRegisterSpace(value).setRegisterSpaceIsDescriptorSet(true)
    constexpr BindingLayoutDesc& setRegisterSpaceAndDescriptorSet(u32 value){ registerSpace = value; registerSpaceIsDescriptorSet = true; return *this; }
    constexpr BindingLayoutDesc& addItem(const BindingLayoutItem& value){ bindings.push_back(value); return *this; }
    constexpr BindingLayoutDesc& setBindingOffsets(const VulkanBindingOffsets& value){ bindingOffsets = value; return *this; }
};

// BindlessDescriptorType bridges the DX12 and Vulkan in supporting HLSL ResourceDescriptorHeap and SamplerDescriptorHeap
// For DX12: 
// - MutableSrvUavCbv, MutableCounters will enable D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED for the Root Signature
// - MutableSampler will enable D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED for the Root Signature
// - The BindingLayout will be ignored in terms of setting a descriptor set. DescriptorIndexing should use GetDescriptorIndexInHeap()
// For Vulkan:
// - The type corresponds to the SPIRV bindings which map to ResourceDescriptorHeap and SamplerDescriptorHeap
// - The shader needs to be compiled with the same descriptor set index as is passed into setState
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
// The same table can be bound to multiple register spaces on DX12, in order to 
// access different types of resources stored in the table through different arrays.
// The `registerSpaces` vector specifies which spaces will the table be bound to,
// with the table type (SRV or UAV) derived from the resource type assigned to each space.
struct BindlessLayoutDesc{
    ShaderType::Mask visibility = ShaderType::None;
    u32 firstSlot = 0;
    u32 maxCapacity = 0;
    FixedVector<BindingLayoutItem, s_maxBindlessRegisterSpaces> registerSpaces;

    BindlessLayoutType::Enum layoutType = BindlessLayoutType::Immutable;

    constexpr BindlessLayoutDesc& setVisibility(ShaderType::Mask value){ visibility = value; return *this; }
    constexpr BindlessLayoutDesc& setFirstSlot(u32 value){ firstSlot = value; return *this; }
    constexpr BindlessLayoutDesc& setMaxCapacity(u32 value){ maxCapacity = value; return *this; }
    constexpr BindlessLayoutDesc& addRegisterSpace(const BindingLayoutItem& value){ registerSpaces.push_back(value); return *this; }
    constexpr BindlessLayoutDesc& setLayoutType(BindlessLayoutType::Enum value){ layoutType = value; return *this; }
};

class IBindingLayout : public IResource{
public:
    [[nodiscard]] virtual const BindingLayoutDesc* getDescription()const = 0;    // returns nullptr for bindless layouts
    [[nodiscard]] virtual const BindlessLayoutDesc* getBindlessDesc()const = 0;  // returns nullptr for regular layouts
};
typedef RefCountPtr<IBindingLayout, BlankDeleter<IBindingLayout>> BindingLayoutHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Sets


struct BindingSetItem{
    IResource* resourceHandle;

    u32 slot;

    // Specifies the index in a binding array.
    // Must be less than the 'size' property of the matching BindingLayoutItem.
    // - DX11/12: Effective binding slot index is calculated as (slot + arrayElement), i.e. arrays are flattened
    // - Vulkan: Descriptor arrays are used.
    // This behavior matches the behavior of HLSL resource array declarations when compiled with DXC.
    u32 arrayElement;

    ResourceType::Enum type          : 8;
    TextureDimension::Enum dimension : 8; // valid for Texture_SRV, Texture_UAV
    Format::Enum format              : 8; // valid for Texture_SRV, Texture_UAV, Buffer_SRV, Buffer_UAV
    u8 unused                        : 8;

    u32 unused2;

    union{
        TextureSubresourceSet subresources; // valid for Texture_SRV, Texture_UAV
        BufferRange range; // valid for Buffer_SRV, Buffer_UAV, ConstantBuffer
        u16 rawData[2];
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
    
    static constexpr BindingSetItem None(u32 slot = 0){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::None;
        result.resourceHandle = nullptr;
        result.format = Format::UNKNOWN;
        result.dimension = TextureDimension::Unknown;
        result.rawData[0] = 0;
        result.rawData[1] = 0;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem Texture_SRV(u32 slot, ITexture* texture, Format::Enum format = Format::UNKNOWN, TextureSubresourceSet subresources = s_allSubresources, TextureDimension::Enum dimension = TextureDimension::Unknown){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::Texture_SRV;
        result.resourceHandle = texture;
        result.format = format;
        result.dimension = dimension;
        result.subresources = subresources;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem Texture_UAV(u32 slot, ITexture* texture, Format::Enum format = Format::UNKNOWN, TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, TextureSubresourceSet::AllArraySlices), TextureDimension::Enum dimension = TextureDimension::Unknown){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::Texture_UAV;
        result.resourceHandle = texture;
        result.format = format;
        result.dimension = dimension;
        result.subresources = subresources;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem TypedBuffer_SRV(u32 slot, IBuffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_entireBuffer){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::TypedBuffer_SRV;
        result.resourceHandle = buffer;
        result.format = format;
        result.dimension = TextureDimension::Unknown;
        result.range = range;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem TypedBuffer_UAV(u32 slot, IBuffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_entireBuffer){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::TypedBuffer_UAV;
        result.resourceHandle = buffer;
        result.format = format;
        result.dimension = TextureDimension::Unknown;
        result.range = range;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem ConstantBuffer(u32 slot, IBuffer* buffer, BufferRange range = s_entireBuffer){
        bool isVolatile = buffer && buffer->getDescription().isVolatile;

        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = isVolatile ? ResourceType::VolatileConstantBuffer : ResourceType::ConstantBuffer;
        result.resourceHandle = buffer;
        result.format = Format::UNKNOWN;
        result.dimension = TextureDimension::Unknown;
        result.range = range;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem Sampler(u32 slot, ISampler* sampler){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::Sampler;
        result.resourceHandle = sampler;
        result.format = Format::UNKNOWN;
        result.dimension = TextureDimension::Unknown;
        result.rawData[0] = 0;
        result.rawData[1] = 0;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem RayTracingAccelStruct(u32 slot, IRayTracingAccelStruct* as){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::RayTracingAccelStruct;
        result.resourceHandle = as;
        result.format = Format::UNKNOWN;
        result.dimension = TextureDimension::Unknown;
        result.rawData[0] = 0;
        result.rawData[1] = 0;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem StructuredBuffer_SRV(u32 slot, IBuffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_entireBuffer){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::StructuredBuffer_SRV;
        result.resourceHandle = buffer;
        result.format = format;
        result.dimension = TextureDimension::Unknown;
        result.range = range;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem StructuredBuffer_UAV(u32 slot, IBuffer* buffer, Format::Enum format = Format::UNKNOWN, BufferRange range = s_entireBuffer){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::StructuredBuffer_UAV;
        result.resourceHandle = buffer;
        result.format = format;
        result.dimension = TextureDimension::Unknown;
        result.range = range;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem RawBuffer_SRV(u32 slot, IBuffer* buffer, BufferRange range = s_entireBuffer){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::RawBuffer_SRV;
        result.resourceHandle = buffer;
        result.format = Format::UNKNOWN;
        result.dimension = TextureDimension::Unknown;
        result.range = range;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem RawBuffer_UAV(u32 slot, IBuffer* buffer, BufferRange range = s_entireBuffer){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::RawBuffer_UAV;
        result.resourceHandle = buffer;
        result.format = Format::UNKNOWN;
        result.dimension = TextureDimension::Unknown;
        result.range = range;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem PushConstants(u32 slot, u32 byteSize){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::PushConstants;
        result.resourceHandle = nullptr;
        result.format = Format::UNKNOWN;
        result.dimension = TextureDimension::Unknown;
        result.range.byteOffset = 0;
        result.range.byteSize = byteSize;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
    static constexpr BindingSetItem SamplerFeedbackTexture_UAV(u32 slot, ISamplerFeedbackTexture* texture){
        BindingSetItem result;
        result.slot = slot;
        result.arrayElement = 0;
        result.type = ResourceType::SamplerFeedbackTexture_UAV;
        result.resourceHandle = texture;
        result.format = Format::UNKNOWN;
        result.dimension = TextureDimension::Unknown;
        result.subresources = s_allSubresources;
        result.unused = 0;
        result.unused2 = 0;
        return result;
    }
};
inline bool operator==(const BindingSetItem& lhs, const BindingSetItem& rhs){
    return
        lhs.resourceHandle == rhs.resourceHandle
        && lhs.slot == rhs.slot
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
    Vector<BindingSetItem> bindings;
       
    // Enables automatic liveness tracking of this binding set by nvrhi command lists.
    // By setting trackLiveness to false, you take the responsibility of not releasing it 
    // until all rendering commands using the binding set are finished.
    bool trackLiveness = true;

    constexpr BindingSetDesc& addItem(const BindingSetItem& value){ bindings.push_back(value); return *this; }
    constexpr BindingSetDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
};
inline bool operator==(const BindingSetDesc& lhs, const BindingSetDesc& rhs){
    if(lhs.bindings.size() != rhs.bindings.size())
        return false;
    for(usize i = 0; i < lhs.bindings.size(); ++i){
        if(lhs.bindings[i] != rhs.bindings[i])
            return false;
    }
    return true;
}
inline bool operator!=(const BindingSetDesc& lhs, const BindingSetDesc& rhs){ return !(lhs == rhs); }

class IBindingSet : public IResource{
public:
    [[nodiscard]] virtual const BindingSetDesc* getDescription()const = 0;  // returns nullptr for descriptor tables
    [[nodiscard]] virtual IBindingLayout* getLayout()const = 0;
};
typedef RefCountPtr<IBindingSet, BlankDeleter<IBindingSet>> BindingSetHandle;

// Descriptor tables are bare, without extra mappings, state, or liveness tracking.
// Unlike binding sets, descriptor tables are mutable - moreover, modification is the only way to populate them.
// They can be grown or shrunk, and they are not tied to any binding layout.
// All tracking is off, so applications should use descriptor tables with great care.
// IDescriptorTable is derived from IBindingSet to allow mixing them in the binding arrays.
class IDescriptorTable : public IBindingSet{
public:
    [[nodiscard]] virtual u32 getCapacity()const = 0;
    [[nodiscard]] virtual u32 getFirstDescriptorIndexInHeap()const = 0;
};
typedef RefCountPtr<IDescriptorTable, BlankDeleter<IDescriptorTable>> DescriptorTableHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draw State


namespace PrimitiveType{
    enum Enum : u8{
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        TriangleListWithAdjacency,
        TriangleStripWithAdjacency,
        PatchList,
    };
};

struct SinglePassStereoState{
    u8 renderTargetIndexOffset = 0;
    bool enabled = false;
    bool independentViewportMask = false;
    
    constexpr SinglePassStereoState& setEnabled(bool value){ enabled = value; return *this; }
    constexpr SinglePassStereoState& setIndependentViewportMask(bool value){ independentViewportMask = value; return *this; }
    constexpr SinglePassStereoState& setRenderTargetIndexOffset(u16 value){ renderTargetIndexOffset = value; return *this; }
};
inline bool operator==(const SinglePassStereoState& lhs, const SinglePassStereoState& rhs){
    return 
        lhs.enabled == rhs.enabled
        && lhs.independentViewportMask == rhs.independentViewportMask
        && lhs.renderTargetIndexOffset == rhs.renderTargetIndexOffset
    ;
}
inline bool operator!=(const SinglePassStereoState& lhs, const SinglePassStereoState& rhs){ return !(lhs == rhs); }

struct RenderState{
    BlendState blendState;
    DepthStencilState depthStencilState;
    RasterState rasterState;
    SinglePassStereoState singlePassStereo;

    constexpr RenderState& setBlendState(const BlendState& value){ blendState = value; return *this; }
    constexpr RenderState& setDepthStencilState(const DepthStencilState& value){ depthStencilState = value; return *this; }
    constexpr RenderState& setRasterState(const RasterState& value){ rasterState = value; return *this; }
    constexpr RenderState& setSinglePassStereoState(const SinglePassStereoState& value){ singlePassStereo = value; return *this; }
};

namespace VariableShadingRate{
    enum Enum : u8{
        e1x1,
        e1x2,
        e2x1,
        e2x2,
        e2x4,
        e4x2,
        e4x4,
    };
};

namespace ShadingRateCombiner{
    enum Enum : u8{
        Passthrough,
        Override,
        Min,
        Max,
        ApplyRelative,
    };
};

struct VariableRateShadingState{
    VariableShadingRate::Enum shadingRate = VariableShadingRate::e1x1;
    ShadingRateCombiner::Enum pipelinePrimitiveCombiner = ShadingRateCombiner::Passthrough;
    ShadingRateCombiner::Enum imageCombiner = ShadingRateCombiner::Passthrough;
    bool enabled = false;

    constexpr VariableRateShadingState& setEnabled(bool value){ enabled = value; return *this; }
    constexpr VariableRateShadingState& setShadingRate(VariableShadingRate::Enum value){ shadingRate = value; return *this; }
    constexpr VariableRateShadingState& setPipelinePrimitiveCombiner(ShadingRateCombiner::Enum value){ pipelinePrimitiveCombiner = value; return *this; }
    constexpr VariableRateShadingState& setImageCombiner(ShadingRateCombiner::Enum value){ imageCombiner = value; return *this; }
};
inline bool operator==(const VariableRateShadingState& lhs, const VariableRateShadingState& rhs){
    return 
        lhs.enabled == rhs.enabled
        && lhs.shadingRate == rhs.shadingRate
        && lhs.pipelinePrimitiveCombiner == rhs.pipelinePrimitiveCombiner
        && lhs.imageCombiner == rhs.imageCombiner
    ;
}
inline bool operator!=(const VariableRateShadingState& lhs, const VariableRateShadingState& rhs){ return !(lhs == rhs); }

typedef FixedVector<BindingLayoutHandle, s_maxBindingLayouts> BindingLayoutVector;

struct GraphicsPipelineDesc{
    PrimitiveType::Enum primType = PrimitiveType::TriangleList;
    u32 patchControlPoints = 0;
    InputLayoutHandle inputLayout;

    ShaderHandle VS;
    ShaderHandle HS;
    ShaderHandle DS;
    ShaderHandle GS;
    ShaderHandle PS;

    RenderState renderState;
    VariableRateShadingState shadingRateState;

    BindingLayoutVector bindingLayouts;
        
    constexpr GraphicsPipelineDesc& setPrimType(PrimitiveType::Enum value){ primType = value; return *this; }
    constexpr GraphicsPipelineDesc& setPatchControlPoints(u32 value){ patchControlPoints = value; return *this; }
    GraphicsPipelineDesc& setInputLayout(IInputLayout* value){ inputLayout = value; return *this; }
    GraphicsPipelineDesc& setVertexShader(IShader* value){ VS = value; return *this; }
    GraphicsPipelineDesc& setHullShader(IShader* value){ HS = value; return *this; }
    GraphicsPipelineDesc& setTessellationControlShader(IShader* value){ HS = value; return *this; }
    GraphicsPipelineDesc& setDomainShader(IShader* value){ DS = value; return *this; }
    GraphicsPipelineDesc& setTessellationEvaluationShader(IShader* value){ DS = value; return *this; }
    GraphicsPipelineDesc& setGeometryShader(IShader* value){ GS = value; return *this; }
    GraphicsPipelineDesc& setPixelShader(IShader* value){ PS = value; return *this; }
    GraphicsPipelineDesc& setFragmentShader(IShader* value){ PS = value; return *this; }
    constexpr GraphicsPipelineDesc& setRenderState(const RenderState& value){ renderState = value; return *this; }
    constexpr GraphicsPipelineDesc& setVariableRateShadingState(const VariableRateShadingState& value){ shadingRateState = value; return *this; }
    GraphicsPipelineDesc& addBindingLayout(IBindingLayout* layout){ bindingLayouts.push_back(layout); return *this; }
};

class IGraphicsPipeline : public IResource{
public:
    [[nodiscard]] virtual const GraphicsPipelineDesc& getDescription()const = 0;
    [[nodiscard]] virtual const FramebufferInfo& getFramebufferInfo()const = 0;
};
typedef RefCountPtr<IGraphicsPipeline, BlankDeleter<IGraphicsPipeline>> GraphicsPipelineHandle;

struct ComputePipelineDesc{
    ShaderHandle CS;

    BindingLayoutVector bindingLayouts;

    ComputePipelineDesc& setComputeShader(IShader* value){ CS = value; return *this; }
    ComputePipelineDesc& addBindingLayout(IBindingLayout* layout){ bindingLayouts.push_back(layout); return *this; }
};

class IComputePipeline : public IResource{
public:
    [[nodiscard]] virtual const ComputePipelineDesc& getDescription()const = 0;
};
typedef RefCountPtr<IComputePipeline, BlankDeleter<IComputePipeline>> ComputePipelineHandle;

struct MeshletPipelineDesc{
    PrimitiveType::Enum primType = PrimitiveType::TriangleList;
        
    ShaderHandle AS;
    ShaderHandle MS;
    ShaderHandle PS;

    RenderState renderState;

    BindingLayoutVector bindingLayouts;
        
    constexpr MeshletPipelineDesc& setPrimType(PrimitiveType::Enum value){ primType = value; return *this; }
    MeshletPipelineDesc& setTaskShader(IShader* value){ AS = value; return *this; }
    MeshletPipelineDesc& setAmplificationShader(IShader* value){ AS = value; return *this; }
    MeshletPipelineDesc& setMeshShader(IShader* value){ MS = value; return *this; }
    MeshletPipelineDesc& setPixelShader(IShader* value){ PS = value; return *this; }
    MeshletPipelineDesc& setFragmentShader(IShader* value){ PS = value; return *this; }
    constexpr MeshletPipelineDesc& setRenderState(const RenderState& value){ renderState = value; return *this; }
    MeshletPipelineDesc& addBindingLayout(IBindingLayout* layout){ bindingLayouts.push_back(layout); return *this; }
};

class IMeshletPipeline : public IResource{
public:
    [[nodiscard]] virtual const MeshletPipelineDesc& getDescription()const = 0;
    [[nodiscard]] virtual const FramebufferInfo& getFramebufferInfo()const = 0;
};
typedef RefCountPtr<IMeshletPipeline, BlankDeleter<IMeshletPipeline>> MeshletPipelineHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draw and Dispatch


class IEventQuery : public IResource{};
typedef RefCountPtr<IEventQuery, BlankDeleter<IEventQuery>> EventQueryHandle;

class ITimerQuery : public IResource{};
typedef RefCountPtr<ITimerQuery, BlankDeleter<ITimerQuery>> TimerQueryHandle;

struct VertexBufferBinding{
    IBuffer* buffer = nullptr;
    u64 offset;
    u32 slot;
    
    constexpr VertexBufferBinding& setBuffer(IBuffer* value){ buffer = value; return *this; }
    constexpr VertexBufferBinding& setSlot(u32 value){ slot = value; return *this; }
    constexpr VertexBufferBinding& setOffset(u64 value){ offset = value; return *this; }
};
inline bool operator==(const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{
    return 
        lhs.buffer == rhs.buffer
        && lhs.offset == rhs.offset
        && lhs.slot == rhs.slot
    ;
}
inline bool operator!=(const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{ return !(lhs == rhs); }

struct IndexBufferBinding{
    IBuffer* buffer = nullptr;
    u32 offset;
    Format::Enum format;
    
    constexpr IndexBufferBinding& setBuffer(IBuffer* value){ buffer = value; return *this; }
    constexpr IndexBufferBinding& setFormat(Format::Enum value){ format = value; return *this; }
    constexpr IndexBufferBinding& setOffset(u32 value){ offset = value; return *this; }
};
inline bool operator==(const IndexBufferBinding& lhs, const IndexBufferBinding& rhs)noexcept{
    return 
        lhs.buffer == rhs.buffer
        && lhs.offset == rhs.offset
        && lhs.format == rhs.format
    ;
}
inline bool operator!=(const IndexBufferBinding& lhs, const IndexBufferBinding& rhs)noexcept{ return !(lhs == rhs); }

typedef FixedVector<IBindingSet*, s_maxBindingLayouts> BindingSetVector;

struct GraphicsState{
    IGraphicsPipeline* pipeline = nullptr;
    IFramebuffer* framebuffer = nullptr;
    ViewportState viewport;
    VariableRateShadingState shadingRateState;
    Color blendConstantColor{};
    u8 dynamicStencilRefValue = 0;

    BindingSetVector bindings;

    FixedVector<VertexBufferBinding, s_maxVertexAttributes> vertexBuffers;
    IndexBufferBinding indexBuffer;

    IBuffer* indirectParams = nullptr;

    constexpr GraphicsState& setPipeline(IGraphicsPipeline* value){ pipeline = value; return *this; }
    constexpr GraphicsState& setFramebuffer(IFramebuffer* value){ framebuffer = value; return *this; }
    constexpr GraphicsState& setViewport(const ViewportState& value){ viewport = value; return *this; }
    constexpr GraphicsState& setShadingRateState(const VariableRateShadingState& value){ shadingRateState = value; return *this; }
    constexpr GraphicsState& setBlendColor(const Color& value){ blendConstantColor = value; return *this; }
    constexpr GraphicsState& setDynamicStencilRefValue(u8 value){ dynamicStencilRefValue = value; return *this; }
    GraphicsState& addBindingSet(IBindingSet* value){ bindings.push_back(value); return *this; }
    GraphicsState& addVertexBuffer(const VertexBufferBinding& value){ vertexBuffers.push_back(value); return *this; }
    constexpr GraphicsState& setIndexBuffer(const IndexBufferBinding& value){ indexBuffer = value; return *this; }
    constexpr GraphicsState& setIndirectParams(IBuffer* value){ indirectParams = value; return *this; }
};

struct DrawArguments{
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 startIndexLocation = 0;
    u32 startVertexLocation = 0;
    u32 startInstanceLocation = 0;

    constexpr DrawArguments& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr DrawArguments& setInstanceCount(u32 value){ instanceCount = value; return *this; }
    constexpr DrawArguments& setStartIndexLocation(u32 value){ startIndexLocation = value; return *this; }
    constexpr DrawArguments& setStartVertexLocation(u32 value){ startVertexLocation = value; return *this; }
    constexpr DrawArguments& setStartInstanceLocation(u32 value){ startInstanceLocation = value; return *this; }
};

struct DrawIndirectArguments{
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 startVertexLocation = 0;
    u32 startInstanceLocation = 0;

    constexpr DrawIndirectArguments& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr DrawIndirectArguments& setInstanceCount(u32 value){ instanceCount = value; return *this; }
    constexpr DrawIndirectArguments& setStartVertexLocation(u32 value){ startVertexLocation = value; return *this; }
    constexpr DrawIndirectArguments& setStartInstanceLocation(u32 value){ startInstanceLocation = value; return *this; }
};

struct DrawIndexedIndirectArguments{
    u32 indexCount = 0;
    u32 instanceCount = 1;
    u32 startIndexLocation = 0;
    i32  baseVertexLocation = 0;
    u32 startInstanceLocation = 0;

    constexpr DrawIndexedIndirectArguments& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setInstanceCount(u32 value){ instanceCount = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setStartIndexLocation(u32 value){ startIndexLocation = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setBaseVertexLocation(i32 value){ baseVertexLocation = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setStartInstanceLocation(u32 value){ startInstanceLocation = value; return *this; }
};

struct ComputeState{
    IComputePipeline* pipeline = nullptr;

    BindingSetVector bindings;

    IBuffer* indirectParams = nullptr;

    constexpr ComputeState& setPipeline(IComputePipeline* value){ pipeline = value; return *this; }
    ComputeState& addBindingSet(IBindingSet* value){ bindings.push_back(value); return *this; }
    constexpr ComputeState& setIndirectParams(IBuffer* value){ indirectParams = value; return *this; }
};

struct DispatchIndirectArguments{
    u32 groupsX = 1;
    u32 groupsY = 1;
    u32 groupsZ = 1;

    constexpr DispatchIndirectArguments& setGroupsX(u32 value){ groupsX = value; return *this; }
    constexpr DispatchIndirectArguments& setGroupsY(u32 value){ groupsY = value; return *this; }
    constexpr DispatchIndirectArguments& setGroupsZ(u32 value){ groupsZ = value; return *this; }
    constexpr DispatchIndirectArguments& setGroups2D(u32 x, u32 y){ groupsX = x; groupsY = y; return *this; }
    constexpr DispatchIndirectArguments& setGroups3D(u32 x, u32 y, u32 z){ groupsX = x; groupsY = y; groupsZ = z; return *this; }
};

struct MeshletState{
    IMeshletPipeline* pipeline = nullptr;
    IFramebuffer* framebuffer = nullptr;
    ViewportState viewport;
    Color blendConstantColor{};
    u8 dynamicStencilRefValue = 0;

    BindingSetVector bindings;

    IBuffer* indirectParams = nullptr;

    constexpr MeshletState& setPipeline(IMeshletPipeline* value){ pipeline = value; return *this; }
    constexpr MeshletState& setFramebuffer(IFramebuffer* value){ framebuffer = value; return *this; }
    constexpr MeshletState& setViewport(const ViewportState& value){ viewport = value; return *this; }
    constexpr MeshletState& setBlendColor(const Color& value){ blendConstantColor = value; return *this; }
    MeshletState& addBindingSet(IBindingSet* value){ bindings.push_back(value); return *this; }
    constexpr MeshletState& setIndirectParams(IBuffer* value){ indirectParams = value; return *this; }
    constexpr MeshletState& setDynamicStencilRefValue(u8 value){ dynamicStencilRefValue = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing


struct RayTracingPipelineShaderDesc{
    ShaderHandle shader;
    BindingLayoutHandle bindingLayout;
    Name exportName;
    
    constexpr RayTracingPipelineShaderDesc& setShader(IShader* value){ shader = value; return *this; }
    constexpr RayTracingPipelineShaderDesc& setBindingLayout(IBindingLayout* value){ bindingLayout = value; return *this; }
    constexpr RayTracingPipelineShaderDesc& setExportName(const Name& value){ exportName = value; return *this; }
};

struct RayTracingPipelineHitGroupDesc{
    ShaderHandle closestHitShader;
    ShaderHandle anyHitShader;
    ShaderHandle intersectionShader;
    BindingLayoutHandle bindingLayout;
    Name exportName;
    bool isProceduralPrimitive = false;
    
    constexpr RayTracingPipelineHitGroupDesc& setClosestHitShader(IShader* value){ closestHitShader = value; return *this; }
    constexpr RayTracingPipelineHitGroupDesc& setAnyHitShader(IShader* value){ anyHitShader = value; return *this; }
    constexpr RayTracingPipelineHitGroupDesc& setIntersectionShader(IShader* value){ intersectionShader = value; return *this; }
    constexpr RayTracingPipelineHitGroupDesc& setBindingLayout(IBindingLayout* value){ bindingLayout = value; return *this; }
    constexpr RayTracingPipelineHitGroupDesc& setExportName(const Name& value){ exportName = value; return *this; }
    constexpr RayTracingPipelineHitGroupDesc& setIsProceduralPrimitive(bool value){ isProceduralPrimitive = value; return *this; }
};

struct RayTracingPipelineDesc{
    Vector<RayTracingPipelineShaderDesc> shaders;
    Vector<RayTracingPipelineHitGroupDesc> hitGroups;
    BindingLayoutVector globalBindingLayouts;
    u32 maxPayloadSize = 0;
    u32 maxAttributeSize = sizeof(f32) * 2; // typical case: float2 uv;
    u32 maxRecursionDepth = 1;
    i32 hlslExtensionsUAV = -1;
    bool allowOpacityMicromaps = false;

    constexpr RayTracingPipelineDesc& addShader(const RayTracingPipelineShaderDesc& value){ shaders.push_back(value); return *this; }
    constexpr RayTracingPipelineDesc& addHitGroup(const RayTracingPipelineHitGroupDesc& value){ hitGroups.push_back(value); return *this; }
    RayTracingPipelineDesc& addBindingLayout(IBindingLayout* value){ globalBindingLayouts.push_back(value); return *this; }
    constexpr RayTracingPipelineDesc& setMaxPayloadSize(u32 value){ maxPayloadSize = value; return *this; }
    constexpr RayTracingPipelineDesc& setMaxAttributeSize(u32 value){ maxAttributeSize = value; return *this; }
    constexpr RayTracingPipelineDesc& setMaxRecursionDepth(u32 value){ maxRecursionDepth = value; return *this; }
    constexpr RayTracingPipelineDesc& setHlslExtensionsUAV(i32 value){ hlslExtensionsUAV = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowOpacityMicromaps(bool value){ allowOpacityMicromaps = value; return *this; }
};

class IRayTracingPipeline;

class IRayTracingShaderTable : public IResource{
public:
    virtual void setRayGenerationShader(const Name& exportName, IBindingSet* bindings = nullptr) = 0;
    virtual int addMissShader(const Name& exportName, IBindingSet* bindings = nullptr) = 0;
    virtual int addHitGroup(const Name& exportName, IBindingSet* bindings = nullptr) = 0;
    virtual int addCallableShader(const Name& exportName, IBindingSet* bindings = nullptr) = 0;
    virtual void clearMissShaders() = 0;
    virtual void clearHitShaders() = 0;
    virtual void clearCallableShaders() = 0;
    virtual IRayTracingPipeline* getPipeline() = 0;
};
typedef RefCountPtr<IRayTracingShaderTable, BlankDeleter<IRayTracingShaderTable>> RayTracingShaderTableHandle;

class IRayTracingPipeline : public IResource{
public:
    [[nodiscard]] virtual const RayTracingPipelineDesc& getDescription()const = 0;
    virtual RayTracingShaderTableHandle createShaderTable() = 0;
};
typedef RefCountPtr<IRayTracingPipeline, BlankDeleter<IRayTracingPipeline>> RayTracingPipelineHandle;

struct RayTracingState{
    IRayTracingShaderTable* shaderTable = nullptr;

    BindingSetVector bindings;

    constexpr RayTracingState& setShaderTable(IRayTracingShaderTable* value){ shaderTable = value; return *this; }
    RayTracingState& addBindingSet(IBindingSet* value){ bindings.push_back(value); return *this; }
};

struct RayTracingDispatchRaysArguments{
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;

    constexpr RayTracingDispatchRaysArguments& setWidth(u32 value){ width = value; return *this; }
    constexpr RayTracingDispatchRaysArguments& setHeight(u32 value){ height = value; return *this; }
    constexpr RayTracingDispatchRaysArguments& setDepth(u32 value){ depth = value; return *this; }
    constexpr RayTracingDispatchRaysArguments& setDimensions(u32 w, u32 h = 1, u32 d = 1){ width = w; height = h; depth = d; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Linear Algebra / Cooperative Vectors


namespace CooperativeVectorDataType{
    enum Enum : u8{
        UInt8,
        SInt8,
        UInt8Packed,
        SInt8Packed,
        UInt16,
        SInt16,
        UInt32,
        SInt32,
        UInt64,
        SInt64,
        FloatE4M3,
        FloatE5M2,
        Float16,
        BFloat16,
        Float32,
        Float64,
    };
};

namespace CooperativeVectorMatrixLayout{
    enum Enum : u8{
        RowMajor,
        ColumnMajor,
        InferencingOptimal,
        TrainingOptimal,
    };
};

// Describes a combination of input and output data types for matrix multiplication with Cooperative Vectors.
// - DX12: Maps from D3D12_COOPERATIVE_VECTOR_PROPERTIES_MUL.
// - Vulkan: Maps from VkCooperativeVectorPropertiesNV.
struct CooperativeVectorMatMulFormatCombo{
    CooperativeVectorDataType::Enum inputType;
    CooperativeVectorDataType::Enum inputInterpretation;
    CooperativeVectorDataType::Enum matrixInterpretation;
    CooperativeVectorDataType::Enum biasInterpretation;
    CooperativeVectorDataType::Enum outputType;
    bool transposeSupported;
};

struct CooperativeVectorDeviceFeatures{
    // Format combinations supported by the device for matrix multiplication with Cooperative Vectors.
    Vector<CooperativeVectorMatMulFormatCombo> matMulFormats;

    // - DX12: True if FLOAT16 is supported as accumulation format for both outer product accumulation
    //         and vector accumulation.
    // - Vulkan: True if cooperativeVectorTrainingFloat16Accumulation is supported.
    bool trainingFloat16 = false;

    // - DX12: True if FLOAT32 is supported as accumulation format for both outer product accumulation
    //         and vector accumulation.
    // - Vulkan: True if cooperativeVectorTrainingFloat32Accumulation is supported.
    bool trainingFloat32 = false;
};

struct CooperativeVectorMatrixLayoutDesc{
    // Buffer where the matrix is stored.
    IBuffer* buffer = nullptr;

    // Offset in bytes from the start of the buffer where the matrix starts.
    u64 offset = 0;

    // Data type of the matrix elements.
    CooperativeVectorDataType::Enum type = CooperativeVectorDataType::UInt8;

    // Layout of the matrix in memory.
    CooperativeVectorMatrixLayout::Enum layout = CooperativeVectorMatrixLayout::RowMajor;

    // Size in bytes of the matrix.
    usize size = 0;

    // Stride in bytes between rows or coumns, depending on the layout.
    // For RowMajor and ColumnMajor layouts, stride may be zero, in which case it is computed automatically.
    // For InferencingOptimal and TrainingOptimal layouts, stride does not matter and should be zero.
    usize stride = 0;
};

// Describes a single matrix layout conversion operation.
// Used by ICommandList::convertCoopVecMatrices(...)
struct CooperativeVectorConvertMatrixLayoutDesc{
    CooperativeVectorMatrixLayoutDesc src;
    CooperativeVectorMatrixLayoutDesc dst;

    u32 numRows = 0;
    u32 numColumns = 0;
};

// Returns the size in bytes of a given data type.
extern constexpr usize GetCooperativeVectorDataTypeSize(CooperativeVectorDataType::Enum type);

// Returns the stride for a given matrix if it's stored in a RowMajor or ColumnMajor layout.
// For other layouts, returns 0.
extern constexpr usize GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, u32 rows, u32 columns);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Miscellaneous


namespace Feature{
    enum Enum : u8{
        ComputeQueue,
        ConservativeRasterization,
        ConstantBufferRanges,
        CopyQueue,
        DeferredCommandLists,
        FastGeometryShader,
        HeapDirectlyIndexed,
        HlslExtensionUAV,
        LinearSweptSpheres,
        Meshlets,
        RayQuery,
        RayTracingAccelStruct,
        RayTracingClusters,
        RayTracingOpacityMicromap,
        RayTracingPipeline,
        SamplerFeedback,
        ShaderExecutionReordering,
        ShaderSpecializations,
        SinglePassStereo,
        Spheres,
        VariableRateShading,
        VirtualResources,
        WaveLaneCountMinMax,
        CooperativeVectorInferencing,
        CooperativeVectorTraining,
    };
};

namespace CommandQueue{
    enum Enum : u8{
        Graphics = 0,
        Compute,
        Copy,
        
        kCount
    };
};

struct VariableRateShadingFeatureInfo{
    u32 shadingRateImageTileSize;
};

struct WaveLaneCountMinMaxFeatureInfo{
    u32 minWaveLaneCount;
    u32 maxWaveLaneCount;
};

class IDevice;

struct CommandListParameters{
    // A command list with enableImmediateExecution = true maps to the immediate context on DX11.
    // Two immediate command lists cannot be open at the same time, which is checked by the validation layer.
    bool enableImmediateExecution = true;

    // Minimum size of memory chunks created to upload data to the device on DX12.
    usize uploadChunkSize = 64 * 1024;

    // Minimum size of memory chunks created for AS build scratch buffers.
    usize scratchChunkSize = 64 * 1024;

    // Maximum total memory size used for all AS build scratch buffers owned by this command list.
    usize scratchMaxMemory = 1024 * 1024 * 1024;

    // Type of the queue that this command list is to be executed on.
    // COPY and COMPUTE queues have limited subsets of methods available.
    CommandQueue::Enum queueType = CommandQueue::Graphics;

    CommandListParameters& setEnableImmediateExecution(bool value){ enableImmediateExecution = value; return *this; }
    CommandListParameters& setUploadChunkSize(usize value){ uploadChunkSize = value; return *this; }
    CommandListParameters& setScratchChunkSize(usize value){ scratchChunkSize = value; return *this; }
    CommandListParameters& setScratchMaxMemory(usize value){ scratchMaxMemory = value; return *this; }
    CommandListParameters& setQueueType(CommandQueue::Enum value){ queueType = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ICommandList


// Represents a sequence of GPU operations.
// - DX11: All command list objects map to the single immediate context. Only one command list may be in the open
//   state at any given time, and all command lists must have CommandListParameters::enableImmediateExecution = true.
// - DX12: One command list object may contain multiple instances of ID3D12GraphicsCommandList* and
//   ID3D12CommandAllocator objects, reusing older ones as they finish executing on the GPU. A command list object
//   also contains the upload manager (for suballocating memory from the upload heap on operations such as
//   writeBuffer) and the DXR scratch manager (for suballocating memory for acceleration structure builds).
//   The upload and scratch managers' memory is reused when possible, but it is only freed when the command list
//   object is destroyed. Thus, it might be a good idea to use a dedicated NVRHI command list for uploading large
//   amounts of data and to destroy it when uploading is finished.
// - Vulkan: The command list objects don't own the VkCommandBuffer-s but request available ones from the queue
//   instead. The upload and scratch buffers behave the same way they do on DX12.
class ICommandList : public IResource{
public:
    // Prepares the command list for recording a new sequence of commands.
    // All other methods of ICommandList must only be used when the command list is open.
    // - DX11: The immediate command list may always stay in the open state, although that prohibits other
    //   command lists from opening.
    // - DX12, Vulkan: Creates or reuses the command list or buffer object and the command allocator (DX12),
    //   starts tracking the resources being referenced in the command list.
    virtual void open() = 0;

    // Finalizes the command list and prepares it for execution.
    // Use IDevice::executeCommandLists(...) to execute it.
    // Re-opening the command list without execution is allowed but not well-tested.
    virtual void close() = 0;

    // Resets the NVRHI state cache associated with the command list, clears some of the underlying API state.
    // This method is mostly useful when switching from recording commands to the open command list using 
    // non-NVRHI code - see getNativeObject(...) - to recording further commands using NVRHI.
    virtual void clearState() = 0;

    // Clears some or all subresources of the given color texture using the provided color.
    // - DX11/12: The clear operation uses either an RTV or a UAV, depending on the texture usage flags
    //   (isRenderTarget and isUAV).
    // - Vulkan: vkCmdClearColorImage is always used with the Float32 color fields set.
    // At least one of the 'isRenderTarget' and 'isUAV' flags must be set, and the format of the texture
    // must be of a color type.
    virtual void clearTextureFloat(ITexture* t, TextureSubresourceSet subresources, const Color& clearColor) = 0;

    // Clears some or all subresources of the given depth-stencil texture using the provided depth and/or stencil
    // values. The texture must have the isRenderTarget flag set, and its format must be of a depth-stencil type.
    virtual void clearDepthStencilTexture(ITexture* t, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil) = 0;

    // Clears some or all subresources of the given color texture using the provided integer value.
    // - DX11/12: If the texture has the isUAV flag set, the clear is performed using ClearUnorderedAccessViewUint.
    //   Otherwise, the clear value is converted to a float, and the texture is cleared as an RTV with all 4
    //   color components using the same value.
    // - Vulkan: vkCmdClearColorImage is always used with the UInt32 and Int32 color fields set.
    virtual void clearTextureUInt(ITexture* t, TextureSubresourceSet subresources, u32 clearColor) = 0;

    // Copies a single 2D or 3D region of texture data from texture 'src' into texture 'dst'.
    // The region's dimensions must be compatible between the two textures, meaning that for simple color textures
    // they must be equal, and for reinterpret copies between compressed and uncompressed textures, they must differ
    // by a factor equal to the block size. The function does not resize textures, only 1:1 pixel copies are
    // supported.
    virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) = 0;

    // Copies a single 2D or 3D region of texture data from regular texture 'src' into staging texture 'dst'.
    virtual void copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) = 0;

    // Copies a single 2D or 3D region of texture data from staging texture 'src' into regular texture 'dst'.
    virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice) = 0;

    // Uploads the contents of an entire 2D or 3D mip level of a single array slice of the texture from CPU memory.
    // The data in CPU memory must be in the same pixel format as the texture. Pixels in every row must be tightly
    // packed, rows are packed with a stride of 'rowPitch' which must not be 0 unless the texture has a height of 1,
    // and depth slices are packed with a stride of 'depthPitch' which also must not be 0 if the texture is 3D.
    // - DX11: Maps directly to UpdateSubresource.
    // - DX12, Vulkan: A region of the automatic upload buffer is suballocated, data is copied there, and then
    //   copied on the GPU into the destination texture using CopyTextureRegion (DX12) or vkCmdCopyBufferToImage (VK).
    //   The upload buffer region can only be reused when this command list instance finishes executing on the GPU.
    // For more advanced uploading operations, such as updating only a region in the texture, use staging texture
    // objects and copyTexture(...).
    virtual void writeTexture(ITexture* dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch = 0) = 0;

    // Performs a resolve operation to combine samples from some or all subresources of a multisample texture 'src'
    // into matching subresources of a non-multisample texture 'dest'. Both textures' formats must be of color type.
    // - DX11/12: Maps to a sequence of ResolveSubresource calls, one per subresource.
    // - Vulkan: Maps to a single vkCmdResolveImage call.
    virtual void resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources) = 0;

    // Uploads 'dataSize' bytes of data from CPU memory into the GPU buffer 'b' at offset 'destOffsetBytes'.
    // - DX11: If the buffer's 'cpuAccess' mode is set to Write, maps the buffer and uploads the data that way.
    //   Otherwise, uses UpdateSubresource.
    // - DX12: If the buffer's 'isVolatile' flag is set, a region of the automatic upload buffer is suballocated,
    //   and the data is copied there. Subsequent uses of the buffer will directly refer to that location in the
    //   upload buffer, until the next call to writeBuffer(...) or until the command list is closed. A volatile
    //   buffer can not be used until writeBuffer(...) is called on it every time after the command list is opened.
    //   If the 'isVolatile' flag is not set, a region of the automatic upload buffer is suballocated, the data
    //   is copied there, and then copied into the real GPU buffer object using CopyBufferRegion.
    // - Vulkan: Similar behavior to DX12, except that each volatile buffer actually has its own Vulkan resource.
    //   The size of such resource is determined by the 'maxVersions' field of the BufferDesc. When writeBuffer(...)
    //   is called on a volatile buffer, a region of that buffer object (a single version) is suballocated, data
    //   is copied there, and subsequent uses of the buffer in the same command list will refer to that version.
    //   For non-volatile buffers, writes of 64 kB or smaller use vkCmdUpdateBuffer. Larger writes suballocate
    //   a portion of the automatic upload buffer and copy the data to the real GPU buffer through that and 
    //   vkCmdCopyBuffer.
    virtual void writeBuffer(IBuffer* b, const void* data, usize dataSize, u64 destOffsetBytes = 0) = 0;

    // Fills the entire buffer using the provided uint32 value.
    // - DX11/12: Maps to ClearUnorderedAccessViewUint.
    // - Vulkan: Maps to vkCmdFillBuffer.
    virtual void clearBufferUInt(IBuffer* b, u32 clearValue) = 0;

    // Copies 'dataSizeBytes' of data from buffer 'src' at offset 'srcOffsetBytes' into buffer 'dest' at offset
    // 'destOffsetBytes'. The source and destination regions must be within the sizes of the respective buffers.
    // - DX11: Maps to CopySubresourceRegion.
    // - DX12: Maps to CopyBufferRegion.
    // - Vulkan: Maps to vkCmdCopyBuffer.
    virtual void copyBuffer(IBuffer* dest, u64 destOffsetBytes, IBuffer* src, u64 srcOffsetBytes, u64 dataSizeBytes) = 0;

    // Clears the entire sampler feedback texture.
    // - DX12: Maps to ClearUnorderedAccessViewUint.
    // - DX11, Vulkan: Unsupported.
    virtual void clearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture) = 0;

    // Decodes the sampler feedback texture into an application-usable format, storing data into the provided buffer.
    // The 'format' parameter should be Format::R8_UINT.
    // - DX12: Maps to ResolveSubresourceRegion.
    //   See https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html
    // - DX11, Vulkan: Unsupported.
    virtual void decodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, Format::Enum format) = 0;

    // Transitions the sampler feedback texture into the requested state, placing a barrier if necessary.
    // The barrier is appended into the pending barrier list and not issued immediately,
    // instead waiting for any rendering, compute or transfer operation.
    // Use commitBarriers() to issue the barriers explicitly.
    // Like the other sampler feedback functions, only supported on DX12.
    virtual void setSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, ResourceStates::Mask stateBits) = 0;

    // Writes the provided data into the push constants block for the currently set pipeline.
    // A graphics, compute, ray tracing or meshlet state must be set using the corresponding call
    // (setGraphicsState etc.) before using setPushConstants. Changing the state invalidates push constants.
    // - DX11: Push constants for all pipelines and command lists use a single buffer associated with the
    //   NVRHI context. This function maps to UpdateSubresource on that buffer.
    // - DX12: Push constants map to root constants in the PSO/root signature. This function maps to 
    //   SetGraphicsRoot32BitConstants for graphics or meshlet pipelines, and SetComputeRoot32BitConstants for
    //   compute or ray tracing pipelines.
    // - Vulkan: Push constants are just Vulkan push constants. This function maps to vkCmdPushConstants.
    // Note that NVRHI only supports one push constants binding in all layouts used in a pipeline.
    virtual void setPushConstants(const void* data, usize byteSize) = 0;

    // Sets the specified graphics state on the command list.
    // The state includes the pipeline (or individual shaders on DX11) and all resources bound to it,
    // from input buffers to render targets. See the members of GraphicsState for more information.
    // State is cached by NVRHI, so if some parts of it are not modified by the setGraphicsState(...) call,
    // the corresponding changes won't be made on the underlying graphics API. When combining command list
    // operations made through NVRHI and through direct access to the command list, state caching may lead to
    // incomplete or incorrect state being set on the underlying API because of cache mismatch with the actual
    // state. To avoid these issues, call clearState() when switching from direct command list access to NVRHI.
    virtual void setGraphicsState(const GraphicsState& state) = 0;

    // Draws non-indexed primitivies using the current graphics state.
    // setGraphicsState(...) must be called between opening the command list or using other types of pipelines
    // and calling draw(...) or any of its siblings. If the pipeline uses push constants, those must be set
    // using setPushConstants(...) between setGraphicsState(...) and draw(...). If the pipeline uses volatile
    // constant buffers, their contents must be written using writeBuffer(...) between open(...) and draw(...),
    // which may be before or after setGraphicsState(...).
    // - DX11/12: Maps to DrawInstanced.
    // - Vulkan: Maps to vkCmdDraw.
    virtual void draw(const DrawArguments& args) = 0;

    // Draws indexed primitivies using the current graphics state.
    // See the comment to draw(...) for state information.
    // - DX11/12: Maps to DrawIndexedInstanced.
    // - Vulkan: Maps to vkCmdDrawIndexed.
    virtual void drawIndexed(const DrawArguments& args) = 0;

    // Draws one or multiple sets of non-indexed primitives using the parameters provided in the indirect buffer
    // specified in the prior call to setGraphicsState(...). The memory layout in the buffer is the same for all
    // graphics APIs and is described by the DrawIndirectArguments structure. If drawCount is more than 1,
    // multiple sets of primitives are drawn, and the parameter structures for them are tightly packed in the
    // indirect parameter buffer one after another.
    // See the comment to draw(...) for state information.
    // - DX11: Maps to multiple calls to DrawInstancedIndirect.
    // - DX12: Maps to ExecuteIndirect with a predefined signature.
    // - Vulkan: Maps to vkCmdDrawIndirect.
    virtual void drawIndirect(u32 offsetBytes, u32 drawCount = 1) = 0;
    
    // Draws one or multiple sets of indexed primitives using the parameters provided in the indirect buffer
    // specified in the prior call to setGraphicsState(...). The memory layout in the buffer is the same for all
    // graphics APIs and is described by the DrawIndexedIndirectArguments structure. If drawCount is more than 1,
    // multiple sets of primitives are drawn, and the parameter structures for them are tightly packed in the
    // indirect parameter buffer one after another.
    // See the comment to draw(...) for state information.
    // - DX11: Maps to multiple calls to DrawIndexedInstancedIndirect.
    // - DX12: Maps to ExecuteIndirect with a predefined signature.
    // - Vulkan: Maps to vkCmdDrawIndexedIndirect.
    virtual void drawIndexedIndirect(u32 offsetBytes, u32 drawCount = 1) = 0;
    
    // Sets the specified compute state on the command list.
    // The state includes the pipeline (or individual shaders on DX11) and all resources bound to it.
    // See the members of ComputeState for more information.
    // See the comment to setGraphicsState(...) for information on state caching.
    virtual void setComputeState(const ComputeState& state) = 0;

    // Launches a compute kernel using the current compute state.
    // See the comment to draw(...) for information on state setting, push constants, and volatile constant buffers,
    // replacing graphics with compute.
    // - DX11/12: Maps to Dispatch.
    // - Vulkan: Maps to vkCmdDispatch.
    virtual void dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1) = 0;

    // Launches a compute kernel using the parameters provided in the indirect buffer specified in the prior
    // call to setComputeState(...). The memory layout in the buffer is the same for all graphics APIs and is
    // described by the DispatchIndirectArguments structure.
    // See the comment to dispatch(...) for state information.
    // - DX11: Maps to DispatchIndirect.
    // - DX12: Maps to ExecuteIndirect with a predefined signature.
    // - Vulkan: Maps to vkCmdDispatchIndirect.
    virtual void dispatchIndirect(u32 offsetBytes) = 0;

    // Sets the specified meshlet rendering state on the command list.
    // The state includes the pipeline and all resources bound to it.
    // Not supported on DX11.
    // Meshlet support on DX12 and Vulkan can be queried using IDevice::queryFeatureSupport(Feature::Meshlets).
    // See the members of MeshletState for more information.
    // See the comment to setGraphicsState(...) for information on state caching.
    virtual void setMeshletState(const MeshletState& state) = 0;

    // Draws meshlet primitives using the current meshlet state.
    // See the comment to draw(...) for information on state setting, push constants, and volatile constant buffers,
    // replacing graphics with meshlets.
    // - DX11: Not supported.
    // - DX12: Maps to DispatchMesh.
    // - Vulkan: Maps to vkCmdDispatchMesh.
    virtual void dispatchMesh(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1) = 0;

    // Sets the specified ray tracing state on the command list.
    // The state includes the shader table, which references the pipeline, and all bound resources.
    // Not supported on DX11.
    // See the members of RayTracingState for more information.
    // See the comment to setGraphicsState(...) for information on state caching.
    virtual void setRayTracingState(const RayTracingState& state) = 0;

    // Launches a grid of ray generation shader threads using the current ray tracing state.
    // The ray generation shader to use is specified by the shader table, which currently supports only one
    // ray generation shader. There may be multiple shaders of all other ray tracing types in the shader table.
    // See the comment to draw(...) for information on state setting, push constants, and volatile constant buffers,
    // replacing graphics with ray tracing.
    // - DX11: Not supported.
    // - DX12: Maps to DispatchRays.
    // - Vulkan: Maps to vkCmdTraceRaysKHR.
    virtual void dispatchRays(const RayTracingDispatchRaysArguments& args) = 0;

    // Launches an opacity micromap (OMM) build kernel.
    // A temporary memory region for the build is suballocated using the scratch buffer manager attached to the
    // command list. The size of this memory region is determined automatically inside this function.
    // - DX11: Not supported.
    // - DX12: Maps to NvAPI_D3D12_BuildRaytracingOpacityMicromapArray and requires NVAPI.
    // - Vulkan: Maps to vkCmdBuildMicromapsEXT.
    virtual void buildOpacityMicromap(IRayTracingOpacityMicromap* omm, const RayTracingOpacityMicromapDesc& desc) = 0;
    
    // Builds or updates a bottom-level ray tracing acceleration structure (BLAS).
    // A temporary memory region for the build is suballocated using the scratch buffer manager attached to the
    // command list. The size of this memory region is determined automatically inside this function.
    // The type of operation to perform is specified by the buildFlags parameter.
    // When building a new BLAS, the amount of memory allocated for it must be sufficient to build the BLAS
    // for the provided geometry. Usually this is achieved by passing the same geometry descriptors to this function
    // and to IDevice::createAccelStruct(...).
    // When updating a BLAS, the geometries and primitive counts must match the BLAS that was previously built,
    // and the BLAS must have been built with the AllowUpdate flag.
    // If compaction is enabled when building the BLAS, the BLAS cannot be rebuilt or updated later, it can only
    // be compacted.
    // - DX11: Not supported.
    // - DX12: Maps to BuildRaytracingAccelerationStructure, or NvAPI_D3D12_BuildRaytracingAccelerationStructureEx
    //   if Opacity Micromaps or Line-Swept Sphere geometries are supported by the device.
    // - Vulkan: Maps to vkCmdBuildAccelerationStructuresKHR.
    // If NVRHI is built with RTXMU enabled, all BLAS builds, updates and compactions are handled by RTXMU.
    // Note that RTXMU currently doesn't support OMM or LSS.
    virtual void buildBottomLevelAccelStruct(IRayTracingAccelStruct* as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None) = 0;
    
    // Compacts all bottom-level ray tracing acceleration structures (BLASes) that are currently available
    // for compaction. This process is handled by the RTXMU library. If NVRHI is built without RTXMU,
    // this function has no effect.
    virtual void compactBottomLevelAccelStructs() = 0;

    // Builds or updates a top-level ray tracing acceleration structure (TLAS).
    // A temporary memory region for the build is suballocated using the scratch buffer manager attached to the
    // command list. The size of this memory region is determined automatically inside this function.
    // The type of operation to perform is specified by the buildFlags parameter.
    // When building a new TLAS, the amount of memory allocated for it must be sufficient to build the TLAS
    // for the provided geometry. Usually this is achieved by making sure that the instance count does not exceed
    // that provided to IDevice::createAccelStruct(...).
    // When updating a TLAS, the instance counts and types must match the TLAS that was previously built,
    // and the TLAS must have been built with the AllowUpdate flag.
    // - DX11: Not supported.
    // - DX12: Maps to BuildRaytracingAccelerationStructure.
    // - Vulkan: Maps to vkCmdBuildAccelerationStructuresKHR.
    virtual void buildTopLevelAccelStruct(IRayTracingAccelStruct* as, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None) = 0;

    // Performs one of the supported operations on clustered ray tracing acceleration structures (CLAS).
    // See the comments to RayTracingClusterOperationDesc for more information.
    // - DX11: Not supported.
    // - DX12: Maps to NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation and requires NVAPI.
    // - Vulkan: Not supported.
    virtual void executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& desc) = 0;

    // Builds or updates a top-level ray tracing acceleration structure (TLAS) using instance data provided
    // through a buffer on the GPU. The buffer must be pre-filled with RayTracingInstanceDesc structures using a
    // copy operation or a shader. No validation on the buffer contents is performed by NVRHI, and no state
    // or liveness tracking is done for the referenced BLAS'es.
    // See the comment to buildTopLevelAccelStruct(...) for more information.
    // - DX11: Not supported.
    // - DX12: Maps to BuildRaytracingAccelerationStructure.
    // - Vulkan: Maps to vkCmdBuildAccelerationStructuresKHR.
    virtual void buildTopLevelAccelStructFromBuffer(IRayTracingAccelStruct* as, IBuffer* instanceBuffer, u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None) = 0;

    // Converts one or several CoopVec compatible matrices between layouts in GPU memory.
    // Source and destination buffers must be different.
    // - DX11: Not supported.
    // - DX12: Maps to ConvertLinearAlgebraMatrix.
    // - Vulkan: Maps to vkCmdConvertCooperativeVectorMatrixNV.
    virtual void convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs) = 0;

    // Starts measuring GPU execution time using the provided timer query at this point in the command list.
    // Use endTimerQuery(...) to stop measusing time, and IDevice::getTimerQueryTime(...) to get the results later.
    // The same timer query cannot be used multiple times within the same command list, or in different
    // command lists until it is resolved.
    // - DX11: Maps to Begin and End calls on two ID3D11Query objects.
    // - DX12: Maps to EndQuery.
    // - Vulkan: Maps to vkCmdResetQueryPool and vkCmdWriteTimestamp.
    virtual void beginTimerQuery(ITimerQuery* query) = 0;

    // Stops measuring GPU execution time using the provided timer query at this point in the command list.
    // beginTimerQuery(...) must have been used on the same timer query in this command list previously.
    // - DX11: Maps to End calls on two ID3D11Query objects.
    // - DX12: Maps to EndQuery and ResolveQueryData.
    // - Vulkan: Maps to vkCmdWriteTimestamp.
    virtual void endTimerQuery(ITimerQuery* query) = 0;

    // Places a debug marker denoting the beginning of a range of commands in the command list.
    // Use endMarker() to denote the end of the range. Ranges may be nested, i.e. calling beginMarker(...)
    // multiple times, followed by multiple endMarker(), is allowed.
    // - DX11: Maps to ID3DUserDefinedAnnotation::BeginEvent.
    // - DX12: Maps to PIXBeginEvent.
    // - Vulkan: Maps to cmdBeginDebugUtilsLabelEXT or cmdDebugMarkerBeginEXT.
    // If Nsight Aftermath integration is enabled, also calls GFSDK_Aftermath_SetEventMarker on DX11 and DX12.
    virtual void beginMarker(const Name& name) = 0;

    // Places a debug marker denoting the end of a range of commands in the command list.
    // - DX11: Maps to ID3DUserDefinedAnnotation::EndEvent.
    // - DX12: Maps to PIXEndEvent.
    // - Vulkan: Maps to cmdEndDebugUtilsLabelEXT or cmdDebugMarkerEndEXT.
    virtual void endMarker() = 0;

    // Enables or disables the automatic barrier placement on set[...]State, copy, write, and clear operations.
    // By default, automatic barriers are enabled, but can be optionally disabled to improve CPU performance
    // and/or specific barrier placement. When automatic barriers are disabled, it is application's responsibility
    // to set correct states for all used resources.
    virtual void setEnableAutomaticBarriers(bool enable) = 0;

    // Sets the necessary resource states for all non-permanent resources used in the binding set.
    virtual void setResourceStatesForBindingSet(IBindingSet* bindingSet) = 0;
    
    // Sets the necessary resource states for all targets of the framebuffer.
    void setResourceStatesForFramebuffer(IFramebuffer* framebuffer);

    // Enables or disables the placement of UAV barriers for the given texture (DX12/VK) or all resources (DX11)
    // between draw or dispatch calls. Disabling UAV barriers may improve performance in cases when the same
    // resource is used by multiple draws or dispatches, but they don't depend on each other's results.
    // Note that this only affects barriers between multiple uses of the same texture as a UAV, and the
    // transition barrier when the texture is first used as a UAV will still be placed.
    // - DX11: Maps to NvAPI_D3D11_BeginUAVOverlap (once - see source code) and requires NVAPI.
    // - DX12, Vulkan: Does not map to any specific API calls, affects NVRHI automatic barriers.
    virtual void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) = 0;

    // Enables or disables the placement of UAV barriers for the given buffer (DX12/VK) or all resources (DX11)
    // between draw or dispatch calls.
    // See the comment to setEnableUavBarriersForTexture(...) for more information.
    virtual void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) = 0;

    // Informs the command list state tracker of the current state of a texture or some of its subresources.
    // This function must be called after opening the command list and before the first use of any textures 
    // that do not have the keepInitialState flag set, and that were not transitioned to a permanent state
    // previously using setPermanentTextureState(...).
    virtual void beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits) = 0;

    // Informs the command list state tracker of the current state of a buffer.
    // See the comment to beginTrackingTextureState(...) for more information.
    virtual void beginTrackingBufferState(IBuffer* buffer, ResourceStates::Mask stateBits) = 0;

    // Places the neccessary barriers to make sure that the texture or some of its subresources are in the given
    // state. If the texture or subresources are already in that state, no action is performed.
    // If the texture was previously transitioned to a permanent state, the new state must be compatible
    // with that permanent state, and no action is performed.
    // The barriers are not immediately submitted to the underlying graphics API, but are placed to the pending
    // list instead. Call commitBarriers() to submit them to the grahics API explicitly or set graphics
    // or other type of state.
    // Has no effect on DX11.
    virtual void setTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits) = 0;

    // Places the neccessary barriers to make sure that the buffer is in the given state.
    // See the comment to setTextureState(...) for more information.
    // Has no effect on DX11.
    virtual void setBufferState(IBuffer* buffer, ResourceStates::Mask stateBits) = 0;

    // Places the neccessary barriers to make sure that the underlying buffer for the acceleration structure is
    // in the given state. See the comment to setTextureState(...) for more information.
    // Has no effect on DX11.
    virtual void setAccelStructState(IRayTracingAccelStruct* as, ResourceStates::Mask stateBits) = 0;

    // Places the neccessary barriers to make sure that the entire texture is in the given state, and marks that
    // state as the texture's permanent state. Once a texture is transitioned into a permanent state, its state
    // can not be modified. This can improve performance by excluding the texture from automatic state tracking
    // in the future.
    // The barriers are not immediately submitted to the underlying graphics API, but are placed to the pending
    // list instead. Call commitBarriers() to submit them to the grahics API explicitly or set graphics
    // or other type of state.
    // Note that the permanent state transitions affect all command lists, and are only applied when the command
    // list that sets them is executed. If the command list is closed but not executed, the permanent states
    // will be abandoned.
    // Has no effect on DX11.
    virtual void setPermanentTextureState(ITexture* texture, ResourceStates::Mask stateBits) = 0;

    // Places the neccessary barriers to make sure that the buffer is in the given state, and marks that state
    // as the buffer's permanent state. See the comment to setPermanentTextureState(...) for more information.
    // Has no effect on DX11.
    virtual void setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask stateBits) = 0;

    // Flushes the barriers from the pending list into the graphics API command list.
    // Has no effect on DX11.
    virtual void commitBarriers() = 0;

    // Returns the current tracked state of a texture subresource.
    // If the state is not known to the command list, returns ResourceStates::Unknown. Using the texture in this
    // state is not allowed.
    // On DX11, always returns ResourceStates::Common.
    virtual ResourceStates::Mask getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel) = 0;
    
    // Returns the current tracked state of a buffer.
    // See the comment to getTextureSubresourceState(...) for more information.
    virtual ResourceStates::Mask getBufferState(IBuffer* buffer) = 0;

    // Returns the owning device, does NOT call AddRef on it.
    virtual IDevice* getDevice() = 0;

    // Returns the CommandListParameters structure that was used to create the command list. 
    virtual const CommandListParameters& getDescription() = 0;
};
typedef RefCountPtr<ICommandList, BlankDeleter<ICommandList>> CommandListHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device


class AftermathCrashDumpHelper;

class IDevice : public IResource
{
public:
    virtual HeapHandle createHeap(const HeapDesc& d) = 0;

    virtual TextureHandle createTexture(const TextureDesc& d) = 0;
    virtual MemoryRequirements getTextureMemoryRequirements(ITexture* texture) = 0;
    virtual bool bindTextureMemory(ITexture* texture, IHeap* heap, u64 offset) = 0;

    virtual TextureHandle createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc) = 0;

    virtual StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess) = 0;
    virtual void* mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum cpuAccess, usize* outRowPitch) = 0;
    virtual void unmapStagingTexture(IStagingTexture* tex) = 0;

    virtual void getTextureTiling(ITexture* texture, u32* numTiles, PackedMipDesc* desc, TileShape* tileShape, u32* subresourceTilingsNum, SubresourceTiling* subresourceTilings) = 0;
    virtual void updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, u32 numTileMappings, CommandQueue::Enum executionQueue = CommandQueue::Graphics) = 0;

    virtual SamplerFeedbackTextureHandle createSamplerFeedbackTexture(ITexture* pairedTexture, const SamplerFeedbackTextureDesc& desc) = 0;
    virtual SamplerFeedbackTextureHandle createSamplerFeedbackForNativeTexture(ObjectType objectType, Object texture, ITexture* pairedTexture) = 0;

    virtual BufferHandle createBuffer(const BufferDesc& d) = 0;
    virtual void* mapBuffer(IBuffer* buffer, CpuAccessMode::Enum cpuAccess) = 0;
    virtual void unmapBuffer(IBuffer* buffer) = 0;
    virtual MemoryRequirements getBufferMemoryRequirements(IBuffer* buffer) = 0;
    virtual bool bindBufferMemory(IBuffer* buffer, IHeap* heap, u64 offset) = 0;

    virtual BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc) = 0;

    virtual ShaderHandle createShader(const ShaderDesc& d, const void* binary, usize binarySize) = 0;
    virtual ShaderHandle createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, u32 numConstants) = 0;
    virtual ShaderLibraryHandle createShaderLibrary(const void* binary, usize binarySize) = 0;
    
    virtual SamplerHandle createSampler(const SamplerDesc& d) = 0;

    // Note: vertexShader is only necessary on D3D11, otherwise it may be null
    virtual InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, IShader* vertexShader) = 0;
    
    // Event queries
    virtual EventQueryHandle createEventQuery() = 0;
    virtual void setEventQuery(IEventQuery* query, CommandQueue::Enum queue) = 0;
    virtual bool pollEventQuery(IEventQuery* query) = 0;
    virtual void waitEventQuery(IEventQuery* query) = 0;
    virtual void resetEventQuery(IEventQuery* query) = 0;

    // Timer queries - see also begin/endTimerQuery in ICommandList
    virtual TimerQueryHandle createTimerQuery() = 0;
    virtual bool pollTimerQuery(ITimerQuery* query) = 0;
    // returns time in seconds
    virtual f32 getTimerQueryTime(ITimerQuery* query) = 0;
    virtual void resetTimerQuery(ITimerQuery* query) = 0;

    // Returns the API kind that the RHI backend is running on top of.
    virtual GraphicsAPI::Enum getGraphicsAPI() = 0;
    
    virtual FramebufferHandle createFramebuffer(const FramebufferDesc& desc) = 0;
    
    virtual GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo) = 0;
    
    virtual ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc) = 0;

    virtual MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo) = 0;

    virtual RayTracingPipelineHandle createRayTracingPipeline(const RayTracingPipelineDesc& desc) = 0;
    
    virtual BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc) = 0;
    virtual BindingLayoutHandle createBindlessLayout(const BindlessLayoutDesc& desc) = 0;

    virtual BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout) = 0;
    virtual DescriptorTableHandle createDescriptorTable(IBindingLayout* layout) = 0;

    virtual void resizeDescriptorTable(IDescriptorTable* descriptorTable, u32 newSize, bool keepContents = true) = 0;
    virtual bool writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item) = 0;

    virtual RayTracingOpacityMicromapHandle createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc) = 0;
    virtual RayTracingAccelStructHandle createAccelStruct(const RayTracingAccelStructDesc& desc) = 0;
    virtual MemoryRequirements getAccelStructMemoryRequirements(IRayTracingAccelStruct* as) = 0;
    virtual RayTracingClusterOperationSizeInfo getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params) = 0;
    virtual bool bindAccelStructMemory(IRayTracingAccelStruct* as, IHeap* heap, u64 offset) = 0;
    
    virtual CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters()) = 0;
    virtual u64 executeCommandLists(ICommandList* const* pCommandLists, usize numCommandLists, CommandQueue::Enum executionQueue = CommandQueue::Graphics) = 0;
    virtual void queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance) = 0;
    // returns true if the wait completes successfully, false if detecting a problem (e.g. device removal)
    virtual bool waitForIdle() = 0;

    // Releases the resources that were referenced in the command lists that have finished executing.
    // IMPORTANT: Call this method at least once per frame.
    virtual void runGarbageCollection() = 0;

    virtual bool queryFeatureSupport(Feature::Enum feature, void* pInfo = nullptr, usize infoSize = 0) = 0;

    virtual FormatSupport::Mask queryFormatSupport(Format::Enum format) = 0;

    // Returns a list of supported CoopVec matrix multiplication formats and accumulation capabilities.
    virtual CooperativeVectorDeviceFeatures queryCoopVecFeatures() = 0;

    // Calculates and returns the on-device size for a CoopVec matrix of the given dimensions, type and layout.
    virtual usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, int rows, int columns) = 0;

    virtual Object getNativeQueue(ObjectType objectType, CommandQueue::Enum queue) = 0;

    virtual bool isAftermathEnabled() = 0;
    virtual AftermathCrashDumpHelper& getAftermathCrashDumpHelper() = 0;

    // Front-end for executeCommandLists(..., 1) for compatibility and convenience
    u64 executeCommandList(ICommandList* commandList, CommandQueue::Enum executionQueue = CommandQueue::Graphics){
        return executeCommandLists(&commandList, 1, executionQueue);
    }
};
typedef RefCountPtr<IDevice, BlankDeleter<IDevice>> DeviceHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{
    template<typename T>
    struct hash<RefCountPtr<T>>{
        size_t operator()(RefCountPtr<T> const& s)const noexcept{
            hash<T*> _hash;
            return _hash(s.Get());
        }
    };

    template<>
    struct hash<NWB::Core::TextureSubresourceSet>{
        size_t operator()(NWB::Core::TextureSubresourceSet const& s)const noexcept{
            usize hash = 0;
            NWB::Core::__hidden_core::hashCombine(hash, s.baseMipLevel);
            NWB::Core::__hidden_core::hashCombine(hash, s.numMipLevels);
            NWB::Core::__hidden_core::hashCombine(hash, s.baseArraySlice);
            NWB::Core::__hidden_core::hashCombine(hash, s.numArraySlices);
            return static_cast<size_t>(hash);
        }
    };

    template<>
    struct hash<NWB::Core::BufferRange>{
        size_t operator()(NWB::Core::BufferRange const& s)const noexcept{
            usize hash = 0;
            NWB::Core::__hidden_core::hashCombine(hash, s.byteOffset);
            NWB::Core::__hidden_core::hashCombine(hash, s.byteSize);
            return static_cast<size_t>(hash);
        }
    };

    template<>
    struct hash<NWB::Core::BindingSetItem>{
        size_t operator()(NWB::Core::BindingSetItem const& s)const noexcept{
            usize value = 0;
            NWB::Core::__hidden_core::hashCombine(value, s.resourceHandle);
            NWB::Core::__hidden_core::hashCombine(value, s.slot);
            NWB::Core::__hidden_core::hashCombine(value, s.type);
            NWB::Core::__hidden_core::hashCombine(value, s.dimension);
            NWB::Core::__hidden_core::hashCombine(value, s.format);
            NWB::Core::__hidden_core::hashCombine(value, s.rawData[0]);
            NWB::Core::__hidden_core::hashCombine(value, s.rawData[1]);
            return static_cast<size_t>(value);
        }
    };

    template<>
    struct hash<NWB::Core::BindingSetDesc>{
        size_t operator()(NWB::Core::BindingSetDesc const& s)const noexcept{
            usize value = 0;
            for(const auto& item : s.bindings)
                NWB::Core::__hidden_core::hashCombine(value, item);
            return static_cast<size_t>(value);
        }
    };

    template<>
    struct hash<NWB::Core::FramebufferInfo>{
        size_t operator()(NWB::Core::FramebufferInfo const& s)const noexcept{
            usize hash = 0;
            for(auto format : s.colorFormats)
                NWB::Core::__hidden_core::hashCombine(hash, format);
            NWB::Core::__hidden_core::hashCombine(hash, s.depthFormat);
            NWB::Core::__hidden_core::hashCombine(hash, s.sampleCount);
            NWB::Core::__hidden_core::hashCombine(hash, s.sampleQuality);
            return static_cast<size_t>(hash);
        }
    };

    template<>
    struct hash<NWB::Core::BlendState::RenderTarget>{
        size_t operator()(NWB::Core::BlendState::RenderTarget const& s)const noexcept{
            usize hash = 0;
            NWB::Core::__hidden_core::hashCombine(hash, s.blendEnable);
            NWB::Core::__hidden_core::hashCombine(hash, s.srcBlend);
            NWB::Core::__hidden_core::hashCombine(hash, s.destBlend);
            NWB::Core::__hidden_core::hashCombine(hash, s.blendOp);
            NWB::Core::__hidden_core::hashCombine(hash, s.srcBlendAlpha);
            NWB::Core::__hidden_core::hashCombine(hash, s.destBlendAlpha);
            NWB::Core::__hidden_core::hashCombine(hash, s.blendOpAlpha);
            NWB::Core::__hidden_core::hashCombine(hash, s.colorWriteMask);
            return static_cast<size_t>(hash);
        }
    };

    template<>
    struct hash<NWB::Core::BlendState>{
        size_t operator()(NWB::Core::BlendState const& s)const noexcept{
            usize hash = 0;
            NWB::Core::__hidden_core::hashCombine(hash, s.alphaToCoverageEnable);
            for(const auto& target : s.targets)
                NWB::Core::__hidden_core::hashCombine(hash, target);
            return static_cast<size_t>(hash);
        }
    };
    
    template<>
    struct hash<NWB::Core::VariableRateShadingState>{
        size_t operator()(NWB::Core::VariableRateShadingState const& s)const noexcept{
            usize hash = 0;
            NWB::Core::__hidden_core::hashCombine(hash, s.enabled);
            NWB::Core::__hidden_core::hashCombine(hash, s.shadingRate);
            NWB::Core::__hidden_core::hashCombine(hash, s.pipelinePrimitiveCombiner);
            NWB::Core::__hidden_core::hashCombine(hash, s.imageCombiner);
            return static_cast<size_t>(hash);
        }
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

