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


struct ViewportState{
    u32 numViewports = 0;
    u32 numScissors = 0;

    Viewport* viewports = nullptr;
    Rect2DInt* scissors = nullptr;
};

struct StencilOperationState{
    VkStencilOp failOp = VK_STENCIL_OP_KEEP;
    VkStencilOp passOp = VK_STENCIL_OP_KEEP;
    VkStencilOp depthFailOp = VK_STENCIL_OP_KEEP;
    VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS;

    u32 compareMask = 0xff;
    u32 writeMask = 0xff;
    u32 reference = 0xff;
};

struct BlendState{
    VkBlendFactor srcColorFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstColorFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorOp = VK_BLEND_OP_ADD;

    VkBlendFactor srcAlphaFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaOp = VK_BLEND_OP_ADD;

    ColorWrite::Mask colorWriteMask = ColorWrite::Mask::MASK_ALL;

    u8 blendEnabled : 1;
    u8 separateBlend : 1;
    u8 reserved : 6;


    BlendState() : blendEnabled(0), separateBlend(0) {}

    BlendState& setColor(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op){
        srcColorFactor = src;
        dstColorFactor = dst;
        colorOp = op;
        blendEnabled = 1;
        return *this;
    }
    BlendState& setAlpha(VkBlendFactor src, VkBlendFactor dst, VkBlendOp op){
        srcAlphaFactor = src;
        dstAlphaFactor = dst;
        alphaOp = op;
        blendEnabled = 1;
        return *this;
    }
    BlendState& setColorWriteMask(ColorWrite::Mask mask){
        colorWriteMask = mask;
        return *this;
    }
};

struct ShaderStage{
    const char* code = nullptr;
    u32 codeSize = 0;
    VkShaderStageFlagBits type = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
};

struct DescriptorSet; // just phantom

struct DescriptorSetUpdate{
    Alloc::AssetHandle<DescriptorSet> descriptorSet;
    u32 frameIssued = 0;
};

struct VertexAttribute{
    u16 location = 0;
    u16 binding = 0;
    u32 offset = 0;
    VertexComponentType::Enum format = VertexComponentType::kCount;
};

struct VertexStream{
    u16 binding = 0;
    u16 stride = 0;
    VertexInputRate::Enum inputRate = VertexInputRate::kCount;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DepthStencilStateCreation{
    StencilOperationState front;
    StencilOperationState back;
    VkCompareOp depthComarison = VK_COMPARE_OP_ALWAYS;

    u8 depthEnabled : 1;
    u8 depthWriteEnabled : 1;
    u8 stencilEnabled : 1;
    u8 reserved : 5;


    DepthStencilStateCreation() : depthEnabled(0), depthWriteEnabled(0), stencilEnabled(0) {}

    DepthStencilStateCreation& setDepth(bool write, VkCompareOp comparisonTest){
        depthWriteEnabled = write ? 1 : 0;
        depthComarison = comparisonTest;
        depthEnabled = 1;
        return *this;
    }
};

struct BlendStateCreation{
    BlendState blendStates[s_maxImageOutputs];
    u32 activeStates = 0;


    BlendStateCreation& reset(){
        activeStates = 0;
        return *this;
    }
    BlendState& addBlendState(){
        NWB_ASSERT(activeStates < LengthOf(blendStates));
        return blendStates[activeStates++];
    }
};

struct RasterizationCreation{
    VkCullModeFlagBits cullMode = VK_CULL_MODE_NONE;
    VkFrontFace front = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    FillMode::Enum fill = FillMode::SOLID;
};

struct BufferCreation{

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

