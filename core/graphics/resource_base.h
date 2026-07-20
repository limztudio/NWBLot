#pragma once


#include <core/global.h>

#include <core/alloc/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef u32 ObjectType;

struct Object{
    union{
        u64 integer;
        void* pointer;
    };


    constexpr Object(u64 i)noexcept
        : integer(i)
    {}
    constexpr Object(void* p)noexcept
        : pointer(p)
    {}

    template<typename T>
    explicit operator T*()const noexcept{ return static_cast<T*>(pointer); }
};

NWB_INLINE bool operator==(const Object& lhs, const Object& rhs)noexcept{ return lhs.integer == rhs.integer; }
NWB_INLINE bool operator!=(const Object& lhs, const Object& rhs)noexcept{ return lhs.integer != rhs.integer; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsResource : public Alloc::ITaskScheduler{
    template<typename, typename> friend struct Alloc::ArenaRefDeleter;
    template<typename Arena, typename T> friend void Alloc::AllocDetail::DestroyArenaReference(Arena* arena, T* p)noexcept;


protected:
    inline explicit GraphicsResource(Alloc::ThreadPool& pool)noexcept
        : Alloc::ITaskScheduler(pool)
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

