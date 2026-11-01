// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Initializerable::Initializerable(){
    Initializer::Instance().enqueue(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Initializer& Initializer::Instance(){
    static Initializer s_Instance;
    return s_Instance;
}

b8 Initializer::initialize(){
    m_initializedItemCount = 0u;
    usize initializedItemCount = 0u;
    ScopeExit rollback([&]()noexcept{ finalizeInitialized(initializedItemCount); });
    for(auto& cur : m_items){
        if(!cur.item->initialize())
            return false;
        ++initializedItemCount;
    }
    m_initializedItemCount = initializedItemCount;
    rollback.release();
    return true;
}

void Initializer::finalize()noexcept{
    finalizeInitialized(m_initializedItemCount);
    m_initializedItemCount = 0u;
}

b8 Initializer::acquire(){
    if(m_activeCount > 0u){
        ++m_activeCount;
        return true;
    }

    if(!initialize())
        return false;

    m_activeCount = 1u;
    return true;
}

void Initializer::release()noexcept{
    if(m_activeCount == 0u)
        TerminateInvariant();

    --m_activeCount;
    if(m_activeCount == 0u)
        finalize();
}

void Initializer::finalizeInitialized(const usize initializedItemCount)noexcept{
    usize itemIndex = 0u;
    for(auto& cur : m_items){
        if(itemIndex >= initializedItemCount)
            return;

        cur.item->finalize();
        ++itemIndex;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 InitializerGuard::initialize(){
    if(m_initializer)
        return true;

    Initializer& initializer = Initializer::Instance();
    if(!initializer.acquire())
        return false;

    m_initializer = &initializer;
    return true;
}

void InitializerGuard::finalize()noexcept{
    if(!m_initializer)
        return;

    Initializer* initializer = m_initializer;
    m_initializer = nullptr;
    initializer->release();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

