// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "arena_names.h"

#include <core/alloc/general.h>

#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CommonDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BaseInitializerable{
public:
    virtual ~BaseInitializerable()noexcept = default;


public:
    virtual b8 initialize() = 0;
    virtual void finalize()noexcept = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Initializerable : public CommonDetail::BaseInitializerable{
public:
    Initializerable();
};


class Initializer{
private:
    struct InitializerItem{
        NotNull<CommonDetail::BaseInitializerable*> item;


    public:
        explicit InitializerItem(CommonDetail::BaseInitializerable& value)noexcept
            : item(MakeNotNull(&value))
        {}
    };


public:
    static Initializer& Instance();


private:
    Initializer()
        : m_arena(CommonArenaScope::s_InitializerArena)
        , m_items(m_arena)
        , m_cursor(m_items.before_begin())
    {}


public:
    b8 initialize();
    void finalize()noexcept;
    [[nodiscard]] b8 acquire();
    void release()noexcept;


public:
    inline void enqueue(Initializerable& item){ m_cursor = m_items.emplace_after(m_cursor, item); }


private:
    void finalizeInitialized(usize initializedItemCount)noexcept;


private:
    Alloc::GlobalArena m_arena;
    ForwardList<InitializerItem, Alloc::GlobalArena> m_items;
    decltype(m_items)::iterator m_cursor;
    usize m_initializedItemCount = 0;
    usize m_activeCount = 0;
};

class InitializerGuard : NoCopy{
public:
    ~InitializerGuard()noexcept{ finalize(); }


public:
    [[nodiscard]] b8 initialize();
    void finalize()noexcept;


private:
    Initializer* m_initializer = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

