// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "command.h"
#include "framebuffer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AcquiredBackBuffer{
    TextureHandle texture;
    QueueSubmissionToken availabilityCompletion;
    ResourceStates::Mask nativeInitialState = ResourceStates::Unknown;
    u32 index = Limit<u32>::s_Max;

    [[nodiscard]] bool valid()const noexcept{
        return texture
            && availabilityCompletion.valid()
            && availabilityCompletion.hasPhysicalQueueIdentity()
            && index != Limit<u32>::s_Max
            && (nativeInitialState == ResourceStates::Unknown || nativeInitialState == ResourceStates::Present)
        ;
    }
};

struct AcquiredPresentationFrame{
    AcquiredBackBuffer backBuffer;
    FramebufferHandle framebuffer;

    [[nodiscard]] bool valid()const noexcept{ return backBuffer.valid() && framebuffer; }
};

namespace BeginFrameStatus{
    enum Enum : u8{
        Acquired,
        ResizeRequired,
        Failed,

        kCount
    };
};

struct BeginFrameResult{
    AcquiredBackBuffer backBuffer;
    BeginFrameStatus::Enum status = BeginFrameStatus::Failed;
    u32 suggestedWidth = 0u;
    u32 suggestedHeight = 0u;

    [[nodiscard]] bool acquired()const noexcept{
        return status == BeginFrameStatus::Acquired && backBuffer.valid();
    }
};

namespace SwapChainTransitionKind{
    enum Enum : u8{
        Resize,
        Destroy,

        kCount
    };
};

struct SwapChainTransitionTicket{
    const void* owner = nullptr;
    u64 epoch = 0u;
    SwapChainTransitionKind::Enum kind = SwapChainTransitionKind::kCount;

    [[nodiscard]] bool valid()const noexcept{
        return owner != nullptr && epoch != 0u && kind < SwapChainTransitionKind::kCount;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

