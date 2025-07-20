// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "../common.h"
#include "config.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ColorWrite{
    enum Mask : u8{
        MASK_R = 0x1,
        MASK_G = 0x2,
        MASK_B = 0x4,
        MASK_A = 0x8,

        MASK_ALL = MASK_R | MASK_G | MASK_B | MASK_A,
    };
};

namespace CullMode{
    enum Mask : u8{
        MASK_NONE = 0x0,
        MASK_FRONT = 0x1,
        MASK_BACK = 0x2,
    };
};

namespace ClockWise{
    enum Mask : u8{
        MASK_CW = 0x1,
        MASK_ACW = 0x2,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

