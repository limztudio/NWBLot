// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#if defined(__SSE__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>
#endif

#if defined(_MSC_VER)
#pragma intrinsic(__rdtsc)

#include <float.h>
#endif

#include <tbb/spin_mutex.h>
#include <tbb/queuing_mutex.h>
#include <tbb/spin_rw_mutex.h>
#include <tbb/queuing_rw_mutex.h>
#include <mutex>
#include <condition_variable>

#include <semaphore>
#include <climits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "generic.h"
#include "type.h"
#include "atomic.h"
#include "thread.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
inline void YieldThread(){ SwitchToThread(); }
#else
inline void YieldThread(){ std::this_thread::yield(); }
#endif


inline void MachinePause([[maybe_unused]] i32 delay){
#if defined(__ARM_ARCH_7A__) || defined(__aarch64__)
    while(delay > 0){
        __asm__ __volatile__("isb sy" ::: "memory");
        --delay;
    }
#elif defined(__SSE__)
    while(delay > 0){
        _mm_pause();
        --delay;
    }
#else /* Generic */
    YieldThread();
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using RecursiveMutex = std::recursive_mutex;
using SpinMutex = tbb::spin_mutex;
using QueuingMutex = tbb::queuing_mutex;
using SharedMutex = tbb::spin_rw_mutex;
using SharedQueuingMutex = tbb::queuing_rw_mutex;

using ConditionVariable = std::condition_variable;
using ConditionVariableAny = std::condition_variable_any;

template<isize LeastMaxValue = static_cast<isize>(INT_MAX)>
using Semaphore = std::counting_semaphore<LeastMaxValue>;
using BinarySemaphore = Semaphore<1>;

template<typename Mutex>
using LockGuard = std::lock_guard<Mutex>;
template<typename Mutex>
using UniqueLock = std::unique_lock<Mutex>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Futex : NoCopy{
private:
    static constexpr u32 s_Unlocked = 0;
    static constexpr u32 s_Locked = 1;
    static constexpr u32 s_LockedContended = 2;
    static constexpr i32 s_SpinCount = 6;


public:
    void lock(){
        if(try_lock())
            return;

        for(i32 i = 0; i < s_SpinCount; ++i){
            if(try_lock())
                return;
            MachinePause(1 << i);
        }

        u32 previousState = m_state.exchange(s_LockedContended, MemoryOrder::acquire);
        while(previousState != s_Unlocked){
            m_state.wait(s_LockedContended, MemoryOrder::relaxed);
            previousState = m_state.exchange(s_LockedContended, MemoryOrder::acquire);
        }
    }

    bool try_lock(){
        u32 expected = s_Unlocked;
        return m_state.compare_exchange_strong(
            expected,
            s_Locked,
            MemoryOrder::acquire,
            MemoryOrder::relaxed
        );
    }

    void unlock(){
        const u32 previous = m_state.fetch_sub(1, MemoryOrder::release);
        if(previous != s_Locked){
            m_state.store(s_Unlocked, MemoryOrder::release);
            m_state.notify_one();
        }
    }


private:
    Atomic<u32> m_state{ s_Unlocked };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//! Class that implements exponential backoff.
class AtomicBackOff{
private:
    //! Time delay, in units of "pause" instructions.
    /** Should be equal to approximately the number of "pause" instructions
      that take the same time as an context switch. Must be a power of two.*/
    static constexpr i32 s_LoopsBeforeYield = 16;


public:
    // In many cases, an object of this type is initialized eagerly on hot path,
    // as in for(AtomicBackOff backoff; ; backoff.pause()) { /*loop body*/ }
    // For this reason, the construction cost must be very small!
    AtomicBackOff()
        : m_count(1)
        {}
    // This constructor pauses immediately; do not use on hot paths!
    AtomicBackOff(bool)
        : m_count(1)
        { pause(); }

    //! No Copy
    AtomicBackOff(const AtomicBackOff&) = delete;
    AtomicBackOff& operator=(const AtomicBackOff&) = delete;

public:
    //! Pause for a while.
    void pause(){
        if(m_count <= s_LoopsBeforeYield){
            MachinePause(m_count);
            // Pause twice as long the next time.
            m_count <<= 1;
        }
        else{
            // Pause is so long that we might as well yield CPU to scheduler.
            YieldThread();
        }
    }

    //! Pause for a few times and return false if saturated.
    bool boundedPause(){
        MachinePause(m_count);
        if(m_count < s_LoopsBeforeYield){
            // Pause twice as long the next time.
            m_count <<= 1;
            return true;
        }
        else{
            return false;
        }
    }

    void reset(){ m_count = 1; }


private:
    i32 m_count;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//! Stripped down version of spin_mutex.
/** Instances of MallocMutex must be declared in memory that is zero-initialized.
    There are no constructors.  This is a feature that lets it be
    used in situations where the mutex might be used while file-scope constructors
    are running. */
class MallocMutex : NoCopy{
public:
    void lock(){
        AtomicBackOff backoff;
        bool locked = m_flag.test_and_set();
        while(locked){
            backoff.pause();
            locked = m_flag.test_and_set();
        }
    }
    bool try_lock(){ return (!m_flag.test_and_set()); }
    void unlock(){ m_flag.clear(MemoryOrder::release); }


private:
    AtomicFlag m_flag = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... Mutexes>
class ScopedLock : public std::scoped_lock<Mutexes...>{
public:
    using std::scoped_lock<Mutexes...>::scoped_lock;
};
template<typename... Mutexes>
ScopedLock(Mutexes&...) -> ScopedLock<Mutexes...>;
template<isize LeastMaxValue>
class ScopedLock<Semaphore<LeastMaxValue>> : NoCopy{
public:
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;

    ScopedLock(Semaphore<LeastMaxValue>& obj)
        : m_obj(obj)
    {
        m_obj.acquire();
    }
    ~ScopedLock(){ m_obj.release(); }


private:
    Semaphore<LeastMaxValue>& m_obj;
};
template<isize LeastMaxValue>
ScopedLock(Semaphore<LeastMaxValue>&) -> ScopedLock<Semaphore<LeastMaxValue>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

