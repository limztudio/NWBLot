// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/graphics/rhi/presentation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BackendContext;
class FramePresentationCoordinator;

struct SwapChainLifecycleState{
    enum Enum : u8{
        Ready,
        RetiringPresentation,
        Preparing,
        PreparedResize,
        PreparedDestroy,
        NeedsDestroy,
        Destroyed,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SwapChainLifecycle final : NoCopy{
    friend class BackendContext;
    friend class FramePresentationCoordinator;


public:
    explicit SwapChainLifecycle(BackendContext& context)noexcept
        : m_context(context)
    {}


public:
    [[nodiscard]] b8 createSwapChain();
    [[nodiscard]] b8 destroy();
    [[nodiscard]] Expected<SwapChainTransitionTicket> prepareSwapChainTransition(SwapChainTransitionKind::Enum kind);
    [[nodiscard]] b8 commitSwapChainResize(SwapChainTransitionTicket&& ticket);
    [[nodiscard]] b8 commitDestroy(SwapChainTransitionTicket&& ticket)noexcept;


private:
    [[nodiscard]] b8 createSwapChainResources(Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 validPreparedTicket(const SwapChainTransitionTicket& ticket, SwapChainTransitionKind::Enum kind)const noexcept;


private:
    BackendContext& m_context;
    Futex m_swapChainLifecycleMutex;
    u64 m_swapChainLifecycleEpoch = 0u;
    SwapChainLifecycleState::Enum m_swapChainLifecycleState = SwapChainLifecycleState::Ready;
    b8 m_lifecycleDrainActive = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

