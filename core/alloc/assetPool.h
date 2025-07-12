// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <template <typename> typename Allocator>
class RawResPool{
private:
    static constexpr const u32 s_defaultPoolSize = 16;
    static constexpr const u32 s_defaultResSize = 4;
    static constexpr const u32 s_invalidHandle = static_cast<u32>(-1);


public:
    RawResPool()
        : m_allocator()
        , m_data(nullptr)
        , m_freeList(nullptr)
        , m_freeListHead(0)
        , m_usedCount(0)
        , m_poolSize(s_defaultPoolSize)
        , m_resSize(s_defaultResSize)
    {}
    RawResPool(const Allocator<u8>& allocator)
        : m_allocator(allocator)
        , m_data(nullptr)
        , m_freeList(nullptr)
        , m_freeListHead(0)
        , m_usedCount(0)
        , m_poolSize(s_defaultPoolSize)
        , m_resSize(s_defaultResSize)
    {}

    virtual ~RawResPool(){ cleanup(); }


public:
    void init(u32 poolSize, u32 resSize){
        m_poolSize = poolSize;
        m_resSize = resSize;

        const usize allocSize = m_poolSize * (m_resSize + sizeof(u32));
        m_data = reinterpret_cast<u8*>(m_allocator.allocate(allocSize));
        NWB_MEMSET(m_data, 0, allocSize);

        m_freeList = reinterpret_cast<u32*>(m_data + m_poolSize * m_resSize);
        m_freeListHead = 0;

        for(u32 i = 0; i < m_poolSize; ++i)
            m_freeList[i] = i;
        m_usedCount = 0;
    }
    void cleanup(){
        if(!m_data)
            return;

        if(m_freeListHead != 0){
            TStringStream ss;
            {
                ss << m_freeList[0];
            }
            for(u32 i = 1; i < m_freeListHead; ++i){
                ss << NWB_TEXT(", ") << m_freeList[i];
            }

            NWB_LOGGER_ERROR(NWB_TEXT("Resource pool is not empty after cleanup. {} resources are still in use: {}"), m_freeListHead, ss.str());
        }

        NWB_ASSERT(m_freeListHead == 0);

        m_allocator.deallocate(m_data, m_poolSize * (m_resSize + sizeof(u32)));

        m_data = nullptr;
        m_freeList = nullptr;
        m_usedCount = 0;
    }

public:
    void releaseAll(){
        m_freeListHead = 0;
        m_usedCount = 0;

        for(u32 i = 0; i < m_poolSize; ++i)
            m_freeList[i] = i;
    }
    void release(u32 handle){
        m_freeList[--m_freeListHead] = handle;
        --m_usedCount;
    }

    u32 assign(){
        if(m_freeListHead < m_poolSize){
            const u32 handle = m_freeList[m_freeListHead++];
            ++m_usedCount;
            return handle;
        }

        NWB_ASSERT(m_freeListHead < m_poolSize);
        return s_invalidHandle;
    }

    void* get(u32 handle){
        NWB_ASSERT(handle != s_invalidHandle);
        return m_data + (handle * m_resSize);
    }
    const void* get(u32 handle)const{
        NWB_ASSERT(handle != s_invalidHandle);
        return m_data + (handle * m_resSize);
    }


protected:
    Allocator<u8> m_allocator;

protected:
    u8* m_data;
    u32* m_freeList;

protected:
    u32 m_freeListHead;
    u32 m_usedCount;
    u32 m_poolSize;
    u32 m_resSize;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, template <typename> typename Allocator>
class AssetPool : protected RawResPool<Allocator>{
public:
    AssetPool(){}
    AssetPool(const Allocator<T>& allocator) : RawResPool<Allocator>(allocator){}


public:
    T* get(u32 handle){ return reinterpret_cast<T*>(RawResPool<Allocator>::get(handle)); }
    const T* get(u32 handle)const{ return reinterpret_cast<const T*>(RawResPool<Allocator>::get(handle)); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

