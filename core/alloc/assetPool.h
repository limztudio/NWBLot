// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/client/logger.h>

#include "global.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
struct AssetTypeTag{};
constexpr u8 AssetTypeIndex(AssetTypeTag<void>)noexcept{ return static_cast<u8>(-1); }


class AssetHandleAny{
    template <typename _T, template <typename> typename Allocator>
    friend class AssetPool;


public:
    union HandleType{
        u32 raw;
        struct{
            u32 index : 24;
            u32 type : 8;
        };
    };


protected:
    static constexpr const u32 s_indexMax = (1 << 24) - 1;
    static constexpr const HandleType s_invalidHandle = { static_cast<u32>(-1) };


protected:
    AssetHandleAny()noexcept : m_value(s_invalidHandle){}
    ~AssetHandleAny(){ m_value = s_invalidHandle; }


public:
    u8 getType()const noexcept{ return static_cast<u8>(m_value.type); }
    bool isValid()const noexcept{ return (m_value.index != s_invalidHandle.index) && (m_value.type != s_invalidHandle.type); }


protected:
    HandleType m_value;
};

class AssetHandleFlexible;

template <typename T>
class AssetHandle : public AssetHandleAny{
    friend AssetHandleFlexible;
    template <typename _T, template <typename> typename Allocator>
    friend class AssetPool;

    friend bool operator==(const AssetHandle&, const AssetHandle&)noexcept;
    friend bool operator!=(const AssetHandle&, const AssetHandle&)noexcept;

    friend bool operator==(const AssetHandle&, const AssetHandleFlexible&)noexcept;
    friend bool operator!=(const AssetHandle&, const AssetHandleFlexible&)noexcept;
    friend bool operator==(const AssetHandleFlexible&, const AssetHandle&)noexcept;
    friend bool operator!=(const AssetHandleFlexible&, const AssetHandle&)noexcept;


public:
    AssetHandle()noexcept{ m_value.type = AssetTypeIndex(AssetTypeTag<T>{}); }
    AssetHandle(u32 value)noexcept{
        NWB_ASSERT(value <= s_indexMax);
        m_value.type = AssetTypeIndex(AssetTypeTag<T>{});
        m_value.index = value;
    }

    AssetHandle(const AssetHandle& rhs)noexcept{
        NWB_ASSERT(rhs.m_value.type == AssetTypeIndex(AssetTypeTag<T>{}));
        m_value.raw = rhs.m_value.raw;
    }
    AssetHandle(AssetHandle&& rhs)noexcept{
        NWB_ASSERT(rhs.m_value.type == AssetTypeIndex(AssetTypeTag<T>{}));
        m_value.raw = rhs.m_value.raw;
        rhs.m_value.index = s_invalidHandle.index;
    }


public:
    AssetHandle& operator=(const AssetHandle& rhs)noexcept{
        if(this != &rhs){
            NWB_ASSERT(rhs.m_value.type == AssetTypeIndex(AssetTypeTag<T>{}));
            m_value.raw = rhs.m_value.raw;
        }
        return *this;
    }
    AssetHandle& operator=(AssetHandle&& rhs)noexcept{
        if(this != &rhs){
            NWB_ASSERT(rhs.m_value.type == AssetTypeIndex(AssetTypeTag<T>{}));
            m_value.raw = rhs.m_value.raw;
            rhs.m_value.index = s_invalidHandle.index;
        }
        return *this;
    }

public:
    operator bool()const noexcept{ return isValid(); }
};
template <typename T>
inline bool operator==(const AssetHandle<T>& lhs, const AssetHandle<T>& rhs)noexcept{ return lhs.m_value.raw == rhs.m_value.raw; }
template <typename T>
inline bool operator!=(const AssetHandle<T>& lhs, const AssetHandle<T>& rhs)noexcept{ return lhs.m_value.raw != rhs.m_value.raw; }

class AssetHandleFlexible : public AssetHandleAny{
    friend bool operator==(const AssetHandleFlexible&, const AssetHandleFlexible&)noexcept;
    friend bool operator!=(const AssetHandleFlexible&, const AssetHandleFlexible&)noexcept;

    template <typename T>
    friend bool operator==(const AssetHandle<T>&, const AssetHandleFlexible&)noexcept;
    template <typename T>
    friend bool operator!=(const AssetHandle<T>&, const AssetHandleFlexible&)noexcept;
    template <typename T>
    friend bool operator==(const AssetHandleFlexible&, const AssetHandle<T>&)noexcept;
    template <typename T>
    friend bool operator!=(const AssetHandleFlexible&, const AssetHandle<T>&)noexcept;


public:
    AssetHandleFlexible()noexcept{}

    AssetHandleFlexible(const AssetHandleFlexible& rhs)noexcept{
        m_value.raw = rhs.m_value.raw;
    }
    AssetHandleFlexible(AssetHandleFlexible&& rhs)noexcept{
        m_value.raw = rhs.m_value.raw;
        rhs.m_value.index = s_invalidHandle.index;
    }
    template <typename T>
    AssetHandleFlexible(const AssetHandle<T>& rhs)noexcept{
        m_value.raw = rhs.m_value.raw;
    }
    template <typename T>
    AssetHandleFlexible(AssetHandle<T>&& rhs)noexcept{
        m_value.raw = rhs.m_value.raw;
        rhs.m_value.index = s_invalidHandle.index;
    }


public:
    AssetHandleFlexible& operator=(const AssetHandleFlexible& rhs)noexcept{
        if(this != &rhs)
            m_value.raw = rhs.m_value.raw;
        return *this;
    }
    AssetHandleFlexible& operator=(AssetHandleFlexible&& rhs)noexcept{
        if(this != &rhs){
            m_value.raw = rhs.m_value.raw;
            rhs.m_value = s_invalidHandle;
        }
        return *this;
    }
    template <typename T>
    AssetHandleFlexible& operator=(const AssetHandle<T>& rhs)noexcept{
        if(reinterpret_cast<const void*>(this) != &rhs)
            m_value.raw = rhs.m_value.raw;
        return *this;
    }
    template <typename T>
    AssetHandleFlexible& operator=(AssetHandle<T>&& rhs)noexcept{
        if(reinterpret_cast<const void*>(this) != &rhs){
            m_value.raw = rhs.m_value.raw;
            rhs.m_value = s_invalidHandle;
        }
        return *this;
    }

public:
    operator bool()const noexcept{ return isValid(); }
};
inline bool operator==(const AssetHandleFlexible& lhs, const AssetHandleFlexible& rhs)noexcept{ return lhs.m_value.raw == rhs.m_value.raw; }
inline bool operator!=(const AssetHandleFlexible& lhs, const AssetHandleFlexible& rhs)noexcept{ return lhs.m_value.raw != rhs.m_value.raw; }

template <typename T>
inline bool operator==(const AssetHandle<T>& lhs, const AssetHandleFlexible& rhs)noexcept{ return lhs.m_value.raw == rhs.m_value.raw; }
template <typename T>
inline bool operator!=(const AssetHandle<T>& lhs, const AssetHandleFlexible& rhs)noexcept{ return lhs.m_value.raw != rhs.m_value.raw; }
template <typename T>
inline bool operator==(const AssetHandleFlexible& lhs, const AssetHandle<T>& rhs)noexcept{ return lhs.m_value.raw == rhs.m_value.raw; }
template <typename T>
inline bool operator!=(const AssetHandleFlexible& lhs, const AssetHandle<T>& rhs)noexcept{ return lhs.m_value.raw != rhs.m_value.raw; }


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

    virtual ~AssetPool(){ cleanup(); }


public:
    void init(){
        NWB_ASSERT(m_poolSize <= AssetHandleAny::s_indexMax);

        m_data = reinterpret_cast<T*>(m_dataAllocator.allocate(m_poolSize));
        m_freeList = reinterpret_cast<AssetHandle<T>*>(m_indexAllocator.allocate(m_poolSize));

        NWB_MEMSET(m_data, 0, m_poolSize * sizeof(T));
        m_freeListHead = 0;

        for(u32 i = 0; i < m_poolSize; ++i){
            m_freeList[i].m_value.type = AssetTypeIndex(AssetTypeTag<T>{});
            m_freeList[i].m_value.index = i;
        }
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
                    ss << m_freeList[0].m_value.index;
                }
                for(u32 i = 1; i < m_freeListHead; ++i){
                    ss << NWB_TEXT(", ") << m_freeList[i].m_value.index;
                }

                NWB_LOGGER_ERROR(NWB_TEXT("Resource pool is not empty after cleanup. {} resources are still in use: {}"), m_freeListHead, ss.str());
            }

            for(u32 i = 0; i < m_freeListHead; ++i){
                const auto handleValue = m_freeList[i].m_value.index;

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
            const auto handleValue = m_freeList[i].m_value.index;

            m_data[handleValue].~T();
        }

        for(u32 i = 0; i < m_poolSize; ++i)
            m_freeList[i].m_value.index = i;
    }
    void release(AssetHandle<T> handle){
        NWB_ASSERT(handle.isValid());
        NWB_ASSERT(m_freeListHead > 0);
        NWB_ASSERT(m_usedCount > 0);

        m_data[handle.m_value.index].~T();

        m_freeList[--m_freeListHead] = handle;
        --m_usedCount;
    }

    AssetHandle<T> assign(){
        AssetHandle<T> handle;

        if(m_freeListHead < m_poolSize){
            handle = m_freeList[m_freeListHead++];
            ++m_usedCount;

            new (&m_data[handle.m_value.index]) T();
        }
        NWB_ASSERT(m_freeListHead < m_poolSize);
        return handle;
    }

    T& operator[](const AssetHandle<T>& handle){
        NWB_ASSERT(handle.isValid());
        return m_data[handle.m_value.index];
    }
    const T& operator[](const AssetHandle<T>& handle)const{
        NWB_ASSERT(handle.isValid());
        return m_data[handle.m_value.index];
    }


private:
    Allocator<T> m_dataAllocator;
    Allocator<AssetHandle<T>> m_indexAllocator;

private:
    T* m_data;
    AssetHandle<T>* m_freeList;

private:
    u32 m_freeListHead;
    u32 m_usedCount;
    u32 m_poolSize;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

