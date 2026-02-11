// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#if defined(_MSC_VER)
#pragma intrinsic(__rdtsc)

#include <immintrin.h>
#include <float.h>
#endif

#include <tbb/mutex.h>
#include <tbb/spin_mutex.h>
#include <tbb/queuing_mutex.h>
#include <tbb/spin_rw_mutex.h>
#include <tbb/queuing_rw_mutex.h>
#include <mutex>

#include <semaphore>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "generic.h"
#include "type.h"
#include "atomic.h"
#include "thread.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
static inline void yield(){ SwitchToThread(); }
#else
using std::this_thread::yield;
#endif


static inline void machine_pause(std::int32_t delay){
#if defined(__ARM_ARCH_7A__) || defined(__aarch64__)
    while(delay-- > 0){ __asm__ __volatile__("isb sy" ::: "memory"); }
#elif defined(__SSE__)
    while(delay-- > 0){ _mm_pause(); }
#else /* Generic */
    (void)delay; // suppress without including _template_helpers.h
    yield();
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Mutex = tbb::mutex;
using RecursiveMutex = std::recursive_mutex;
using SpinMutex = tbb::spin_mutex;
using QueuingMutex = tbb::queuing_mutex;
using SharedMutex = tbb::spin_rw_mutex;
using SharedQueuingMutex = tbb::queuing_rw_mutex;

template<isize LeastMaxValue = PTRDIFF_MAX>
using Semaphore = std::counting_semaphore<LeastMaxValue>;
using BinarySemaphore = Semaphore<1>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//! Class that implements exponential backoff.
class AtomicBackOff{
private:
    //! Time delay, in units of "pause" instructions.
    /** Should be equal to approximately the number of "pause" instructions
      that take the same time as an context switch. Must be a power of two.*/
    static constexpr std::int32_t LOOPS_BEFORE_YIELD = 16;


public:
    // In many cases, an object of this type is initialized eagerly on hot path,
    // as in for(atomic_backoff b; ; b.pause()) { /*loop body*/ }
    // For this reason, the construction cost must be very small!
    AtomicBackOff()
        : count(1)
        {}
    // This constructor pauses immediately; do not use on hot paths!
    AtomicBackOff(bool)
        : count(1)
        { pause(); }

    //! No Copy
    AtomicBackOff(const AtomicBackOff&) = delete;
    AtomicBackOff& operator=(const AtomicBackOff&) = delete;

public:
    //! Pause for a while.
    void pause(){
        if(count <= LOOPS_BEFORE_YIELD){
            machine_pause(count);
            // Pause twice as long the next time.
            count <<= 1;
        }
        else{
            // Pause is so long that we might as well yield CPU to scheduler.
            yield();
        }
    }

    //! Pause for a few times and return false if saturated.
    bool bounded_pause(){
        machine_pause(count);
        if(count < LOOPS_BEFORE_YIELD){
            // Pause twice as long the next time.
            count <<= 1;
            return true;
        }
        else{
            return false;
        }
    }

    void reset(){ count = 1; }


private:
    std::int32_t count;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//! Stripped down version of spin_mutex.
/** Instances of MallocMutex must be declared in memory that is zero-initialized.
    There are no constructors.  This is a feature that lets it be
    used in situations where the mutex might be used while file-scope constructors
    are running.

    There are no methods "acquire" or "release".  The scoped_lock must be used
    in a strict block-scoped locking pattern.  Omitting these methods permitted
    further simplification. */
class MallocMutex : NoCopy{
public:
    class scoped_lock : NoCopy{
    public:
        scoped_lock(MallocMutex& m)
        : m_mutex(m), m_taken(true)
        { m.lock(); }
        scoped_lock(MallocMutex& m, bool block, bool* locked)
            : m_mutex(m), m_taken(false)
        {
            if(block){
                m.lock();
                m_taken = true;
            }
            else
                m_taken = m.try_lock();
            if(locked)
                *locked = m_taken;
        }
        ~scoped_lock(){
            if(m_taken)
                m_mutex.unlock();
        }

        scoped_lock(scoped_lock& other) = delete;
        scoped_lock& operator=(scoped_lock&) = delete;


    private:
        MallocMutex& m_mutex;
        bool m_taken;
    };
    friend class scoped_lock;


private:
    void lock(){
        AtomicBackOff backoff;
        while(m_flag.test_and_set())
            backoff.pause();
    }
    bool try_lock(){ return (!m_flag.test_and_set()); }
    void unlock(){ m_flag.clear(MemoryOrder::memory_order_release); }


private:
    AtomicFlag m_flag = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class ScopedLock : NoCopy{
public:
    ScopedLock(ScopedLock& other) = delete;
    ScopedLock& operator=(ScopedLock&) = delete;

    ScopedLock(T& obj) : m_obj(obj){ m_obj.lock(); }
    ~ScopedLock(){ m_obj.unlock(); }


private:
    T& m_obj;
};
template<>
class ScopedLock<MallocMutex> : NoCopy{
public:
    ScopedLock(ScopedLock& other) = delete;
    ScopedLock& operator=(ScopedLock&) = delete;

    ScopedLock(MallocMutex& obj) : lock(obj){}


private:
    MallocMutex::scoped_lock lock;
};
template<isize LeastMaxValue>
class ScopedLock<Semaphore<LeastMaxValue>> : NoCopy{
public:
    ScopedLock(ScopedLock& other) = delete;
    ScopedLock& operator=(ScopedLock&) = delete;

    ScopedLock(Semaphore<LeastMaxValue>& obj) : m_obj(obj){ m_obj.acquire(); }
    ~ScopedLock(){ m_obj.release(); }


private:
    Semaphore<LeastMaxValue>& m_obj;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

