// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"

#include <atomic>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MemoryOrder = std::memory_order;

template <typename T>
using Atomic = std::atomic<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AtomicFlag{
public:
    constexpr AtomicFlag()noexcept = default;


public:
    _NODISCARD bool test(const MemoryOrder _Order = MemoryOrder::memory_order_seq_cst)const noexcept{ return _Storage.load(_Order) != 0; }
    _NODISCARD bool test(const MemoryOrder _Order = MemoryOrder::memory_order_seq_cst)const volatile noexcept{ return _Storage.load(_Order) != 0; }

    bool test_and_set(const MemoryOrder _Order = MemoryOrder::memory_order_seq_cst)noexcept{ return _Storage.exchange(true, _Order) != 0; }
    bool test_and_set(const MemoryOrder _Order = MemoryOrder::memory_order_seq_cst)volatile noexcept{ return _Storage.exchange(true, _Order) != 0; }

    void clear(const MemoryOrder _Order = MemoryOrder::memory_order_seq_cst)noexcept{ _Storage.store(false, _Order); }
    void clear(const MemoryOrder _Order = MemoryOrder::memory_order_seq_cst)volatile noexcept{ _Storage.store(false, _Order); }

    void wait(const bool _Expected, const MemoryOrder _Order = MemoryOrder::memory_order_seq_cst)const noexcept{ _Storage.wait(static_cast<decltype(_Storage)::value_type>(_Expected), _Order); }
    void wait(const bool _Expected, const MemoryOrder _Order = MemoryOrder::memory_order_seq_cst)const volatile noexcept{ _Storage.wait(static_cast<decltype(_Storage)::value_type>(_Expected), _Order); }

    void notify_one()noexcept{ _Storage.notify_one(); }
    void notify_one()volatile noexcept{ _Storage.notify_one(); }

    void notify_all()noexcept{ _Storage.notify_all(); }
    void notify_all()volatile noexcept{ _Storage.notify_all(); }


public:
    Atomic<i32> _Storage;
};

_NODISCARD inline bool atomic_flag_test(const volatile AtomicFlag* const _Flag)noexcept{ return _Flag->test(); }
_NODISCARD inline bool atomic_flag_test(const AtomicFlag* const _Flag)noexcept{ return _Flag->test(); }
_NODISCARD inline bool atomic_flag_test_explicit(const volatile AtomicFlag* const _Flag, const MemoryOrder _Order)noexcept{ return _Flag->test(_Order); }
_NODISCARD inline bool atomic_flag_test_explicit(const AtomicFlag* const _Flag, const MemoryOrder _Order)noexcept{ return _Flag->test(_Order); }

inline bool atomic_flag_test_and_set(AtomicFlag* const _Flag)noexcept{ return _Flag->test_and_set(); }
inline bool atomic_flag_test_and_set(volatile AtomicFlag* const _Flag)noexcept{ return _Flag->test_and_set(); }
inline bool atomic_flag_test_and_set_explicit(AtomicFlag* const _Flag, const MemoryOrder _Order)noexcept{ return _Flag->test_and_set(_Order); }
inline bool atomic_flag_test_and_set_explicit(volatile AtomicFlag* const _Flag, const MemoryOrder _Order)noexcept{ return _Flag->test_and_set(_Order); }
inline void atomic_flag_clear(AtomicFlag* const _Flag)noexcept{ _Flag->clear(); }
inline void atomic_flag_clear(volatile AtomicFlag* const _Flag)noexcept{ _Flag->clear(); }
inline void atomic_flag_clear_explicit(AtomicFlag* const _Flag, const MemoryOrder _Order)noexcept{ _Flag->clear(_Order); }
inline void atomic_flag_clear_explicit(volatile AtomicFlag* const _Flag, const MemoryOrder _Order)noexcept{ _Flag->clear(_Order); }

inline void atomic_flag_wait(const volatile AtomicFlag* const _Flag, const bool _Expected)noexcept{ return _Flag->wait(_Expected); }
inline void atomic_flag_wait(const AtomicFlag* const _Flag, const bool _Expected)noexcept{ return _Flag->wait(_Expected); }
inline void atomic_flag_wait_explicit(const volatile AtomicFlag* const _Flag, const bool _Expected, const MemoryOrder _Order)noexcept{ return _Flag->wait(_Expected, _Order); }
inline void atomic_flag_wait_explicit(const AtomicFlag* const _Flag, const bool _Expected, const MemoryOrder _Order)noexcept{ return _Flag->wait(_Expected, _Order); }
inline void atomic_flag_notify_one(volatile AtomicFlag* const _Flag)noexcept{ return _Flag->notify_one(); }
inline void atomic_flag_notify_one(AtomicFlag* const _Flag)noexcept{ return _Flag->notify_one(); }
inline void atomic_flag_notify_all(volatile AtomicFlag* const _Flag)noexcept{ return _Flag->notify_all(); }
inline void atomic_flag_notify_all(AtomicFlag* const _Flag)noexcept{ return _Flag->notify_all(); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

