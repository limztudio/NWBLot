// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, template <typename> typename Allocator>
class AssetPool{
private:
    static constexpr const u32 s_defaultPoolSize = 16;
    static constexpr const u32 s_invalidHandle = static_cast<u32>(-1);


public:
    AssetPool()
        : m_dataAllocator()
        , m_indexAllocator()
        , m_data(nullptr)
        , m_freeList(nullptr)
        , m_freeListHead(0)
        , m_usedCount(0)
        , m_poolSize(s_defaultPoolSize)
    {}
    AssetPool(const Allocator<T>& dataAllocator, const Allocator<u32>& indexAllocator)
        : m_dataAllocator(dataAllocator)
        , m_indexAllocator(indexAllocator)
        , m_data(nullptr)
        , m_freeList(nullptr)
        , m_freeListHead(0)
        , m_usedCount(0)
        , m_poolSize(s_defaultPoolSize)
    {}

    virtual ~RawResPool(){ cleanup(); }


public:
    void init(){
        m_data = reinterpret_cast<T*>(m_dataAllocator.allocate(m_poolSize));
        m_freeList = reinterpret_cast<u32*>(m_indexAllocator.allocate(m_poolSize));

        NWB_MEMSET(m_data, 0, m_poolSize * sizeof(T));
        m_freeListHead = 0;

        for(u32 i = 0; i < m_poolSize; ++i)
            m_freeList[i] = i;
        m_usedCount = 0;
    }
    void init(u32 poolSize){
        m_poolSize = poolSize;
        init();
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

            for(u32 i = 0; i < m_freeListHead; ++i){
                u32 handle = m_freeList[i];

                m_data[handle].~T();
            }
        }
        NWB_ASSERT(m_freeListHead == 0);

        m_dataAllocator.deallocate(m_data, m_poolSize);
        m_indexAllocator.deallocate(m_freeList, m_poolSize);

        m_data = nullptr;
        m_freeList = nullptr;
        m_usedCount = 0;
        m_freeListHead = 0;
    }

public:
    void releaseAll(){
        m_freeListHead = 0;
        m_usedCount = 0;

        for(u32 i = 0; i < m_freeListHead; ++i){
            u32 handle = m_freeList[i];

            m_data[handle].~T();
        }

        for(u32 i = 0; i < m_poolSize; ++i)
            m_freeList[i] = i;
    }
    void release(u32 handle){
        NWB_ASSERT(handle != s_invalidHandle);
        NWB_ASSERT(m_freeListHead > 0);
        NWB_ASSERT(m_usedCount > 0);

        m_data[handle].~T();

        m_freeList[--m_freeListHead] = handle;
        --m_usedCount;
    }

    u32 assign(){
        if(m_freeListHead < m_poolSize){
            const u32 handle = m_freeList[m_freeListHead++];
            ++m_usedCount;

            new (&m_data[handle]) T();
            return handle;
        }

        NWB_ASSERT(m_freeListHead < m_poolSize);
        return s_invalidHandle;
    }

    T& operator[](u32 handle){
        NWB_ASSERT(handle != s_invalidHandle);
        return m_data[handle];
    }
    const T& operator[](u32 handle)const{
        NWB_ASSERT(handle != s_invalidHandle);
        return m_data[handle];
    }


private:
    Allocator<T> m_dataAllocator;
    Allocator<u32> m_indexAllocator;

private:
    T* m_data;
    u32* m_freeList;

private:
    u32 m_freeListHead;
    u32 m_usedCount;
    u32 m_poolSize;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

