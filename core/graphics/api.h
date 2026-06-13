// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/common/module.h>
#include <core/alloc/module.h>

#include "resource_base.h"
#include "shader_param.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsAPI{
    enum Enum : u8;
};

namespace GraphicsBackend{
    class BackendContext;
    using Backend = BackendContext;
    class Device;
    class Heap;
    class Texture;
    class StagingTexture;
    class SamplerFeedbackTexture;
    class Buffer;
    class Shader;
    class ShaderLibrary;
    class Sampler;
    class InputLayout;
    class Framebuffer;
    class AccelStruct;
    class OpacityMicromap;
    class BindingLayout;
    class BindingSet;
    class DescriptorTable;
    class GraphicsPipeline;
    class ComputePipeline;
    class MeshletPipeline;
    class EventQuery;
    class TimerQuery;
    class ShaderTable;
    class RayTracingPipeline;
    class CommandList;

    using RayTracingOpacityMicromap = OpacityMicromap;
    using RayTracingAccelStruct = AccelStruct;
    using RayTracingShaderTable = ShaderTable;

#define NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Type) \
    u32 RefCountAddReference(Type* value)noexcept; \
    u32 RefCountRelease(Type* value)noexcept; \
    void DestroyArenaReference(Alloc::GlobalArena* arena, Type* value)noexcept;

    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Device)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Heap)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Texture)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(StagingTexture)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(SamplerFeedbackTexture)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Buffer)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Shader)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(ShaderLibrary)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Sampler)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(InputLayout)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Framebuffer)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(AccelStruct)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(OpacityMicromap)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(BindingLayout)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(BindingSet)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(DescriptorTable)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(GraphicsPipeline)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(ComputePipeline)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(MeshletPipeline)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(EventQuery)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(TimerQuery)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(ShaderTable)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(RayTracingPipeline)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(CommandList)

#undef NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS

    template<typename T>
    using Handle = RefCountPtr<T, ArenaRefDeleter<T>>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Device = GraphicsBackend::Device;
using Heap = GraphicsBackend::Heap;
using Texture = GraphicsBackend::Texture;
using StagingTexture = GraphicsBackend::StagingTexture;
using SamplerFeedbackTexture = GraphicsBackend::SamplerFeedbackTexture;
using InputLayout = GraphicsBackend::InputLayout;
using Buffer = GraphicsBackend::Buffer;
using Shader = GraphicsBackend::Shader;
using ShaderLibrary = GraphicsBackend::ShaderLibrary;
using Sampler = GraphicsBackend::Sampler;
using Framebuffer = GraphicsBackend::Framebuffer;
using RayTracingOpacityMicromap = GraphicsBackend::RayTracingOpacityMicromap;
using RayTracingAccelStruct = GraphicsBackend::RayTracingAccelStruct;
using BindingLayout = GraphicsBackend::BindingLayout;
using BindingSet = GraphicsBackend::BindingSet;
using DescriptorTable = GraphicsBackend::DescriptorTable;
using GraphicsPipeline = GraphicsBackend::GraphicsPipeline;
using ComputePipeline = GraphicsBackend::ComputePipeline;
using MeshletPipeline = GraphicsBackend::MeshletPipeline;
using EventQuery = GraphicsBackend::EventQuery;
using TimerQuery = GraphicsBackend::TimerQuery;
using RayTracingShaderTable = GraphicsBackend::RayTracingShaderTable;
using RayTracingPipeline = GraphicsBackend::RayTracingPipeline;
using CommandList = GraphicsBackend::CommandList;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CoreDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
void HashCombine(usize& seed, const T& v){
    ::HashCombine(seed, v);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MaxRenderTargets = 8;
inline constexpr u32 s_MaxViewports = 16;
inline constexpr u32 s_MaxVertexAttributes = 16;
inline constexpr u32 s_MaxBindingLayouts = 8;
inline constexpr u32 s_MaxBindlessRegisterSpaces = 16;
inline constexpr u32 s_MaxVolatileConstantBuffersPerLayout = 6;
inline constexpr u32 s_MaxVolatileConstantBuffers = 32;
inline constexpr u32 s_MaxPushConstantSize = 128;
inline constexpr u32 s_ConstantBufferOffsetSizeAlignment = 256;
inline constexpr u32 s_MaxAftermathEventStrings = 128;

inline constexpr u32 s_BindingOffsetShaderResource = 0;
inline constexpr u32 s_BindingOffsetSampler = 128;
inline constexpr u32 s_BindingOffsetConstantBuffer = 256;
inline constexpr u32 s_BindingOffsetUnorderedAccess = 384;

inline constexpr i32 s_WindowPositionAuto = -1;
inline constexpr u32 s_BackBufferWidth = 1280;
inline constexpr u32 s_BackBufferHeight = 720;
inline constexpr u32 s_SwapChainBufferCount = 3;
inline constexpr u32 s_MaxFramesInFlight = 2;
inline constexpr f32 s_DepthClearValue = 1.0f;
inline constexpr f64 s_AverageFrameTimeUpdateIntervalSeconds = 0.5;

using GraphicsArena = Alloc::GlobalArena;
using GraphicsString = AString<GraphicsArena>;
using GraphicsTString = TString<GraphicsArena>;
template<typename T>
using GraphicsVector = Vector<T, GraphicsArena>;
using GraphicsBytes = GraphicsVector<u8>;
template<typename T>
using GraphicsSet = Set<T, GraphicsArena>;
template<typename T>
using GraphicsDeque = Deque<T, GraphicsArena>;
template<typename T, typename V>
using GraphicsHashMap = HashMap<T, V, GraphicsArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_GRAPHICS_MASK_OPERATORS(MaskType) \
    constexpr MaskType operator|(MaskType lhs, MaskType rhs)noexcept{ return static_cast<MaskType>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); } \
    constexpr MaskType operator&(MaskType lhs, MaskType rhs)noexcept{ return static_cast<MaskType>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); } \
    constexpr MaskType& operator|=(MaskType& lhs, MaskType rhs)noexcept{ lhs = lhs | rhs; return lhs; } \
    constexpr MaskType& operator&=(MaskType& lhs, MaskType rhs)noexcept{ lhs = lhs & rhs; return lhs; } \
    constexpr MaskType operator~(MaskType value)noexcept{ return static_cast<MaskType>(~static_cast<u32>(value)); } \
    constexpr bool operator!(MaskType value)noexcept{ return static_cast<u32>(value) == 0; } \
    constexpr bool operator==(MaskType lhs, MaskType rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); } \
    constexpr bool operator!=(MaskType lhs, MaskType rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics allocator


class GraphicsAllocator : NoCopy{
public:
    explicit GraphicsAllocator(Alloc::GlobalArena& objectArena);


public:
    [[nodiscard]] Alloc::GlobalArena& getObjectArena()noexcept{ return m_objectArena; }


private:
    Alloc::GlobalArena& m_objectArena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Basic types


struct Color{
    f32 r, g, b, a;

    constexpr Color()noexcept
        : r(0)
        , g(0)
        , b(0)
        , a(0)
    {}
    constexpr Color(f32 c)noexcept
        : r(c)
        , g(c)
        , b(c)
        , a(c)
    {}
    constexpr Color(f32 red, f32 green, f32 blue, f32 alpha)noexcept
        : r(red)
        , g(green)
        , b(blue)
        , a(alpha)
    {}
};
inline bool operator==(const Color& lhs, const Color& rhs)noexcept{
    return (lhs.r == rhs.r) && (lhs.g == rhs.g) && (lhs.b == rhs.b) && (lhs.a == rhs.a);
}
inline bool operator!=(const Color& lhs, const Color& rhs)noexcept{ return !(lhs == rhs); }

struct UIntColor{
    u32 r, g, b, a;

    constexpr UIntColor()noexcept
        : r(0)
        , g(0)
        , b(0)
        , a(0)
    {}
    constexpr UIntColor(u32 c)noexcept
        : r(c)
        , g(c)
        , b(c)
        , a(c)
    {}
    constexpr UIntColor(u32 red, u32 green, u32 blue, u32 alpha)noexcept
        : r(red)
        , g(green)
        , b(blue)
        , a(alpha)
    {}
};
inline bool operator==(const UIntColor& lhs, const UIntColor& rhs)noexcept{
    return (lhs.r == rhs.r) && (lhs.g == rhs.g) && (lhs.b == rhs.b) && (lhs.a == rhs.a);
}
inline bool operator!=(const UIntColor& lhs, const UIntColor& rhs)noexcept{ return !(lhs == rhs); }

struct IntColor{
    i32 r, g, b, a;

    constexpr IntColor()noexcept
        : r(0)
        , g(0)
        , b(0)
        , a(0)
    {}
    constexpr IntColor(i32 c)noexcept
        : r(c)
        , g(c)
        , b(c)
        , a(c)
    {}
    constexpr IntColor(i32 red, i32 green, i32 blue, i32 alpha)noexcept
        : r(red)
        , g(green)
        , b(blue)
        , a(alpha)
    {}
};
inline bool operator==(const IntColor& lhs, const IntColor& rhs)noexcept{
    return (lhs.r == rhs.r) && (lhs.g == rhs.g) && (lhs.b == rhs.b) && (lhs.a == rhs.a);
}
inline bool operator!=(const IntColor& lhs, const IntColor& rhs)noexcept{ return !(lhs == rhs); }


struct Viewport{
    f32 minX, maxX;
    f32 minY, maxY;
    f32 minZ, maxZ;

    constexpr Viewport()noexcept
        : minX(0)
        , maxX(0)
        , minY(0)
        , maxY(0)
        , minZ(0)
        , maxZ(0)
    {}
    constexpr Viewport(f32 width, f32 height)noexcept
        : minX(0)
        , maxX(width)
        , minY(0)
        , maxY(height)
        , minZ(0)
        , maxZ(1)
    {}
    constexpr Viewport(f32 minXValue, f32 maxXValue, f32 minYValue, f32 maxYValue, f32 minZValue, f32 maxZValue)noexcept
        : minX(minXValue)
        , maxX(maxXValue)
        , minY(minYValue)
        , maxY(maxYValue)
        , minZ(minZValue)
        , maxZ(maxZValue)
    {}

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

    constexpr Rect()noexcept
        : minX(0)
        , maxX(0)
        , minY(0)
        , maxY(0)
    {}
    constexpr Rect(i32 width, i32 height)noexcept
        : minX(0)
        , maxX(width)
        , minY(0)
        , maxY(height)
    {}
    constexpr Rect(i32 minXValue, i32 maxXValue, i32 minYValue, i32 maxYValue)noexcept
        : minX(minXValue)
        , maxX(maxXValue)
        , minY(minYValue)
        , maxY(maxYValue)
    {}
    explicit Rect(const Viewport& viewport)noexcept
        : minX(static_cast<i32>(Floor(viewport.minX)))
        , maxX(static_cast<i32>(Ceil(viewport.maxX)))
        , minY(static_cast<i32>(Floor(viewport.minY)))
        , maxY(static_cast<i32>(Ceil(viewport.maxY)))
    {}

    [[nodiscard]] constexpr i32 width()const noexcept{ return maxX - minX; }
    [[nodiscard]] constexpr i32 height()const noexcept{ return maxY - minY; }
};
inline bool operator==(const Rect& lhs, const Rect& rhs)noexcept{
    return (lhs.minX == rhs.minX) && (lhs.maxX == rhs.maxX) && (lhs.minY == rhs.minY) && (lhs.maxY == rhs.maxY);
}
inline bool operator!=(const Rect& lhs, const Rect& rhs)noexcept{ return !(lhs == rhs); }

struct Box{
    i32 minX, maxX;
    i32 minY, maxY;
    i32 minZ, maxZ;

    constexpr Box()noexcept
        : minX(0)
        , maxX(0)
        , minY(0)
        , maxY(0)
        , minZ(0)
        , maxZ(0)
    {}
    constexpr Box(i32 width, i32 height, i32 depth)noexcept
        : minX(0)
        , maxX(width)
        , minY(0)
        , maxY(height)
        , minZ(0)
        , maxZ(depth)
    {}
    constexpr Box(i32 minXValue, i32 maxXValue, i32 minYValue, i32 maxYValue, i32 minZValue, i32 maxZValue)noexcept
        : minX(minXValue)
        , maxX(maxXValue)
        , minY(minYValue)
        , maxY(maxYValue)
        , minZ(minZValue)
        , maxZ(maxZValue)
    {}
    constexpr Box(const Rect& rect, i32 minZValue, i32 maxZValue)noexcept
        : minX(rect.minX)
        , maxX(rect.maxX)
        , minY(rect.minY)
        , maxY(rect.maxY)
        , minZ(minZValue)
        , maxZ(maxZValue)
    {}

    [[nodiscard]] constexpr i32 width()const noexcept{ return maxX - minX; }
    [[nodiscard]] constexpr i32 height()const noexcept{ return maxY - minY; }
    [[nodiscard]] constexpr i32 depth()const noexcept{ return maxZ - minZ; }
};
inline bool operator==(const Box& lhs, const Box& rhs)noexcept{
    return
        (lhs.minX == rhs.minX) && (lhs.maxX == rhs.maxX)
        && (lhs.minY == rhs.minY) && (lhs.maxY == rhs.maxY)
        && (lhs.minZ == rhs.minZ) && (lhs.maxZ == rhs.maxZ)
    ;
}
inline bool operator!=(const Box& lhs, const Box& rhs)noexcept{ return !(lhs == rhs); }


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
        RGBA8_UNORM_SRGB,
        BGRA8_UNORM,
        BGRA8_UNORM_SRGB,
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

        ASTC_4x4_UNORM,
        ASTC_4x4_UNORM_SRGB,
        ASTC_4x4_FLOAT,
        ASTC_5x4_UNORM,
        ASTC_5x4_UNORM_SRGB,
        ASTC_5x4_FLOAT,
        ASTC_5x5_UNORM,
        ASTC_5x5_UNORM_SRGB,
        ASTC_5x5_FLOAT,
        ASTC_6x5_UNORM,
        ASTC_6x5_UNORM_SRGB,
        ASTC_6x5_FLOAT,
        ASTC_6x6_UNORM,
        ASTC_6x6_UNORM_SRGB,
        ASTC_6x6_FLOAT,
        ASTC_8x5_UNORM,
        ASTC_8x5_UNORM_SRGB,
        ASTC_8x5_FLOAT,
        ASTC_8x6_UNORM,
        ASTC_8x6_UNORM_SRGB,
        ASTC_8x6_FLOAT,
        ASTC_10x5_UNORM,
        ASTC_10x5_UNORM_SRGB,
        ASTC_10x5_FLOAT,
        ASTC_10x6_UNORM,
        ASTC_10x6_UNORM_SRGB,
        ASTC_10x6_FLOAT,
        ASTC_8x8_UNORM,
        ASTC_8x8_UNORM_SRGB,
        ASTC_8x8_FLOAT,
        ASTC_10x8_UNORM,
        ASTC_10x8_UNORM_SRGB,
        ASTC_10x8_FLOAT,
        ASTC_10x10_UNORM,
        ASTC_10x10_UNORM_SRGB,
        ASTC_10x10_FLOAT,
        ASTC_12x10_UNORM,
        ASTC_12x10_UNORM_SRGB,
        ASTC_12x10_FLOAT,
        ASTC_12x12_UNORM,
        ASTC_12x12_UNORM_SRGB,
        ASTC_12x12_FLOAT,

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
[[nodiscard]] u32 GetFormatBlockWidth(const FormatInfo& formatInfo)noexcept;
[[nodiscard]] u32 GetFormatBlockHeight(const FormatInfo& formatInfo)noexcept;

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

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
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
    HeapType::Enum type = HeapType::DeviceLocal;
    Name debugName;
};

typedef GraphicsBackend::Handle<Heap> HeapHandle;

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

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

typedef u32 MipLevel;
typedef u32 ArraySlice;

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

    bool isShaderResource = true;
    bool isRenderTarget = false;
    bool isUAV = false;
    bool isTypeless = false;
    bool isShadingRateSurface = false;

    // Indicates that the texture is created with no backing memory,
    // and memory is bound to the texture later using bindTextureMemory.
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
};

struct TextureSlice{
    u32 x = 0;
    u32 y = 0;
    u32 z = 0;

    // -1 means the entire dimension is part of the region
    // resolve() will translate these values into actual dimensions
    u32 width = static_cast<u32>(-1);
    u32 height = static_cast<u32>(-1);
    u32 depth = static_cast<u32>(-1);

    MipLevel mipLevel = 0;
    ArraySlice arraySlice = 0;

    [[nodiscard]] TextureSlice resolve(const TextureDesc& desc)const;
    [[nodiscard]] TextureSlice resolve(u32 mipWidth, u32 mipHeight, u32 mipDepth)const;

    constexpr TextureSlice& setOrigin(u32 vx = 0, u32 vy = 0, u32 vz = 0){ x = vx; y = vy; z = vz; return *this; }
    constexpr TextureSlice& setWidth(u32 value){ width = value; return *this; }
    constexpr TextureSlice& setHeight(u32 value){ height = value; return *this; }
    constexpr TextureSlice& setDepth(u32 value){ depth = value; return *this; }
    constexpr TextureSlice& setSize(u32 vx = static_cast<u32>(-1), u32 vy = static_cast<u32>(-1), u32 vz = static_cast<u32>(-1)){ width = vx; height = vy; depth = vz; return *this; }
    constexpr TextureSlice& setMipLevel(MipLevel level){ mipLevel = level; return *this; }
    constexpr TextureSlice& setArraySlice(ArraySlice slice){ arraySlice = slice; return *this; }
};

namespace TextureSubresourceMipResolve{
    enum Enum : u8{
        Range = 0u,
        Single = 1u,
    };
};

struct TextureSubresourceSet{
    static constexpr auto AllMipLevels = static_cast<MipLevel>(-1);
    static constexpr auto AllArraySlices = static_cast<ArraySlice>(-1);

    MipLevel baseMipLevel = 0;
    MipLevel numMipLevels = 1;
    ArraySlice baseArraySlice = 0;
    ArraySlice numArraySlices = 1;

    [[nodiscard]] TextureSubresourceSet resolve(const TextureDesc& desc, TextureSubresourceMipResolve::Enum mipResolve)const;
    [[nodiscard]] bool isEntireTexture(const TextureDesc& desc)const;

    constexpr TextureSubresourceSet() = default;
    constexpr TextureSubresourceSet(
        MipLevel baseMipLevelValue,
        MipLevel numMipLevelsValue,
        ArraySlice baseArraySliceValue,
        ArraySlice numArraySlicesValue
    )
        : baseMipLevel(baseMipLevelValue)
        , numMipLevels(numMipLevelsValue)
        , baseArraySlice(baseArraySliceValue)
        , numArraySlices(numArraySlicesValue)
    {}

    constexpr TextureSubresourceSet& setBaseMipLevel(MipLevel value){ baseMipLevel = value; return *this; }
    constexpr TextureSubresourceSet& setNumMipLevels(MipLevel value){ numMipLevels = value; return *this; }
    constexpr TextureSubresourceSet& setMipLevels(MipLevel base, MipLevel num){ baseMipLevel = base; numMipLevels = num; return *this; }
    constexpr TextureSubresourceSet& setBaseArraySlice(ArraySlice value){ baseArraySlice = value; return *this; }
    constexpr TextureSubresourceSet& setNumArraySlices(ArraySlice value){ numArraySlices = value; return *this; }
    constexpr TextureSubresourceSet& setArraySlices(ArraySlice base, ArraySlice num){ baseArraySlice = base; numArraySlices = num; return *this; }

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

inline constexpr auto s_AllSubresources = TextureSubresourceSet(0, TextureSubresourceSet::AllMipLevels, 0, TextureSubresourceSet::AllArraySlices);

typedef GraphicsBackend::Handle<Texture> TextureHandle;

typedef GraphicsBackend::Handle<StagingTexture> StagingTextureHandle;

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
    Heap* heap = nullptr;
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

typedef GraphicsBackend::Handle<SamplerFeedbackTexture> SamplerFeedbackTextureHandle;


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

typedef GraphicsBackend::Handle<InputLayout> InputLayoutHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffer


struct BufferDesc{
    u64 byteSize = 0;
    u32 structStride = 0; // if non-zero it's structured
    u32 maxVersions = 0; // only valid and required to be nonzero for volatile buffers on backends that keep per-version state
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

    // Equivalent to .setInitialState(initialStateValue).setKeepInitialState(true)
    constexpr BufferDesc& enableAutomaticStateTracking(ResourceStates::Mask initialStateValue){
        initialState = initialStateValue;
        keepInitialState = true;
        return *this;
    }
};

struct BufferRange{
    u64 byteOffset = 0;
    u64 byteSize = 0;

    BufferRange() = default;
    constexpr BufferRange(u64 byteOffsetValue, u64 byteSizeValue)
        : byteOffset(byteOffsetValue)
        , byteSize(byteSizeValue)
    {}

    [[nodiscard]] BufferRange resolve(const BufferDesc& desc)const;
    [[nodiscard]] constexpr bool isEntireBuffer(const BufferDesc& desc)const{ return (!byteOffset) && (byteSize == static_cast<u64>(-1) || byteSize == desc.byteSize); }
    constexpr bool operator==(const BufferRange& other)const{ return byteOffset == other.byteOffset && byteSize == other.byteSize; }

    constexpr BufferRange& setByteOffset(u64 value){ byteOffset = value; return *this; }
    constexpr BufferRange& setByteSize(u64 value){ byteSize = value; return *this; }
};

inline constexpr BufferRange s_EntireBuffer = BufferRange(0, static_cast<u64>(-1));

typedef GraphicsBackend::Handle<Buffer> BufferHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader


namespace ShaderType{
    enum Enum : u8{
        VertexStage = 0,
        HullStage = 1,
        DomainStage = 2,
        GeometryStage = 3,
        PixelStage = 4,
        ComputeStage = 5,
        AmplificationStage = 6,
        MeshStage = 7,
        RayGenerationStage = 8,
        AnyHitStage = 9,
        ClosestHitStage = 10,
        MissStage = 11,
        IntersectionStage = 12,
        CallableStage = 13,

        Count,
        Invalid = Count,
    };
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

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)

    [[nodiscard]] inline constexpr bool IsValid(const Enum shaderType)noexcept{
        return shaderType < Count;
    }

    [[nodiscard]] inline constexpr usize ToIndex(const Enum shaderType)noexcept{
        return static_cast<usize>(shaderType);
    }

    [[nodiscard]] inline constexpr Mask ToMask(const Enum shaderType)noexcept{
        switch(shaderType){
            case VertexStage: return Vertex;
            case HullStage: return Hull;
            case DomainStage: return Domain;
            case GeometryStage: return Geometry;
            case PixelStage: return Pixel;
            case ComputeStage: return Compute;
            case AmplificationStage: return Amplification;
            case MeshStage: return Mesh;
            case RayGenerationStage: return RayGeneration;
            case AnyHitStage: return AnyHit;
            case ClosestHitStage: return ClosestHit;
            case MissStage: return Miss;
            case IntersectionStage: return Intersection;
            case CallableStage: return Callable;
            default: return None;
        }
    }

    [[nodiscard]] inline constexpr Enum ToEnum(const Mask shaderType)noexcept{
        switch(shaderType){
            case Vertex: return VertexStage;
            case Hull: return HullStage;
            case Domain: return DomainStage;
            case Geometry: return GeometryStage;
            case Pixel: return PixelStage;
            case Compute: return ComputeStage;
            case Amplification: return AmplificationStage;
            case Mesh: return MeshStage;
            case RayGeneration: return RayGenerationStage;
            case AnyHit: return AnyHitStage;
            case ClosestHit: return ClosestHitStage;
            case Miss: return MissStage;
            case Intersection: return IntersectionStage;
            case Callable: return CallableStage;
            default: return Invalid;
        }
    }
};

namespace FastGeometryShaderFlags{
    enum Mask : u8{
        None                             = 0,

        ForceFastGS                      = 1 << 0,
        UseViewportMask                  = 1 << 1,
        OffsetTargetIndexByViewportIndex = 1 << 2,
        StrictApiOrder                   = 1 << 3,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
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
    GraphicsString entryName;

    i32 hlslExtensionsUAV = -1;

    bool useSpecificShaderExt = false;
    u32 numCustomSemantics = 0;
    CustomSemantic* pCustomSemantics = nullptr;

    FastGeometryShaderFlags::Mask fastGSFlags = FastGeometryShaderFlags::None;
    u32* pCoordinateSwizzling = nullptr;

    explicit ShaderDesc(GraphicsArena& arena)
        : entryName("main", arena)
    {}

    constexpr ShaderDesc& setShaderType(ShaderType::Mask value){ shaderType = value; return *this; }
    constexpr ShaderDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    ShaderDesc& setEntryName(const AStringView value){ entryName.assign(value); return *this; }
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

typedef GraphicsBackend::Handle<Shader> ShaderHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Library


typedef GraphicsBackend::Handle<ShaderLibrary> ShaderLibraryHandle;


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

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
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

        RenderTarget targets[s_MaxRenderTargets];
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

        for(u32 i = 0; i < s_MaxRenderTargets; ++i){
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
    constexpr RasterState& setSamplePositions(const i8* x, const i8* y, usize count){
        if(!x || !y)
            return *this;
        const usize samplePositionCount = count < 16 ? count : 16;
        for(usize i = 0; i < samplePositionCount; ++i){
            samplePositionsX[i] = x[i];
            samplePositionsY[i] = y[i];
        }
        return *this;
    }
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
    FixedVector<Viewport, s_MaxViewports> viewports;
    FixedVector<Rect, s_MaxViewports> scissorRects;

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

typedef GraphicsBackend::Handle<Sampler> SamplerHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame buffer


struct FramebufferAttachment{
    Texture* texture = nullptr;
    TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, 1);
    Format::Enum format = Format::UNKNOWN;
    bool isReadOnly = false;

    constexpr FramebufferAttachment& setTexture(Texture* t){ texture = t; return *this; }
    constexpr FramebufferAttachment& setSubresources(TextureSubresourceSet value){ subresources = value; return *this; }
    constexpr FramebufferAttachment& setArraySlice(ArraySlice index){ subresources.baseArraySlice = index; subresources.numArraySlices = 1; return *this; }
    constexpr FramebufferAttachment& setArraySliceRange(ArraySlice index, ArraySlice count){ subresources.baseArraySlice = index; subresources.numArraySlices = count; return *this; }
    constexpr FramebufferAttachment& setMipLevel(MipLevel level){ subresources.baseMipLevel = level; subresources.numMipLevels = 1; return *this; }
    constexpr FramebufferAttachment& setFormat(Format::Enum f){ format = f; return *this; }
    constexpr FramebufferAttachment& setReadOnly(bool val){ isReadOnly = val; return *this; }

    [[nodiscard]] constexpr bool valid()const{ return texture != nullptr; }
};

struct FramebufferDesc{
    FixedVector<FramebufferAttachment, s_MaxRenderTargets> colorAttachments;
    FramebufferAttachment depthAttachment;
    FramebufferAttachment shadingRateAttachment;

    constexpr FramebufferDesc& addColorAttachment(const FramebufferAttachment& a){ colorAttachments.push_back(a); return *this; }
    constexpr FramebufferDesc& addColorAttachment(Texture* texture){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture)); return *this; }
    constexpr FramebufferDesc& addColorAttachment(Texture* texture, TextureSubresourceSet subresources){ colorAttachments.push_back(FramebufferAttachment().setTexture(texture).setSubresources(subresources)); return *this; }
    constexpr FramebufferDesc& setDepthAttachment(const FramebufferAttachment& d){ depthAttachment = d; return *this; }
    constexpr FramebufferDesc& setDepthAttachment(Texture* texture){ depthAttachment = FramebufferAttachment().setTexture(texture); return *this; }
    constexpr FramebufferDesc& setDepthAttachment(Texture* texture, TextureSubresourceSet subresources){ depthAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
    constexpr FramebufferDesc& setShadingRateAttachment(const FramebufferAttachment& d){ shadingRateAttachment = d; return *this; }
    constexpr FramebufferDesc& setShadingRateAttachment(Texture* texture){ shadingRateAttachment = FramebufferAttachment().setTexture(texture); return *this; }
    constexpr FramebufferDesc& setShadingRateAttachment(Texture* texture, TextureSubresourceSet subresources){ shadingRateAttachment = FramebufferAttachment().setTexture(texture).setSubresources(subresources); return *this; }
};

struct FramebufferInfo{
    FixedVector<Format::Enum, s_MaxRenderTargets> colorFormats;
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

typedef GraphicsBackend::Handle<Framebuffer> FramebufferHandle;


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

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingOpacityMicromapUsageCount{
    // Number of OMMs with the specified subdivision level and format.
    u32 count = 0;
    // Micro triangle count is 4^N, where N is the subdivision level.
    u32 subdivisionLevel = 0;
    // OMM input sub format.
    OpacityMicromapFormat::Enum format = OpacityMicromapFormat::OC1_2_State;
};

struct RayTracingOpacityMicromapDesc{
    Name debugName;
    bool trackLiveness = true;

    // OMM flags. Applies to all OMMs in array.
    RayTracingOpacityMicromapBuildFlags::Mask flags = RayTracingOpacityMicromapBuildFlags::None;
    // OMM counts for each subdivision level and format combination in the inputs.
    GraphicsVector<RayTracingOpacityMicromapUsageCount> counts;

    // Base pointer for raw OMM input data.
    // Individual OMMs must be 1B aligned, though natural alignment is recommended.
    // It's also recommended to try to organize OMMs together that are expected to be used spatially close together.
    Buffer* inputBuffer = nullptr;
    u64 inputBufferOffset = 0;

    // One entry per OMM matching the VkMicromapTriangleEXT layout.
    Buffer* perOmmDescs = nullptr;
    u64 perOmmDescsOffset = 0;

    explicit RayTracingOpacityMicromapDesc(GraphicsArena& arena)
        : counts(arena)
    {}

    constexpr RayTracingOpacityMicromapDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setFlags(RayTracingOpacityMicromapBuildFlags::Mask value){ flags = value; return *this; }
    RayTracingOpacityMicromapDesc& setCounts(const GraphicsVector<RayTracingOpacityMicromapUsageCount>& value){ counts = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setInputBuffer(Buffer* value){ inputBuffer = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setInputBufferOffset(u64 value){ inputBufferOffset = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setPerOmmDescs(Buffer* value){ perOmmDescs = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setPerOmmDescsOffset(u64 value){ perOmmDescsOffset = value; return *this; }
};

typedef GraphicsBackend::Handle<RayTracingOpacityMicromap> RayTracingOpacityMicromapHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing AccelStruct


using AffineTransform = Float4[3];

inline constexpr AffineTransform s_identityTransform = {
    Float4(1.f, 0.f, 0.f, 0.f),
    Float4(0.f, 1.f, 0.f, 0.f),
    Float4(0.f, 0.f, 1.f, 0.f)
};
static_assert(sizeof(AffineTransform) == sizeof(f32) * 12u, "AffineTransform GPU layout drifted");
static_assert(alignof(AffineTransform) >= alignof(Float4), "AffineTransform must stay SIMD-aligned");

namespace RayTracingGeometryFlags{
    enum Mask : u8{
        None = 0,

        Opaque = 1 << 0,
        NoDuplicateAnyHitInvocation = 1 << 1,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
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
    Buffer* indexBuffer = nullptr;   // make sure the first 2 fields in all Geometry
    Buffer* vertexBuffer = nullptr;  // structs are Buffer* for easier debugging
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexFormat = Format::UNKNOWN;
    u64 indexOffset = 0;
    u64 vertexOffset = 0;
    u32 indexCount = 0;
    u32 vertexCount = 0;
    u32 vertexStride = 0;

    RayTracingOpacityMicromap* opacityMicromap = nullptr;
    Buffer* ommIndexBuffer = nullptr;
    u64 ommIndexBufferOffset = 0;
    Format::Enum ommIndexFormat = Format::UNKNOWN;
    const RayTracingOpacityMicromapUsageCount* pOmmUsageCounts = nullptr;
    u32 numOmmUsageCounts = 0;

    constexpr RayTracingGeometryTriangles& setIndexBuffer(Buffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexBuffer(Buffer* value){ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexFormat(Format::Enum value){ vertexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexOffset(u64 value){ vertexOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexStride(u32 value){ vertexStride = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOpacityMicromap(RayTracingOpacityMicromap* value){ opacityMicromap = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOmmIndexBuffer(Buffer* value){ ommIndexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOmmIndexBufferOffset(u64 value){ ommIndexBufferOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOmmIndexFormat(Format::Enum value){ ommIndexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setPOmmUsageCounts(const RayTracingOpacityMicromapUsageCount* value){ pOmmUsageCounts = value; return *this; }
    constexpr RayTracingGeometryTriangles& setNumOmmUsageCounts(u32 value){ numOmmUsageCounts = value; return *this; }
};

struct RayTracingGeometryAABBs{
    Buffer* buffer = nullptr;
    Buffer* unused = nullptr;
    u64 offset = 0;
    u32 count = 0;
    u32 stride = 0;

    constexpr RayTracingGeometryAABBs& setBuffer(Buffer* value){ buffer = value; return *this; }
    constexpr RayTracingGeometryAABBs& setOffset(u64 value){ offset = value; return *this; }
    constexpr RayTracingGeometryAABBs& setCount(u32 value){ count = value; return *this; }
    constexpr RayTracingGeometryAABBs& setStride(u32 value){ stride = value; return *this; }
};

struct RayTracingGeometrySpheres{
    Buffer* indexBuffer = nullptr;
    Buffer* vertexBuffer = nullptr;
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

    constexpr RayTracingGeometrySpheres& setIndexBuffer(Buffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexBuffer(Buffer* value){ vertexBuffer = value; return *this; }
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
    Buffer* indexBuffer = nullptr;
    Buffer* vertexBuffer = nullptr;
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

    constexpr RayTracingGeometryLss& setIndexBuffer(Buffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexBuffer(Buffer* value){ vertexBuffer = value; return *this; }
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

    RayTracingGeometryDesc()
        : geometryData{}
    {}

    RayTracingGeometryDesc& setTransform(const AffineTransform& value){ NWB_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); useTransform = true; return *this; }
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

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingInstanceDesc{
    AffineTransform transform;
    u32 instanceID : 24;
    u32 instanceMask : 8;
    u32 instanceContributionToHitGroupIndex : 24;
    RayTracingInstanceFlags::Mask flags : 8;
    union{
        RayTracingAccelStruct* bottomLevelAS; // for buildTopLevelAccelStruct
        u64 blasDeviceAddress;       // for buildTopLevelAccelStructFromBuffer - use RayTracingAccelStruct::getDeviceAddress()
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
    RayTracingInstanceDesc& setTransform(const AffineTransform& value){ NWB_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); return *this; }
    constexpr RayTracingInstanceDesc& setFlags(RayTracingInstanceFlags::Mask value){ flags = value; return *this; }
    constexpr RayTracingInstanceDesc& setBLAS(RayTracingAccelStruct* value){ bottomLevelAS = value; return *this; }
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

        // Allows a TLAS to include an instance that points at a null BLAS or has a zero instance mask.
        // Only affects local validation; it does not translate to backend AS build flags.
        AllowEmptyInstances = 0x80,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingAccelStructDesc{
    usize topLevelMaxInstances = 0; // only applies when isTopLevel = true
    GraphicsVector<RayTracingGeometryDesc> bottomLevelGeometries; // only applies when isTopLevel = false
    RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None;
    Name debugName;
    bool trackLiveness = true;
    bool isTopLevel = false;
    bool isVirtual = false;

    explicit RayTracingAccelStructDesc(GraphicsArena& arena)
        : bottomLevelGeometries(arena)
    {}

    constexpr RayTracingAccelStructDesc& setTopLevelMaxInstances(usize value){ topLevelMaxInstances = value; isTopLevel = true; return *this; }
    RayTracingAccelStructDesc& addBottomLevelGeometry(const RayTracingGeometryDesc& value){ bottomLevelGeometries.push_back(value); isTopLevel = false; return *this; }
    constexpr RayTracingAccelStructDesc& setBuildFlags(RayTracingAccelStructBuildFlags::Mask value){ buildFlags = value; return *this; }
    constexpr RayTracingAccelStructDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    constexpr RayTracingAccelStructDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
    constexpr RayTracingAccelStructDesc& setIsTopLevel(bool value){ isTopLevel = value; return *this; }
    constexpr RayTracingAccelStructDesc& setIsVirtual(bool value){ isVirtual = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing AccelStruct


typedef GraphicsBackend::Handle<RayTracingAccelStruct> RayTracingAccelStructHandle;


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

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
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
    RayTracingClusterOperationMoveType::Enum type = RayTracingClusterOperationMoveType::BottomLevel;
    u32 maxBytes = 0;
};

struct RayTracingClusterOperationClasBuildParams{
    // Vertex format accepted by the backend cluster acceleration structure implementation.
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

    RayTracingClusterOperationType::Enum type = RayTracingClusterOperationType::Move;
    RayTracingClusterOperationMode::Enum mode = RayTracingClusterOperationMode::ImplicitDestinations;
    RayTracingClusterOperationFlags::Mask flags = RayTracingClusterOperationFlags::None;

    RayTracingClusterOperationMoveParams move;
    RayTracingClusterOperationClasBuildParams clas;
    RayTracingClusterOperationBlasBuildParams blas;
};

struct RayTracingClusterOperationDesc{
    RayTracingClusterOperationParams params;

    u64 scratchSizeInBytes = 0;                             // Size of scratch resource returned by getClusterOperationSizeInfo() scratchSizeInBytes

    // Input Resources
    Buffer* inIndirectArgCountBuffer = nullptr;            // Buffer containing the number of AS to build, instantiate, or move
    u64 inIndirectArgCountOffsetInBytes = 0;                // Offset (in bytes) to where the count is in the inIndirectArgCountBuffer
    Buffer* inIndirectArgsBuffer = nullptr;                // Buffer of descriptor array of format IndirectTriangleClasArgs, IndirectTriangleTemplateArgs, IndirectInstantiateTemplateArgs
    u64 inIndirectArgsOffsetInBytes = 0;                    // Offset (in bytes) to where the descriptor array starts inIndirectArgsBuffer

    // In/Out Resources
    Buffer* inOutAddressesBuffer = nullptr;                // Array of addresseses of CLAS, CLAS Templates, or BLAS
    u64 inOutAddressesOffsetInBytes = 0;                    // Offset (in bytes) to where the addresses array starts in inOutAddressesBuffer

    // Output Resources
    Buffer* outSizesBuffer = nullptr;                      // Sizes (in bytes) of CLAS, CLAS Templates, or BLAS
    u64 outSizesOffsetInBytes = 0;                          // Offset (in bytes) to where the output sizes array starts in outSizesBuffer
    Buffer* outAccelerationStructuresBuffer = nullptr;     // Destination buffer for CLAS, CLAS Template, or BLAS data. Size must be calculated with getOperationSizeInfo or with the outSizesBuffer result of OperationMode::GetSizes
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
    ShaderType::Mask visibility = ShaderType::None;

    // DXC maps HLSL register spaces to SPIR-V descriptor sets, so this can be used as the descriptor set index.
    // Set `registerSpaceIsDescriptorSet` to enable that mapping explicitly.
    u32 registerSpace = 0;

    // This flag controls the behavior for pipelines that use multiple binding layouts.
    // When true, the layout uses `registerSpace` as its SPIR-V descriptor set index. Layouts in the same
    // pipeline must not reuse a descriptor set index.
    bool registerSpaceIsDescriptorSet = false;

    GraphicsVector<BindingLayoutItem> bindings;
    BindingOffsets bindingOffsets;

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
    ShaderType::Mask visibility = ShaderType::None;
    u32 firstSlot = 0;
    u32 maxCapacity = 0;
    FixedVector<BindingLayoutItem, s_MaxBindlessRegisterSpaces> registerSpaces;

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
    u8 unused                        : 8;

    u32 unused2;

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
        result.unused = 0;
        result.unused2 = 0;
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
    // When disabled, the caller must keep the binding set and referenced resources alive
    // until all commands using the binding set have finished.
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
    constexpr SinglePassStereoState& setRenderTargetIndexOffset(u16 value){ renderTargetIndexOffset = static_cast<u8>(value); return *this; }
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

typedef FixedVector<BindingLayoutHandle, s_MaxBindingLayouts> BindingLayoutVector;

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

    ~GraphicsPipelineDesc();

    constexpr GraphicsPipelineDesc& setPrimType(PrimitiveType::Enum value){ primType = value; return *this; }
    constexpr GraphicsPipelineDesc& setPatchControlPoints(u32 value){ patchControlPoints = value; return *this; }
    GraphicsPipelineDesc& setInputLayout(const InputLayoutHandle& value);
    GraphicsPipelineDesc& setVertexShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setHullShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setTessellationControlShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setDomainShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setTessellationEvaluationShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setGeometryShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setPixelShader(const ShaderHandle& value);
    GraphicsPipelineDesc& setFragmentShader(const ShaderHandle& value);
    constexpr GraphicsPipelineDesc& setRenderState(const RenderState& value){ renderState = value; return *this; }
    constexpr GraphicsPipelineDesc& setVariableRateShadingState(const VariableRateShadingState& value){ shadingRateState = value; return *this; }
    GraphicsPipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<GraphicsPipeline> GraphicsPipelineHandle;

struct ComputePipelineDesc{
    ShaderHandle CS;

    BindingLayoutVector bindingLayouts;

    ~ComputePipelineDesc();

    ComputePipelineDesc& setComputeShader(const ShaderHandle& value);
    ComputePipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<ComputePipeline> ComputePipelineHandle;

struct MeshletPipelineDesc{
    PrimitiveType::Enum primType = PrimitiveType::TriangleList;

    ShaderHandle AS;
    ShaderHandle MS;
    ShaderHandle PS;

    RenderState renderState;

    BindingLayoutVector bindingLayouts;

    ~MeshletPipelineDesc();

    constexpr MeshletPipelineDesc& setPrimType(PrimitiveType::Enum value){ primType = value; return *this; }
    MeshletPipelineDesc& setTaskShader(const ShaderHandle& value);
    MeshletPipelineDesc& setAmplificationShader(const ShaderHandle& value);
    MeshletPipelineDesc& setMeshShader(const ShaderHandle& value);
    MeshletPipelineDesc& setPixelShader(const ShaderHandle& value);
    MeshletPipelineDesc& setFragmentShader(const ShaderHandle& value);
    constexpr MeshletPipelineDesc& setRenderState(const RenderState& value){ renderState = value; return *this; }
    MeshletPipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<MeshletPipeline> MeshletPipelineHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draw and Dispatch


typedef GraphicsBackend::Handle<EventQuery> EventQueryHandle;
typedef GraphicsBackend::Handle<TimerQuery> TimerQueryHandle;

struct VertexBufferBinding{
    Buffer* buffer = nullptr;
    u64 offset = 0;
    u32 slot = 0;

    constexpr VertexBufferBinding& setBuffer(Buffer* value){ buffer = value; return *this; }
    constexpr VertexBufferBinding& setSlot(u32 value){ slot = value; return *this; }
    constexpr VertexBufferBinding& setOffset(u64 value){ offset = value; return *this; }
};
inline bool operator==(const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{
    return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.slot == rhs.slot;
}
inline bool operator!=(const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{ return !(lhs == rhs); }

struct IndexBufferBinding{
    Buffer* buffer = nullptr;
    u32 offset = 0;
    Format::Enum format = Format::UNKNOWN;

    constexpr IndexBufferBinding& setBuffer(Buffer* value){ buffer = value; return *this; }
    constexpr IndexBufferBinding& setFormat(Format::Enum value){ format = value; return *this; }
    constexpr IndexBufferBinding& setOffset(u32 value){ offset = value; return *this; }
};
inline bool operator==(const IndexBufferBinding& lhs, const IndexBufferBinding& rhs)noexcept{
    return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.format == rhs.format;
}
inline bool operator!=(const IndexBufferBinding& lhs, const IndexBufferBinding& rhs)noexcept{ return !(lhs == rhs); }

typedef FixedVector<BindingSet*, s_MaxBindingLayouts> BindingSetVector;

struct GraphicsState{
    GraphicsPipeline* pipeline = nullptr;
    Framebuffer* framebuffer = nullptr;
    ViewportState viewport;
    VariableRateShadingState shadingRateState;
    Color blendConstantColor{};
    u8 dynamicStencilRefValue = 0;

    BindingSetVector bindings;

    FixedVector<VertexBufferBinding, s_MaxVertexAttributes> vertexBuffers;
    IndexBufferBinding indexBuffer;

    Buffer* indirectParams = nullptr;

    constexpr GraphicsState& setPipeline(GraphicsPipeline* value){ pipeline = value; return *this; }
    constexpr GraphicsState& setFramebuffer(Framebuffer* value){ framebuffer = value; return *this; }
    constexpr GraphicsState& setViewport(const ViewportState& value){ viewport = value; return *this; }
    constexpr GraphicsState& setShadingRateState(const VariableRateShadingState& value){ shadingRateState = value; return *this; }
    constexpr GraphicsState& setBlendColor(const Color& value){ blendConstantColor = value; return *this; }
    constexpr GraphicsState& setDynamicStencilRefValue(u8 value){ dynamicStencilRefValue = value; return *this; }
    GraphicsState& addBindingSet(BindingSet* value){ bindings.push_back(value); return *this; }
    GraphicsState& addVertexBuffer(const VertexBufferBinding& value){ vertexBuffers.push_back(value); return *this; }
    constexpr GraphicsState& setIndexBuffer(const IndexBufferBinding& value){ indexBuffer = value; return *this; }
    constexpr GraphicsState& setIndirectParams(Buffer* value){ indirectParams = value; return *this; }
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
    ComputePipeline* pipeline = nullptr;

    BindingSetVector bindings;

    Buffer* indirectParams = nullptr;

    constexpr ComputeState& setPipeline(ComputePipeline* value){ pipeline = value; return *this; }
    ComputeState& addBindingSet(BindingSet* value){ bindings.push_back(value); return *this; }
    constexpr ComputeState& setIndirectParams(Buffer* value){ indirectParams = value; return *this; }
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
    MeshletPipeline* pipeline = nullptr;
    Framebuffer* framebuffer = nullptr;
    ViewportState viewport;
    Color blendConstantColor{};
    u8 dynamicStencilRefValue = 0;

    BindingSetVector bindings;

    Buffer* indirectParams = nullptr;

    constexpr MeshletState& setPipeline(MeshletPipeline* value){ pipeline = value; return *this; }
    constexpr MeshletState& setFramebuffer(Framebuffer* value){ framebuffer = value; return *this; }
    constexpr MeshletState& setViewport(const ViewportState& value){ viewport = value; return *this; }
    constexpr MeshletState& setBlendColor(const Color& value){ blendConstantColor = value; return *this; }
    MeshletState& addBindingSet(BindingSet* value){ bindings.push_back(value); return *this; }
    constexpr MeshletState& setIndirectParams(Buffer* value){ indirectParams = value; return *this; }
    constexpr MeshletState& setDynamicStencilRefValue(u8 value){ dynamicStencilRefValue = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing


struct RayTracingPipelineShaderDesc{
    ShaderHandle shader;
    BindingLayoutHandle bindingLayout;
    GraphicsString exportName;

    explicit RayTracingPipelineShaderDesc(GraphicsArena& arena);
    ~RayTracingPipelineShaderDesc();

    RayTracingPipelineShaderDesc& setShader(const ShaderHandle& value);
    RayTracingPipelineShaderDesc& setBindingLayout(const BindingLayoutHandle& value);
    RayTracingPipelineShaderDesc& setExportName(AStringView value){ exportName.assign(value); return *this; }
};

struct RayTracingPipelineHitGroupDesc{
    ShaderHandle closestHitShader;
    ShaderHandle anyHitShader;
    ShaderHandle intersectionShader;
    BindingLayoutHandle bindingLayout;
    GraphicsString exportName;
    bool isProceduralPrimitive = false;

    explicit RayTracingPipelineHitGroupDesc(GraphicsArena& arena);
    ~RayTracingPipelineHitGroupDesc();

    RayTracingPipelineHitGroupDesc& setClosestHitShader(const ShaderHandle& value);
    RayTracingPipelineHitGroupDesc& setAnyHitShader(const ShaderHandle& value);
    RayTracingPipelineHitGroupDesc& setIntersectionShader(const ShaderHandle& value);
    RayTracingPipelineHitGroupDesc& setBindingLayout(const BindingLayoutHandle& value);
    RayTracingPipelineHitGroupDesc& setExportName(AStringView value){ exportName.assign(value); return *this; }
    constexpr RayTracingPipelineHitGroupDesc& setIsProceduralPrimitive(bool value){ isProceduralPrimitive = value; return *this; }
};

struct RayTracingPipelineDesc{
    GraphicsVector<RayTracingPipelineShaderDesc> shaders;
    GraphicsVector<RayTracingPipelineHitGroupDesc> hitGroups;
    BindingLayoutVector globalBindingLayouts;
    u32 maxPayloadSize = 0;
    u32 maxAttributeSize = sizeof(f32) * 2; // typical case: float2 uv;
    u32 maxRecursionDepth = 1;
    i32 hlslExtensionsUAV = -1;
    bool allowOpacityMicromaps = false;
    bool allowSpheres = false;
    bool allowLinearSweptSpheres = false;

    explicit RayTracingPipelineDesc(GraphicsArena& arena)
        : shaders(arena)
        , hitGroups(arena)
    {}
    ~RayTracingPipelineDesc();

    RayTracingPipelineDesc& addShader(const RayTracingPipelineShaderDesc& value);
    RayTracingPipelineDesc& addHitGroup(const RayTracingPipelineHitGroupDesc& value);
    RayTracingPipelineDesc& addBindingLayout(const BindingLayoutHandle& value);
    constexpr RayTracingPipelineDesc& setMaxPayloadSize(u32 value){ maxPayloadSize = value; return *this; }
    constexpr RayTracingPipelineDesc& setMaxAttributeSize(u32 value){ maxAttributeSize = value; return *this; }
    constexpr RayTracingPipelineDesc& setMaxRecursionDepth(u32 value){ maxRecursionDepth = value; return *this; }
    constexpr RayTracingPipelineDesc& setHlslExtensionsUAV(i32 value){ hlslExtensionsUAV = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowOpacityMicromaps(bool value){ allowOpacityMicromaps = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowSpheres(bool value){ allowSpheres = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowLinearSweptSpheres(bool value){ allowLinearSweptSpheres = value; return *this; }
};

typedef GraphicsBackend::Handle<RayTracingShaderTable> RayTracingShaderTableHandle;
typedef GraphicsBackend::Handle<RayTracingPipeline> RayTracingPipelineHandle;

struct RayTracingState{
    RayTracingShaderTable* shaderTable = nullptr;

    BindingSetVector bindings;

    constexpr RayTracingState& setShaderTable(RayTracingShaderTable* value){ shaderTable = value; return *this; }
    RayTracingState& addBindingSet(BindingSet* value){ bindings.push_back(value); return *this; }
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
// Maps from VkCooperativeVectorPropertiesNV.
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
    GraphicsVector<CooperativeVectorMatMulFormatCombo> matMulFormats;

    // True if cooperativeVectorTrainingFloat16Accumulation is supported.
    bool trainingFloat16 = false;

    // True if cooperativeVectorTrainingFloat32Accumulation is supported.
    bool trainingFloat32 = false;

    explicit CooperativeVectorDeviceFeatures(GraphicsArena& arena)
        : matMulFormats(arena)
    {}
};

struct CooperativeVectorMatrixLayoutDesc{
    // Buffer where the matrix is stored.
    Buffer* buffer = nullptr;

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
// Used by CommandList::convertCoopVecMatrices(...)
struct CooperativeVectorConvertMatrixLayoutDesc{
    CooperativeVectorMatrixLayoutDesc src;
    CooperativeVectorMatrixLayoutDesc dst;

    u32 numRows = 0;
    u32 numColumns = 0;
};

// Returns the size in bytes of a given data type.
usize GetCooperativeVectorDataTypeSize(CooperativeVectorDataType::Enum type);

// Returns the stride for a given matrix if it's stored in a RowMajor or ColumnMajor layout.
// For other layouts, returns 0.
usize GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, u32 rows, u32 columns);


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

        kCount
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

struct CommandListParameters{
    // Type of the queue that this command list is to be executed on.
    // COPY and COMPUTE queues have limited subsets of methods available.
    CommandQueue::Enum queueType = CommandQueue::Graphics;

    CommandListParameters& setQueueType(CommandQueue::Enum value){ queueType = value; return *this; }
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List

// Represents a sequence of GPU operations submitted through a backend queue.
typedef GraphicsBackend::Handle<CommandList> CommandListHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Aftermath


typedef Pair<bool, GraphicsTString> ResolvedMarker;
typedef Pair<const void*, usize> BinaryBlob;
typedef Function<u64(BinaryBlob, GraphicsAPI::Enum)> ShaderHashGeneratorFunction;
typedef Function<BinaryBlob(u64, ShaderHashGeneratorFunction)> ShaderBinaryLookupCallback;

// Aftermath will return the payload of the last marker the GPU executed.
// In cases of nested regimes, we want the marker payloads to represent the whole "stack" of regimes.
// AftermathMarkerTracker pushes/pops regimes to this stack.
// The payload itself is a 64bit value, so AftermathMarkerTracker stores the mappings of strings<->hashes.
// There should be one AftermathMarkerTracker per graphics API-level command list.
class AftermathMarkerTracker{
public:
    explicit AftermathMarkerTracker(GraphicsArena& arena);


public:
    usize pushEvent(const char* name);
    void popEvent(){ m_eventStack = m_eventStack.parent_path(); }
    Pair<bool, GraphicsString> getEventString(usize hash);


private:
    // Using a filesystem path to track the event stack since that automatically inserts "/" separators
    GraphicsArena& m_arena;
    Path m_eventStack;

    Array<usize, s_MaxAftermathEventStrings> m_eventHashes;
    usize m_oldestHashIndex;
    GraphicsHashMap<usize, GraphicsString> m_eventStrings;
};

// AftermathCrashDumpHelper tracks all Device-level constructs needed when generating a crash dump.
// It provides two services: resolving a marker hash to the original string, and getting shader bytecode.
// There should be one AftermathCrashDumpHelper per Device.
// All command lists will register their AftermathMarkerTrackers with the AftermathCrashDumpHelper.
class AftermathCrashDumpHelper{
public:
    explicit AftermathCrashDumpHelper(GraphicsArena& arena);


public:
    void registerAftermathMarkerTracker(AftermathMarkerTracker& tracker);
    void unRegisterAftermathMarkerTracker(AftermathMarkerTracker& tracker);
    void registerShaderBinaryLookupCallback(void* client, ShaderBinaryLookupCallback lookupCallback);
    void unRegisterShaderBinaryLookupCallback(void* client);

    Pair<bool, GraphicsString> resolveMarker(usize markerHash);
    BinaryBlob findShaderBinary(u64 shaderHash, ShaderHashGeneratorFunction hashGenerator);


private:
    GraphicsArena& m_arena;
    GraphicsSet<AftermathMarkerTracker*> m_markerTrackers;
    // Command lists deleted on CPU could still be executing (and crashing) on GPU,
    // so keep a small number of recently destroyed marker trackers
    GraphicsDeque<AftermathMarkerTracker> m_destroyedMarkerTrackers;
    GraphicsHashMap<void*, ShaderBinaryLookupCallback> m_shaderBinaryLookupCallbacks;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device


typedef GraphicsBackend::Handle<Device> DeviceHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adapter info


struct AdapterInfo{
    typedef Array<u8, 16> UUID;
    typedef Array<u8, 8> LUID;

    GraphicsString name;
    u32 vendorID = 0;
    u32 deviceID = 0;
    u64 dedicatedVideoMemory = 0;

    UUID uuid = {};
    bool hasUUID = false;
    LUID luid = {};
    bool hasLUID = false;

    explicit AdapterInfo(GraphicsArena& arena)
        : name(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instance and device creation parameters


struct InstanceParameters{
    bool enableDebugRuntime = false;
    bool enableWarningsAsErrors = false;
    bool headlessDevice = false;
    bool enableAftermath = false;
    bool logBufferLifetime = false;
    bool enablePerMonitorDPI = false;

    GraphicsString backendLibraryName;
    GraphicsVector<GraphicsString> requiredBackendInstanceExtensions;
    GraphicsVector<GraphicsString> requiredBackendLayers;
    GraphicsVector<GraphicsString> optionalBackendInstanceExtensions;
    GraphicsVector<GraphicsString> optionalBackendLayers;

    explicit InstanceParameters(GraphicsArena& arena)
        : backendLibraryName(arena)
        , requiredBackendInstanceExtensions(arena)
        , requiredBackendLayers(arena)
        , optionalBackendInstanceExtensions(arena)
        , optionalBackendLayers(arena)
    {}
};

struct DeviceCreationParameters : public InstanceParameters{
    bool startMaximized = false;
    bool startFullscreen = false;
    bool startBorderless = false;
    bool allowModeSwitch = false;
    i32 windowPosX = s_WindowPositionAuto;
    i32 windowPosY = s_WindowPositionAuto;
    u32 refreshRate = 0;
    u32 swapChainBufferCount = s_SwapChainBufferCount;
    Format::Enum swapChainFormat = Format::RGBA8_UNORM_SRGB;
    u32 swapChainSampleCount = 1;
    u32 swapChainSampleQuality = 0;
    u32 maxFramesInFlight = s_MaxFramesInFlight;
    bool enableNvrhiValidationLayer = false;
    bool enableRayTracingExtensions = false;
    bool enableComputeQueue = false;
    bool enableCopyQueue = false;
    i32 adapterIndex = -1;
    bool supportExplicitDisplayScaling = false;
    bool resizeWindowWithDisplayScale = false;

    GraphicsVector<GraphicsString> requiredBackendDeviceExtensions;
    GraphicsVector<GraphicsString> optionalBackendDeviceExtensions;
    GraphicsVector<usize> ignoredValidationMessageLocations;

    Path pipelineCacheDirectory;

    explicit DeviceCreationParameters(GraphicsArena& arena)
        : InstanceParameters(arena)
        , requiredBackendDeviceExtensions(arena)
        , optionalBackendDeviceExtensions(arena)
        , ignoredValidationMessageLocations(arena)
        , pipelineCacheDirectory(arena)
    {}
};

struct SwapChainRuntimeState{
    u32 backBufferWidth = s_BackBufferWidth;
    u32 backBufferHeight = s_BackBufferHeight;
    Format::Enum backBufferFormat = Format::RGBA8_UNORM_SRGB;
    bool vsyncEnabled = false;
};

struct BackBufferResizeCallbacks{
    void* userData = nullptr;
    void (*beforeResize)(void*) = nullptr;
    void (*afterResize)(void*) = nullptr;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_DEFINE_GRAPHICS_MASK_OPERATORS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct hash<RefCountPtr<T>>{
    size_t operator()(RefCountPtr<T> const& s)const noexcept{
        hash<T*> pointerHash;
        return pointerHash(s.Get());
    }
};

template<>
struct hash<NWB::Core::TextureSubresourceSet>{
    size_t operator()(NWB::Core::TextureSubresourceSet const& s)const noexcept{
        usize hash = 0;
        NWB::Core::CoreDetail::HashCombine(hash, s.baseMipLevel);
        NWB::Core::CoreDetail::HashCombine(hash, s.numMipLevels);
        NWB::Core::CoreDetail::HashCombine(hash, s.baseArraySlice);
        NWB::Core::CoreDetail::HashCombine(hash, s.numArraySlices);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::BufferRange>{
    size_t operator()(NWB::Core::BufferRange const& s)const noexcept{
        usize hash = 0;
        NWB::Core::CoreDetail::HashCombine(hash, s.byteOffset);
        NWB::Core::CoreDetail::HashCombine(hash, s.byteSize);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::BindingSetItem>{
    size_t operator()(NWB::Core::BindingSetItem const& s)const noexcept{
        usize value = 0;
        NWB::Core::CoreDetail::HashCombine(value, s.resourceHandle);
        NWB::Core::CoreDetail::HashCombine(value, s.slot);
        NWB::Core::CoreDetail::HashCombine(value, s.arrayElement);
        NWB::Core::CoreDetail::HashCombine(value, s.type);
        NWB::Core::CoreDetail::HashCombine(value, s.dimension);
        NWB::Core::CoreDetail::HashCombine(value, s.format);
        NWB::Core::CoreDetail::HashCombine(value, s.rawData[0]);
        NWB::Core::CoreDetail::HashCombine(value, s.rawData[1]);
        return static_cast<size_t>(value);
    }
};

template<>
struct hash<NWB::Core::BindingSetDesc>{
    size_t operator()(NWB::Core::BindingSetDesc const& s)const noexcept{
        usize value = 0;
        NWB::Core::CoreDetail::HashCombine(value, s.trackLiveness);
        for(const auto& item : s.bindings)
            NWB::Core::CoreDetail::HashCombine(value, item);
        return static_cast<size_t>(value);
    }
};

template<>
struct hash<NWB::Core::FramebufferInfo>{
    size_t operator()(NWB::Core::FramebufferInfo const& s)const noexcept{
        usize hash = 0;
        for(const auto format : s.colorFormats)
            NWB::Core::CoreDetail::HashCombine(hash, format);
        NWB::Core::CoreDetail::HashCombine(hash, s.depthFormat);
        NWB::Core::CoreDetail::HashCombine(hash, s.sampleCount);
        NWB::Core::CoreDetail::HashCombine(hash, s.sampleQuality);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::BlendState::RenderTarget>{
    size_t operator()(NWB::Core::BlendState::RenderTarget const& s)const noexcept{
        usize hash = 0;
        NWB::Core::CoreDetail::HashCombine(hash, s.blendEnable);
        NWB::Core::CoreDetail::HashCombine(hash, s.srcBlend);
        NWB::Core::CoreDetail::HashCombine(hash, s.destBlend);
        NWB::Core::CoreDetail::HashCombine(hash, s.blendOp);
        NWB::Core::CoreDetail::HashCombine(hash, s.srcBlendAlpha);
        NWB::Core::CoreDetail::HashCombine(hash, s.destBlendAlpha);
        NWB::Core::CoreDetail::HashCombine(hash, s.blendOpAlpha);
        NWB::Core::CoreDetail::HashCombine(hash, s.colorWriteMask);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::BlendState>{
    size_t operator()(NWB::Core::BlendState const& s)const noexcept{
        usize hash = 0;
        NWB::Core::CoreDetail::HashCombine(hash, s.alphaToCoverageEnable);
        for(const auto& target : s.targets)
            NWB::Core::CoreDetail::HashCombine(hash, target);
        return static_cast<size_t>(hash);
    }
};

template<>
struct hash<NWB::Core::VariableRateShadingState>{
    size_t operator()(NWB::Core::VariableRateShadingState const& s)const noexcept{
        usize hash = 0;
        NWB::Core::CoreDetail::HashCombine(hash, s.enabled);
        NWB::Core::CoreDetail::HashCombine(hash, s.shadingRate);
        NWB::Core::CoreDetail::HashCombine(hash, s.pipelinePrimitiveCombiner);
        NWB::Core::CoreDetail::HashCombine(hash, s.imageCombiner);
        return static_cast<size_t>(hash);
    }
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

