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
    static constexpr const u32 s_invalidIndex = static_cast<u32>(-1);
    
    
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
            m_freeIndices = m_arena.template allocate<u32>(m_poolSize);
            m_freeIndicesHead = 0;
            
            for(u32 i = 0; i < m_poolSize; ++i)
                m_freeIndices[i] = i;
        }
        
        m_usedIndices = 0;
    }
    void cleanup(){
#if NWB_OCCUR_WARNING
        if(m_freeIndicesHead != 0){
            WString msg = NWB_TEXT("There are still resources in use during cleanup:");
            for(u32 i = 0; i < m_freeIndicesHead; ++i)
                msg += StringFormat(NWB_TEXT(" {}"), m_freeIndices[i]);
            NWB_LOGGER_WARNING(msg);
        }
#endif
        NWB_ASSERT(!m_usedIndices);
        
        m_arena.template deallocate<u8>(m_data, m_poolSize * m_resourceSize);
        m_data = nullptr;
        
        m_arena.template deallocate<u32>(m_freeIndices, m_poolSize);
        m_freeIndices = nullptr;
    }
    
public:
    void releaseAll(){
        m_freeIndicesHead = 0;
        m_usedIndices = 0;
        
        for(u32 i = 0; i < m_poolSize; ++i)
            m_freeIndices[i] = i;
    }
    
    void releaseResource(u32 index){
        NWB_ASSERT(index != s_invalidIndex);
        NWB_ASSERT(index < m_poolSize);
        NWB_ASSERT(m_usedIndices > 0);
        
        m_freeIndices[--m_freeIndicesHead] = index;
        --m_usedIndices;
    }
    
    u32 assignResource(){
        if(m_freeIndicesHead < m_poolSize){
            const u32 index = m_freeIndices[m_freeIndicesHead++];
            ++m_usedIndices;
            return index;
        }
        NWB_ASSERT(false);
        return s_invalidIndex;
    }
    
public:
    void* accessResource(u32 index){
        NWB_ASSERT(index != s_invalidIndex);
        NWB_ASSERT(index < m_poolSize);

        return &m_data[index * m_resourceSize];
    }
    const void* accessResource(u32 index)const{
        NWB_ASSERT(index != s_invalidIndex);
        NWB_ASSERT(index < m_poolSize);

        return &m_data[index * m_resourceSize];
    }
    
    
private:
    Arena& m_arena;
    
private:
    u8* m_data = nullptr;
    u32* m_freeIndices = nullptr;
    
private:
    u32 m_freeIndicesHead = 0;
    u32 m_poolSize = 16;
    u32 m_resourceSize = 4;
    u32 m_usedIndices = 0;
};


template <typename T, typename Arena>
class ResourcePool : public ResourcePoolAny<Arena>{
public:
    ResourcePool(Arena& arena)
        : ResourcePoolAny<Arena>(arena)
    {}
    
    
public:
    void init(u32 poolSize){ return ResourcePoolAny<Arena>::init(poolSize, sizeof(T)); }
    
public:
    void release(const T* resource){
        resource->~T();
        return ResourcePoolAny<Arena>::releaseResource(resource->getPoolIndex());
    }
    
    template <typename... ARGS>
    T* assign(ARGS&&... args){
        const u32 index = ResourcePoolAny<Arena>::assignResource();
        if(index != ResourcePoolAny<Arena>::s_invalidIndex){
            T* resource = new (ResourcePoolAny<Arena>::accessResource(index)) T(Forward<ARGS>(args)...);
            resource->setPoolIndex(index);
            return resource;
        }
        return nullptr;
    }
    
public:
    T* access(u32 index){ return reinterpret_cast<T*>(ResourcePoolAny<Arena>::accessResource(index)); }
    const T* access(u32 index)const{ return reinterpret_cast<const T*>(ResourcePoolAny<Arena>::accessResource(index)); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

