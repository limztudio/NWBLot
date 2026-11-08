// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "resource.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderType{
    static constexpr u8 s_ShaderTypeVertexStageBase = NWB::Core::HardwareRayTracingPolicy::s_ShaderTypeVertexStageBase;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


    enum Enum : u8{
        VertexStage = s_ShaderTypeVertexStageBase,
        HullStage,
        DomainStage,
        GeometryStage,
        PixelStage,
        ComputeStage,
        AmplificationStage,
        MeshStage,
        RayGenerationStage,
        AnyHitStage,
        ClosestHitStage,
        MissStage,
        IntersectionStage,
        CallableStage,

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

    [[nodiscard]] inline constexpr b8 IsValid(const Enum shaderType)noexcept{
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

struct ShaderDesc{
    static constexpr AStringView s_DefaultEntryName = NWB::Core::HardwareRayTracingPolicy::s_ShaderDefaultEntryName;

    Name debugName;
    // Borrowed until shader creation completes; the created shader rebinds this to its own retained bytecode.
    AStringView entryName = s_DefaultEntryName;
    ShaderType::Mask shaderType = ShaderType::None;


public:
    constexpr ShaderDesc& setShaderType(ShaderType::Mask value)noexcept{ shaderType = value; return *this; }

    constexpr ShaderDesc& setDebugName(const Name& value)noexcept{ debugName = value; return *this; }

    constexpr ShaderDesc& setEntryName(const AStringView value)noexcept{ entryName = value; return *this; }
};

struct ShaderSpecialization{
    u32 constantID = 0;
    union{
        u32 u = 0;
        i32 i;
        f32 f;
    } value;
};

typedef GraphicsBackend::Handle<Shader> ShaderHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef GraphicsBackend::Handle<ShaderLibrary> ShaderLibraryHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

