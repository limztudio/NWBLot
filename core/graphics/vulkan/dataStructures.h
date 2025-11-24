// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../common.h"
#include "config.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename Arena>
class ResourcePoolAny{
public:
    ResourcePoolAny(Arena& arena)
        : m_arena(arena)
    {}
    virtual ~ResourcePoolAny(){}
    
    
public:
    void init(u32 poolSize, u32 resourceSize){
        NWB_ASSERT(!m_data && !m_freeIndices);
        
        m_poolSize = poolSize;
        m_resourceSize = resourceSize;
        
        {
            m_data = m_arena.template allocate<u8>(m_poolSize * m_resourceSize);
            NWB_MEMSET(m_data, 0, m_poolSize * m_resourceSize);
        }
        
        {
            m_freeIndices = m_arena.template allocate<u32>(m_poolSize * m_resourceSize);
            m_freeIndexHead = 0;
            
            for(u32 i = 0; i < m_poolSize; ++i)
                m_freeIndices[i] = i;
        }
        
        m_usedIndices = 0;
    }
    void cleanup(){
#if NWB_OCCUR_WARNING
        if(m_freeIndexHead != 0){
            WString msg = NWB_TEXT("There are still resources in use during cleanup:");
            for(u32 i = 0; i < m_freeIndexHead; ++i)
                msg += StringFormat(NWB_TEXT(" {}"), m_freeIndices[i]);
            NWB_LOGGER_WARNING(msg);
        }
#endif
        NWB_ASSERT(!m_usedIndices);
        
        m_arena.template deallocate<u8>(m_data, m_poolSize * m_resourceSize);
        m_data = nullptr;
        
        m_arena.template deallocate<u32>(m_freeIndices, m_poolSize * m_resourceSize);
        m_freeIndices = nullptr;
    }
    
public:
    
    
    
private:
    Arena& m_arena;
    
private:
    u8* m_data = nullptr;
    u32* m_freeIndices = nullptr;
    
private:
    u32 m_freeIndexHead = 0;
    u32 m_poolSize = 16;
    u32 m_resourceSize = 4;
    u32 m_usedIndices = 0;
};


template <typename Arena>
class ResourcePool{
    
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

