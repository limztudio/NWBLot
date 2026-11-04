// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "generic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ContainerT, typename FailureT>
class AppendedContainerRollbackScope final : NoCopy{
    static_assert(IsNothrowMoveConstructible_V<FailureT> && IsNothrowDestructible_V<FailureT>);


public:
    explicit AppendedContainerRollbackScope(ContainerT& container, FailureT failure)noexcept
        : m_container(container)
        , m_initialSize(container.size())
        , m_failure(Move(failure))
    {}
    ~AppendedContainerRollbackScope()noexcept{
        static_assert(noexcept(m_container.size()));
        static_assert(noexcept(m_container.pop_back()));
        static_assert(noexcept(m_failure()));
        if(m_committed)
            return;
        if(m_container.size() < m_initialSize)
            m_failure();
        while(m_container.size() > m_initialSize)
            m_container.pop_back();
    }


public:
    void commit()noexcept{ m_committed = true; }


private:
    ContainerT& m_container;
    usize m_initialSize = 0u;
    FailureT m_failure;
    b8 m_committed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

