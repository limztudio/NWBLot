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


struct AtomicFlag{
private:
    Atomic<i32> m_storage = {};


public:
    constexpr AtomicFlag()noexcept = default;


public:
    [[nodiscard]] bool test(const MemoryOrder order = std::memory_order_seq_cst)const noexcept{
        return m_storage.load(order) != 0;
    }

    bool test_and_set(const MemoryOrder order = std::memory_order_seq_cst)noexcept{
        return m_storage.exchange(true, order) != 0;
    }

    void clear(const MemoryOrder order = std::memory_order_seq_cst)noexcept{ m_storage.store(false, order); }

    void wait(const bool expected, const MemoryOrder order = std::memory_order_seq_cst)const noexcept{
        m_storage.wait(static_cast<decltype(m_storage)::value_type>(expected), order);
    }

    void notify_one()noexcept{ m_storage.notify_one(); }

    void notify_all()noexcept{ m_storage.notify_all(); }
};

[[nodiscard]] inline bool atomic_flag_test(const AtomicFlag* const flag)noexcept{ return flag->test(); }
[[nodiscard]] inline bool atomic_flag_test_explicit(const AtomicFlag* const flag, const MemoryOrder order)noexcept{
    return flag->test(order);
}

inline bool atomic_flag_test_and_set(AtomicFlag* const flag)noexcept{ return flag->test_and_set(); }
inline bool atomic_flag_test_and_set_explicit(AtomicFlag* const flag, const MemoryOrder order)noexcept{
    return flag->test_and_set(order);
}
inline void atomic_flag_clear(AtomicFlag* const flag)noexcept{ flag->clear(); }
inline void atomic_flag_clear_explicit(AtomicFlag* const flag, const MemoryOrder order)noexcept{ flag->clear(order); }

inline void atomic_flag_wait(const AtomicFlag* const flag, const bool expected)noexcept{ flag->wait(expected); }
inline void atomic_flag_wait_explicit(const AtomicFlag* const flag, const bool expected, const MemoryOrder order)noexcept{
    flag->wait(expected, order);
}
inline void atomic_flag_notify_one(AtomicFlag* const flag)noexcept{ flag->notify_one(); }
inline void atomic_flag_notify_all(AtomicFlag* const flag)noexcept{ flag->notify_all(); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

