// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetHandle{
    template <typename T, template <typename> typename Allocator>
    friend class AssetPool;

    friend bool operator==(const AssetHandle&, const AssetHandle&)noexcept;
    friend bool operator!=(const AssetHandle&, const AssetHandle&)noexcept;


private:
    static constexpr const u32 s_invalidHandle = static_cast<u32>(-1);


public:
    AssetHandle()noexcept : m_value(s_invalidHandle){}
    AssetHandle(u32 value)noexcept : m_value(value){}

    AssetHandle(const AssetHandle& rhs)noexcept : m_value(rhs.m_value){}
    AssetHandle(AssetHandle&& rhs)noexcept : m_value(rhs.m_value){ rhs.m_value = s_invalidHandle; }

    ~AssetHandle(){ m_value = s_invalidHandle; }


public:
    AssetHandle& operator=(const AssetHandle& rhs)noexcept{
        if(this != &rhs)
            m_value = rhs.m_value;
        return *this;
    }
    AssetHandle& operator=(AssetHandle&& rhs)noexcept{
        if(this != &rhs){
            m_value = rhs.m_value;
            rhs.m_value = s_invalidHandle;
        }
        return *this;
    }

public:
    operator bool()const noexcept{ return m_value != s_invalidHandle; }
    bool isValid()const noexcept{ return m_value != s_invalidHandle; }


private:
    u32 m_value;
};
inline bool operator==(const AssetHandle& lhs, const AssetHandle& rhs)noexcept{ return lhs.m_value == rhs.m_value; }
inline bool operator!=(const AssetHandle& lhs, const AssetHandle& rhs)noexcept{ return lhs.m_value != rhs.m_value; }


template <typename T, template <typename> typename Allocator>
class AssetPool{
private:
    static constexpr const u32 s_defaultPoolSize = 16;


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
        m_freeList = reinterpret_cast<AssetHandle*>(m_indexAllocator.allocate(m_poolSize));

        NWB_MEMSET(m_data, 0, m_poolSize * sizeof(T));
        m_freeListHead = 0;

        for(u32 i = 0; i < m_poolSize; ++i)
            m_freeList[i].m_value = i;
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
            {
                TStringStream ss;
                {
                    ss << m_freeList[0].m_value;
                }
                for(u32 i = 1; i < m_freeListHead; ++i){
                    ss << NWB_TEXT(", ") << m_freeList[i].m_value;
                }

                NWB_LOGGER_ERROR(NWB_TEXT("Resource pool is not empty after cleanup. {} resources are still in use: {}"), m_freeListHead, ss.str());
            }

            for(u32 i = 0; i < m_freeListHead; ++i){
                const auto handleValue = m_freeList[i].m_value;

                m_data[handleValue].~T();
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
            const auto handleValue = m_freeList[i].m_value;

            m_data[handleValue].~T();
        }

        for(u32 i = 0; i < m_poolSize; ++i)
            m_freeList[i].m_value = i;
    }
    void release(AssetHandle handle){
        NWB_ASSERT(handle.isValid());
        NWB_ASSERT(m_freeListHead > 0);
        NWB_ASSERT(m_usedCount > 0);

        m_data[handle.m_value].~T();

        m_freeList[--m_freeListHead] = handle;
        --m_usedCount;
    }

    AssetHandle assign(){
        AssetHandle handle;

        if(m_freeListHead < m_poolSize){
            handle = m_freeList[m_freeListHead++];
            ++m_usedCount;

            new (&m_data[handle.m_value]) T();
        }
        NWB_ASSERT(m_freeListHead < m_poolSize);
        return handle;
    }

    T& operator[](AssetHandle handle){
        NWB_ASSERT(handle.isValid());
        return m_data[handle.m_value];
    }
    const T& operator[](AssetHandle handle)const{
        NWB_ASSERT(handle.isValid());
        return m_data[handle.m_value];
    }


private:
    Allocator<T> m_dataAllocator;
    Allocator<AssetHandle> m_indexAllocator;

private:
    T* m_data;
    AssetHandle* m_freeList;

private:
    u32 m_freeListHead;
    u32 m_usedCount;
    u32 m_poolSize;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

