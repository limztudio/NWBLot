// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>

#include <core/alloc/global.h>
#include <core/graphics/rhi/command.h>

#include <global/arena_object.h>
#include <global/refcount_ptr.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct StatisticsPendingReadback{
    StatisticsReservationKey key;
    Core::QueueSubmissionToken token;
};

// CPU lifecycle shared by owner and leases; completion follows GPU token completion.
template<typename StatisticsT>
class StatisticsReadbackStateBase : NoCopy{
protected:
    struct Slot{
        StatisticsT metadata;
        b8 reserved = false;
        b8 inFlight = false;
    };


public:
    static constexpr u32 s_SlotCount = 3u;


public:
    explicit StatisticsReadbackStateBase(const u16 deviceGeneration)noexcept
        : m_deviceGeneration(deviceGeneration)
    {}


public:
    // Owner joins GPU work before invalidation; stale callbacks cannot affect next epoch.
    void reset(const u16 deviceGeneration)noexcept{
        NothrowScopedLock lock(m_mutex);

        m_generation = m_generation == Limit<u64>::s_Max ? 0u : m_generation + 1u;
        m_deviceGeneration = deviceGeneration;
        m_latest = {};
        for(Slot& slot : m_slots)
            slot = {};
    }

    [[nodiscard]] StatisticsReservationKey reserve(const StatisticsT& metadata)noexcept{
        NothrowScopedLock lock(m_mutex);

        if(m_deviceGeneration == 0u || m_generation == 0u || m_nextSequence == Limit<u64>::s_Max)
            return {};
        for(u32 index = 0u; index < s_SlotCount; ++index){
            Slot& slot = m_slots[index];
            if(slot.reserved || slot.inFlight)
                continue;
            slot.metadata = metadata;
            slot.metadata.sequence = m_nextSequence++;
            slot.metadata.generation = m_generation;
            slot.metadata.acceptedToken = {};
            slot.metadata.hardwareReady = false;
            slot.reserved = true;
            return StatisticsReservationKey{ slot.metadata.sequence, m_generation, index };
        }
        return {};
    }

    void discard(const StatisticsReservationKey& key)noexcept{
        NothrowScopedLock lock(m_mutex);

        Slot* slot = matchingSlot(key);
        if(slot && slot->reserved && !slot->inFlight)
            *slot = {};
    }

    [[nodiscard]] Expected<StatisticsPendingReadback> pending(const u32 index)const noexcept{
        NothrowScopedLock lock(m_mutex);

        if(index >= s_SlotCount)
            return MakeUnexpected(Failure{});
        const Slot& slot = m_slots[index];
        const Core::QueueSubmissionToken& token = slot.metadata.acceptedToken;
        if(!slot.inFlight || !token.valid() || !token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration)
            return MakeUnexpected(Failure{});
        return StatisticsPendingReadback{ .key = { slot.metadata.sequence, slot.metadata.generation, index }, .token = token };
    }

    [[nodiscard]] Expected<StatisticsT> tryGetLatestStatistics()const noexcept{
        NothrowScopedLock lock(m_mutex);

        if(m_latest.sequence == 0u)
            return MakeUnexpected(Failure{});
        return m_latest;
    }


protected:
    [[nodiscard]] Slot* matchingSlot(const StatisticsReservationKey& key)noexcept{
        if(!key.valid() || key.generation != m_generation || key.slot >= s_SlotCount)
            return nullptr;
        Slot& slot = m_slots[key.slot];
        return slot.metadata.sequence == key.sequence && slot.metadata.generation == key.generation ? &slot : nullptr;
    }

    [[nodiscard]] Slot* beginCompleteWithinLock(const StatisticsReservationKey& key, const Core::QueueSubmissionToken& token)noexcept{
        Slot* slot = matchingSlot(key);
        if(!slot || !slot->inFlight)
            return nullptr;
        const Core::QueueSubmissionToken& accepted = slot->metadata.acceptedToken;
        if(!token.valid() || !token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration || accepted != token)
            return nullptr;
        return slot;
    }


protected:
    Slot m_slots[s_SlotCount];
    StatisticsT m_latest;
    u64 m_generation = 1u;
    u64 m_nextSequence = 1u;
    mutable Futex m_mutex;
    u16 m_deviceGeneration = 0u;
};

template<typename ControlT>
[[nodiscard]] inline Expected<RefCountPtr<ControlT, ArenaRefDeleter<ControlT, Core::Alloc::GlobalArena>>> CreateStatisticsControl(Core::Alloc::GlobalArena& arena, const u16 deviceGeneration)noexcept{
    using HandleT = RefCountPtr<ControlT, ArenaRefDeleter<ControlT, Core::Alloc::GlobalArena>>;
    auto control = HandleT(NewArenaObject<ControlT>(arena, deviceGeneration), ArenaRefDeleter<ControlT, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
    if(!control)
        return MakeUnexpected(Failure{});
    return control;
}

// Destruction releases only an unaccepted reservation.
template<typename ControlHandleT, typename StatisticsT>
class StatisticsReservationBase : NoCopy{
public:
    StatisticsReservationBase(ControlHandleT control, const StatisticsT& metadata)noexcept
        : m_control(Move(control))
    {
        if(m_control)
            m_key = m_control->reserve(metadata);
    }
    StatisticsReservationBase(StatisticsReservationBase&& other)noexcept
        : m_control(Move(other.m_control))
        , m_key(other.m_key)
    {
        other.m_key = {};
    }
    ~StatisticsReservationBase()noexcept{
        discard();
    }


public:
    StatisticsReservationBase& operator=(StatisticsReservationBase&& other)noexcept{
        if(this != &other){
            discard();
            m_control = Move(other.m_control);
            m_key = other.m_key;
            other.m_key = {};
        }
        return *this;
    }


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_key.valid(); }

    [[nodiscard]] u32 slotIndex()const noexcept{ return m_key.slot; }

    void discard()noexcept{
        if(m_control && m_key.valid())
            m_control->discard(m_key);
        m_key = {};
        m_control = nullptr;
    }


protected:
    ControlHandleT m_control;
    StatisticsReservationKey m_key;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

