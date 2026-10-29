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

inline void AtomicSignalFence(const MemoryOrder order)noexcept{ std::atomic_signal_fence(order); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AtomicFlag{
private:
    Atomic<i32> m_storage = {};


public:
    constexpr AtomicFlag()noexcept = default;


public:
    [[nodiscard]] b8 test(const MemoryOrder order = MemoryOrder::seq_cst)const noexcept{
        return m_storage.load(order) != 0;
    }

    b8 testAndSet(const MemoryOrder order = MemoryOrder::seq_cst)noexcept{
        return m_storage.exchange(true, order) != 0;
    }

    void clear(const MemoryOrder order = MemoryOrder::seq_cst)noexcept{ m_storage.store(false, order); }

    void wait(const b8 expected, const MemoryOrder order = MemoryOrder::seq_cst)const noexcept{
        m_storage.wait(static_cast<decltype(m_storage)::value_type>(expected), order);
    }

    void notifyOne()noexcept{ m_storage.notify_one(); }

    void notifyAll()noexcept{ m_storage.notify_all(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

