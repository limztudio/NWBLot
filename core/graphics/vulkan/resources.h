// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "../common.h"
#include "config.h"

#include <core/alloc/assetPool.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u8 s_maxImageOutputs = 8;
constexpr u8 s_maxShaderStages = 5;
constexpr u8 s_maxDescriptorsPerSet = 16;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ColorWrite{
    enum Enum : u8{
        R,
        G,
        B,
        A,

        kCount
    };
    
    enum Mask : u8{
        MASK_R = 0x1,
        MASK_G = 0x2,
        MASK_B = 0x4,
        MASK_A = 0x8,

        MASK_ALL = MASK_R | MASK_G | MASK_B | MASK_A,
    };
};

namespace CullMode{
    enum Enum : u8{
        NONE,
        FRONT,
        BACK,

        kCount
    };
};

namespace DepthWrite{
    enum Enum : u8{
        ZERO,
        ALL,

        kCount
    };

    enum Mask : u8{
        MASK_ZERO = 0x1,
        MASK_ALL = 0x2,
    };
};

namespace FillMode{
    enum Enum : u8{
        WIREFRAME,
        SOLID,
        POINT,

        kCount
    };
};

namespace ClockWise{
    enum Enum : u8{
        CW,
        ACW,

        kCount
    };
};

namespace StencilOperation{
    enum Enum : u8{
        KEEP,
        ZERO,
        REPLACE,
        INCRSAT,
        DECRSAT,
        INVERT,
        INCR,
        DECR,

        kCount
    };
};

namespace TopologyType{
    enum Enum : u8{
        UNKNOWN,
        POINT,
        LINE,
        TRIANGLE,
        PATCH,

        kCount
    };
};

namespace ResourceUsageType{
    enum Enum : u8{
        IMMUTABLE,
        DYNAMIC,
        STREAM,

        kCount
    };
};

namespace IndexType{
    enum Enum : u8{
        U16,
        U32,

        kCount
    };
};

namespace TextureType{
    enum Enum : u8{
        TEX1D,
        TEX2D,
        TEX3D,
        TEX1DARY,
        TEX2DARY,
        TEX2DCUBE,

        kCount
    };
};

namespace VertexComponentType{
    enum Enum : u8{
        FLOAT,
        FLOAT2,
        FLOAT3,
        FLOAT4,
        MAT4,
        BYTE,
        BYTE4N,
        UBYTE,
        UBYTE4N,
        SHORT2,
        SHORT2N,
        SHORT4,
        SHORT4N,
        UINT,
        UINT2,
        UINT4,

        kCount
    };
};

namespace VertexInputRate{
    enum Enum : u8{
        PER_VERTEX,
        PER_INSTANCE,

        kCount
    };
};

namespace LogicOperation{
    enum Enum : u8{
        CLEAR,
        SET,
        COPY,
        COPY_INVERTED,
        NO_OP,
        INVERT,
        AND,
        NAND,
        OR,
        NOR,
        XOR,
        EQUIV,
        AND_REVERSE,
        AND_INVERTED,
        OR_REVERSE,
        OR_INVERTED,

        kCount
    };
};

namespace QueueType{
    enum Enum : u8{
        GRAPHICS,
        COMPUTE,
        COPY_TRANSFER,

        kCount
    };
};

namespace CommandType{
    enum Enum : u8{
        BIND_PIPELINE,
        BIND_RESOURCE_TABLE,
        BIND_VERTEX_BUFFER,
        BIND_INDEX_BUFFER,
        BIND_RESOURCE_SET,
        DRAW,
        DRAW_INDEXED,
        DRAW_INSTANCED,
        DRAW_INDEXED_INSTANCED,
        DISPATCH,
        COPY_RESOURCE,
        SET_SCISSOR,
        SET_VIEWPORT,
        CLEAR,
        CLEAR_DEPTH,
        CLEAR_STENCIL,
        BEGIN_PASS,
        END_PASS,

        kCount
    };
};

namespace TextureFlag{
    enum Enum : u8{
        DEFAULT,
        RENDER_TARGET,
        COMPUTE,

        kCount
    };
};

namespace PipelineStage{
    enum Enum : u8{
        DRAW_INDIRECT,
        VERTEX_INPUT,
        VERTEX_SHADER,
        FRAGMENT_SHADER,
        RENDER_TARGET,
        COMPUTE_SHADER,
        TRANSFER,

        kCount
    };
};

namespace RenderPassType{
    enum Enum : u8{
        GEOMETRY,
        SWAPCHAIN,
        COMPUTE,

        kCount
    };
};

namespace ResourceDeletionType{
    enum Enum : u8{
        BUFFER,
        TEXTURE,
        PIPELINE,
        SAMPLER,
        DESCRIPTOR_SET_LAYOUT,
        DESCRIPTOR_SET,
        RENDER_PASS,
        SHADER_STATE,

        kCount
    };
};

namespace PresentMode{
    enum Enum : u8{
        IMMEDIATE,
        V_SYNC,
        V_SYNC_FAST,
        V_SYNC_RELAXED,

        kCount
    };
};

namespace RenderPassOperation{
    enum Enum : u8{
        DONT_CARE,
        LOAD,
        CLEAR,

        kCount
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Rect2D{
    f32 x = 0;
    f32 y = 0;
    f32 width = 0;
    f32 height = 0;
};

struct Rect2DInt{
    i16 x = 0;
    i16 y = 0;
    u16 width = 0;
    u16 height = 0;
};

struct Viewport{
    Rect2DInt rect;
    f32 minDepth = 0;
    f32 maxDepth = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

