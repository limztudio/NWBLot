// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/common/common.h>
#include <core/alloc/assetPool.h>

#include "basic.h"


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
    virtual const HeapDesc& getDescription() = 0;
};

template <typename Deleter = DefaultDeleter<IHeap>>
using HeapHandle = RefCountPtr<IHeap, Deleter>;

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
        Shared              = 0x01,

        // D3D11: adds (D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
        // D3D12, Vulkan: ignored
        Shared_NTHandle     = 0x02,

        // D3D12: adds D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER and D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER
        // D3D11, Vulkan: ignored
        Shared_CrossAdapter = 0x04,
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
#ifdef NWB_DEBUG
    AString debugName;
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
#ifdef NWB_DEBUG
    TextureDesc& setDebugName(const AString& v)noexcept{ debugName = v; return *this; }
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

