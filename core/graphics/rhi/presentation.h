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

struct PresentationFailure{
    bool presentationAccepted = false;
};

namespace PresentationReceiptStatus{
    enum Enum : u8{ Pending, Accepted, Rejected };
};

// Main-thread observation of one native present attempt. The receipt retains identity, never image resources.
struct PresentationReceipt{
    QueueSubmissionToken availabilityCompletion;
    ResourceStates::Mask nativeInitialState = ResourceStates::Unknown;
    u32 index = Limit<u32>::s_Max;
    bool accepted = false;

    [[nodiscard]] bool valid()const noexcept{
        return
            availabilityCompletion.valid() && availabilityCompletion.hasPhysicalQueueIdentity()
            && index != Limit<u32>::s_Max
            && (nativeInitialState == ResourceStates::Unknown || nativeInitialState == ResourceStates::Present)
        ;
    }

    void record(const AcquiredBackBuffer& acquired, const bool presentationAccepted)noexcept{
        availabilityCompletion = acquired.availabilityCompletion;
        nativeInitialState = acquired.nativeInitialState;
        index = acquired.index;
        accepted = presentationAccepted;
        if(!valid())
            reset();
    }

    // Pending means no exact attempt is known, including invalid or different acquisition identities.
    [[nodiscard]] PresentationReceiptStatus::Enum status(const AcquiredBackBuffer& acquired)const noexcept{
        const QueueSubmissionToken& token = acquired.availabilityCompletion;
        if(!valid() || !token.valid() || !token.hasPhysicalQueueIdentity())
            return PresentationReceiptStatus::Pending;
        if(
            availabilityCompletion.value != token.value || availabilityCompletion.queue != token.queue
            || availabilityCompletion.physicalQueueIndex != token.physicalQueueIndex
            || availabilityCompletion.deviceGeneration != token.deviceGeneration
            || index != acquired.index || nativeInitialState != acquired.nativeInitialState
        )
            return PresentationReceiptStatus::Pending;
        return accepted ? PresentationReceiptStatus::Accepted : PresentationReceiptStatus::Rejected;
    }

    void reset()noexcept{ *this = {}; }
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
    u32 suggestedWidth = 0u;
    u32 suggestedHeight = 0u;
    BeginFrameStatus::Enum status = BeginFrameStatus::Failed;

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

