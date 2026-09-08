// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/alloc/module.h>
#include <core/task/cpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef u32 ObjectType;

struct Object{
    u64 integer;


    constexpr Object(u64 i)noexcept
        : integer(i)
    {}
    Object(void* p)noexcept
        : integer(static_cast<u64>(reinterpret_cast<usize>(p)))
    {}

    [[nodiscard]] void* pointer()const noexcept{ return reinterpret_cast<void*>(static_cast<usize>(integer)); }

    template<typename T>
    explicit operator T*()const noexcept{ return static_cast<T*>(pointer()); }
};

static_assert(sizeof(void*) == sizeof(usize), "Object pointer conversion requires usize to preserve the complete pointer representation");
static_assert(sizeof(usize) == sizeof(u64), "Object native identities require the engine's supported 64-bit address space");
static_assert(sizeof(Object) == sizeof(u64), "Object must remain one canonical 64-bit native identity");
static_assert(alignof(Object) == alignof(u64), "Object alignment must match its canonical 64-bit native identity");
static_assert(IsStandardLayout_V<Object>, "Object must remain layout-stable across graphics module boundaries");
static_assert(IsTriviallyCopyable_V<Object>, "Object must remain trivially copyable across graphics module boundaries");

NWB_INLINE bool operator==(const Object& lhs, const Object& rhs)noexcept{ return lhs.integer == rhs.integer; }
NWB_INLINE bool operator!=(const Object& lhs, const Object& rhs)noexcept{ return lhs.integer != rhs.integer; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsResource{
    template<typename, typename> friend struct ::ArenaRefDeleter;
    template<typename ArenaT, typename ValueT> friend void ::ArenaObjectDetail::DestroyArenaReference(ArenaT* arena, ValueT* value)noexcept;


protected:
    inline explicit GraphicsResource(CpuTaskScheduler& scheduler)noexcept
        : m_cpuScheduler(scheduler)
    {}
    virtual ~GraphicsResource()noexcept = default;


public:
    GraphicsResource(const GraphicsResource&) = delete;
    GraphicsResource(GraphicsResource&&) = delete;
    GraphicsResource& operator=(const GraphicsResource&) = delete;
    GraphicsResource& operator=(GraphicsResource&&) = delete;


public:
    virtual u32 addReference()noexcept = 0;
    virtual u32 release()noexcept = 0;

    virtual Object getNativeHandle(ObjectType type){ static_cast<void>(type); return nullptr; }


protected:
    [[nodiscard]] CpuTaskScheduler& taskScheduler()const noexcept{ return m_cpuScheduler; }

    template<typename Func>
    void scheduleParallelFor(usize begin, usize end, const Func& func){
        m_cpuScheduler.parallelFor(begin, end, func);
    }

    template<typename Func>
    void scheduleParallelFor(usize begin, usize end, usize grainSize, const Func& func){
        m_cpuScheduler.parallelFor(begin, end, grainSize, func);
    }


private:
    CpuTaskScheduler& m_cpuScheduler;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

