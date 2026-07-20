#pragma once


#include "resource.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    Name debugName;
    GraphicsString entryName;
    CustomSemantic* pCustomSemantics = nullptr;
    u32* pCoordinateSwizzling = nullptr;

    i32 hlslExtensionsUAV = -1;
    u32 numCustomSemantics = 0;

    ShaderType::Mask shaderType = ShaderType::None;
    FastGeometryShaderFlags::Mask fastGSFlags = FastGeometryShaderFlags::None;
    bool useSpecificShaderExt = false;

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


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

