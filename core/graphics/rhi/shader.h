// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "resource.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderType{
    enum Enum : u8{
        VertexStage = 0,
        PixelStage = 4,
        ComputeStage = 5,
        MeshStage = 7,
        RayGenerationStage = 8,
        ClosestHitStage = 10,
        MissStage = 11,

        Count = 12,
        Invalid = Count,
    };
    enum Mask : u16{
        None            = 0x0000,

        Compute         = 0x0020,

        Vertex          = 0x0001,
        Pixel           = 0x0010,
        Mesh            = 0x0080,
        AllGraphics     = Vertex | Pixel | Mesh,

        RayGeneration   = 0x0100,
        ClosestHit      = 0x0400,
        Miss            = 0x0800,
        AllRayTracing   = RayGeneration | ClosestHit | Miss,

        All             = AllGraphics | Compute | AllRayTracing,
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
            case PixelStage: return Pixel;
            case ComputeStage: return Compute;
            case MeshStage: return Mesh;
            case RayGenerationStage: return RayGeneration;
            case ClosestHitStage: return ClosestHit;
            case MissStage: return Miss;
            default: return None;
        }
    }

    [[nodiscard]] inline constexpr Enum ToEnum(const Mask shaderType)noexcept{
        switch(shaderType){
            case Vertex: return VertexStage;
            case Pixel: return PixelStage;
            case Compute: return ComputeStage;
            case Mesh: return MeshStage;
            case RayGeneration: return RayGenerationStage;
            case ClosestHit: return ClosestHitStage;
            case Miss: return MissStage;
            default: return Invalid;
        }
    }
};

struct ShaderDesc{
    Name debugName;
    GraphicsString entryName;

    ShaderType::Mask shaderType = ShaderType::None;

    explicit ShaderDesc(GraphicsArena& arena)
        : entryName("main", arena)
    {}

    constexpr ShaderDesc& setShaderType(ShaderType::Mask value){ shaderType = value; return *this; }
    constexpr ShaderDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    ShaderDesc& setEntryName(const AStringView value){ entryName.assign(value); return *this; }
};

typedef GraphicsBackend::Handle<Shader> ShaderHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

