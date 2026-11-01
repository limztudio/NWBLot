// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/reflection/types.adhd.h>

#include <core/graphics/rhi/command.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ReflectionStatisticsTestHelpers{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline NWB::Impl::ReflectionSceneContentStamp Stamp()noexcept{
    return NWB::Impl::ReflectionSceneContentStamp{ 1u, 2u, 3u, 4u, true };
}

template<u16 DefaultDeviceGeneration = 1u, u64 DefaultValue = 1u>
[[nodiscard]] inline NWB::Core::QueueSubmissionToken Token(const u64 value = DefaultValue)noexcept{
    return NWB::Core::QueueSubmissionToken{ .value = value, .physicalQueueIndex = 0u, .deviceGeneration = DefaultDeviceGeneration, .queue = NWB::Core::CommandQueue::Graphics, };
}

template<typename StateT, typename PlanT>
inline void Accept(const PlanT& plan, StateT& state, const b8 hardwareReady = true){
    ASSERT_TRUE(state.reserve(plan));
    if constexpr(IsSame_V<decltype(state.accept(plan, Token(plan.sequence), hardwareReady)), b8>)
        ASSERT_TRUE(state.accept(plan, Token(plan.sequence), hardwareReady));
    else
        state.accept(plan, Token(plan.sequence), hardwareReady);
}

template<typename ReservationT>
struct ReservationLeaseTask{
    struct Payload{
        ReservationT reservation;
    };


public:
    static void Discarded(Payload& payload)noexcept{ payload.reservation.discard(); }
};

template<typename ReservationT>
struct DestructorReservationLeaseTask{
    struct Payload{
        ReservationT reservation;
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

