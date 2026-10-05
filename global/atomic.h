// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"

#include <atomic>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MemoryOrder = std::memory_order;

template<typename T>
using Atomic = std::atomic<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AtomicThreadFence(const MemoryOrder order)noexcept{ std::atomic_thread_fence(order); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AtomicFlag{
private:
    Atomic<i32> m_storage = {};


public:
    constexpr AtomicFlag()noexcept = default;


public:
    [[nodiscard]] bool test(const MemoryOrder order = MemoryOrder::seq_cst)const noexcept{
        return m_storage.load(order) != 0;
    }

    bool testAndSet(const MemoryOrder order = MemoryOrder::seq_cst)noexcept{
        return m_storage.exchange(true, order) != 0;
    }

    void clear(const MemoryOrder order = MemoryOrder::seq_cst)noexcept{ m_storage.store(false, order); }

    void wait(const bool expected, const MemoryOrder order = MemoryOrder::seq_cst)const noexcept{
        m_storage.wait(static_cast<decltype(m_storage)::value_type>(expected), order);
    }

    void notifyOne()noexcept{ m_storage.notify_one(); }

    void notifyAll()noexcept{ m_storage.notify_all(); }
};

[[nodiscard]] inline bool AtomicFlagTest(const AtomicFlag& flag)noexcept{ return flag.test(); }
[[nodiscard]] inline bool AtomicFlagTestExplicit(const AtomicFlag& flag, const MemoryOrder order)noexcept{
    return flag.test(order);
}

inline bool AtomicFlagTestAndSet(AtomicFlag& flag)noexcept{ return flag.testAndSet(); }
inline bool AtomicFlagTestAndSetExplicit(AtomicFlag& flag, const MemoryOrder order)noexcept{
    return flag.testAndSet(order);
}
inline void AtomicFlagClear(AtomicFlag& flag)noexcept{ flag.clear(); }
inline void AtomicFlagClearExplicit(AtomicFlag& flag, const MemoryOrder order)noexcept{ flag.clear(order); }

inline void AtomicFlagWait(const AtomicFlag& flag, const bool expected)noexcept{ flag.wait(expected); }
inline void AtomicFlagWaitExplicit(const AtomicFlag& flag, const bool expected, const MemoryOrder order)noexcept{
    flag.wait(expected, order);
}
inline void AtomicFlagNotifyOne(AtomicFlag& flag)noexcept{ flag.notifyOne(); }
inline void AtomicFlagNotifyAll(AtomicFlag& flag)noexcept{ flag.notifyAll(); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

