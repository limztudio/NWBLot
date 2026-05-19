// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MaxAlignSize = 512;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize maxAlignSize = s_MaxAlignSize>
class ScratchArena : NoCopy{
private:
    class Chunk{
        friend class ScratchArena;


    public:
        inline Chunk(usize align, usize size)
            : m_size(Alignment(align, size))
            , m_remaining(m_size)
            , m_next(nullptr)
            , m_buffer(CoreAllocAligned(m_size, align, "NWB::Core::Alloc::ScratchArena::Chunk::constructor"))
            , m_available(m_buffer)
        {}
        ~Chunk(){
            CoreFreeAligned(m_buffer, "NWB::Core::Alloc::ScratchArena::Chunk::destructor");
        }


        inline void* allocate(usize size){
            auto* ret = m_available;
            m_available = reinterpret_cast<u8*>(m_available) + static_cast<isize>(size);
            m_remaining -= size;
            return ret;
        }
        inline bool deallocate(usize size){
            const bool isValidRequest = m_remaining <= m_size && size <= (m_size - m_remaining);
            NWB_ASSERT(isValidRequest);
            if(!isValidRequest)
                return false;

            m_available = reinterpret_cast<u8*>(m_available) - static_cast<isize>(size);
            m_remaining += size;
            return true;
        }

        inline void add(Chunk* next){
            m_next = next;
        }


    private:
        const usize m_size;
        usize m_remaining;
        Chunk* m_next;

        void* m_buffer;
        void* m_available;
    };
    struct ChunkWrapper{
        Chunk* head;
        Chunk* last;
        usize size;
    };


public:
    ScratchArena(usize initSize = 1024){
        for(usize i = 0; i < LengthOf(m_bucket); ++i){
            auto& bucket = m_bucket[i];
            bucket.head = nullptr;
            bucket.last = nullptr;
            bucket.size = Alignment(static_cast<usize>(1) << i, initSize);
        }
    }
    ~ScratchArena(){
        for(auto& bucket : m_bucket){
            for(auto* cur = bucket.head; cur;){
                auto* next = cur->m_next;
                delete cur;
                cur = next;
            }
        }
    }


public:
    inline void* allocate(usize align, usize size){
        NWB_ASSERT_MSG(align != 0, NWB_TEXT("ScratchArena alignment must be non-zero"));
        NWB_ASSERT_MSG(align <= maxAlignSize, NWB_TEXT("ScratchArena alignment exceeds maxAlignSize"));
        if(align == 0 || align > maxAlignSize)
            return nullptr;

        const usize bucketIndex = FloorLog2(align);
        NWB_ASSERT_MSG(bucketIndex < LengthOf(m_bucket), NWB_TEXT("ScratchArena alignment bucket index is out of range"));
        if(bucketIndex >= LengthOf(m_bucket))
            return nullptr;

        auto& bucket = m_bucket[bucketIndex];

        size = Alignment(align, size);
        if(size > bucket.size)
            bucket.size = (size > (static_cast<usize>(-1) >> 1)) ? size : (size << 1);

        if(!bucket.head){
            bucket.head = new Chunk(align, bucket.size);
            bucket.last = bucket.head;
        }
        else if(size > bucket.last->m_remaining){
            bucket.last->add(new Chunk(align, bucket.size));
            bucket.last = bucket.last->m_next;
        }
        return bucket.last->allocate(size);
    }
    template<typename T>
    inline T* allocate(usize count){
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
        static_assert(alignof(T) <= maxAlignSize, "ScratchArena cannot allocate types with alignment greater than maxAlignSize.");
        const usize bytes = SizeOf<sizeof(T)>(count);

        T* output = nullptr;
        if(bytes){
            if(IsConstantEvaluated())
                output = reinterpret_cast<T*>(allocate(1, bytes));
            else{
                constexpr usize alignSize = alignof(T);
                output = reinterpret_cast<T*>(allocate(alignSize, bytes));
            }
        }
        return output;
    }

    inline void deallocate(void* p, usize align, usize size){
        static_cast<void>(p);

        NWB_ASSERT_MSG(align != 0, NWB_TEXT("ScratchArena alignment must be non-zero"));
        NWB_ASSERT_MSG(align <= maxAlignSize, NWB_TEXT("ScratchArena alignment exceeds maxAlignSize"));
        if(align == 0 || align > maxAlignSize)
            return;

        const usize bucketIndex = FloorLog2(align);
        NWB_ASSERT_MSG(bucketIndex < LengthOf(m_bucket), NWB_TEXT("ScratchArena alignment bucket index is out of range"));
        if(bucketIndex >= LengthOf(m_bucket))
            return;

        auto& bucket = m_bucket[bucketIndex];
        NWB_ASSERT_MSG(bucket.last != nullptr, NWB_TEXT("Attempted to deallocate before allocating"));
        if(!bucket.last)
            return;

        size = Alignment(align, size);
        bucket.last->deallocate(size);
    }
    template<typename T>
    inline void deallocate(void* p, usize count){
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
        static_assert(alignof(T) <= maxAlignSize, "ScratchArena cannot deallocate types with alignment greater than maxAlignSize.");
        const usize bytes = SizeOf<sizeof(T)>(count);

        if(bytes){
            if(IsConstantEvaluated())
                deallocate(p, 1, bytes);
            else{
                constexpr usize alignSize = alignof(T);
                deallocate(p, alignSize, bytes);
            }
        }
    }


private:
    ChunkWrapper m_bucket[FloorLog2(maxAlignSize) + 1];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, usize maxAlignSize = NWB::Core::Alloc::s_MaxAlignSize>
using ScratchUniquePtr = UniquePtr<T, ArenaDeleter<T, NWB::Core::Alloc::ScratchArena<maxAlignSize>>>;
template<typename T>
using StackonlyUniquePtr = UniquePtr<T, EmptyDeleter<T>>;

template<typename T, usize maxAlignSize = NWB::Core::Alloc::s_MaxAlignSize, typename... Args>
inline typename EnableIf<!IsArray<T>::value, ScratchUniquePtr<T, maxAlignSize>>::type MakeScratchUnique(NWB::Core::Alloc::ScratchArena<maxAlignSize>& arena, Args&&... args){
    return ScratchUniquePtr<T, maxAlignSize>(new(arena.template allocate<T>(1)) T(Forward<Args>(args)...), typename ScratchUniquePtr<T, maxAlignSize>::deleter_type(arena));
}
template<typename T, usize maxAlignSize = NWB::Core::Alloc::s_MaxAlignSize>
inline typename EnableIf<IsUnboundedArray<T>::value, ScratchUniquePtr<T, maxAlignSize>>::type MakeScratchUnique(NWB::Core::Alloc::ScratchArena<maxAlignSize>& arena, usize n){
    typedef typename RemoveExtent<T>::type TBase;
    return ScratchUniquePtr<T, maxAlignSize>(new(arena.template allocate<TBase>(n)) TBase[n], typename ScratchUniquePtr<T, maxAlignSize>::deleter_type(arena, n));
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeScratchUnique(Args&&...) = delete;

template<typename T, usize maxAlignSize = NWB::Core::Alloc::s_MaxAlignSize, typename... Args>
inline typename EnableIf<!IsArray<T>::value, StackonlyUniquePtr<T>>::type MakeStackonlyUnique(NWB::Core::Alloc::ScratchArena<maxAlignSize>& arena, Args&&... args){
    return StackonlyUniquePtr<T>(new(arena.template allocate<T>(1)) T(Forward<Args>(args)...));
}
template<typename T, usize maxAlignSize = NWB::Core::Alloc::s_MaxAlignSize>
inline typename EnableIf<IsUnboundedArray<T>::value, StackonlyUniquePtr<T>>::type MakeStackonlyUnique(NWB::Core::Alloc::ScratchArena<maxAlignSize>& arena, usize n){
    typedef typename RemoveExtent<T>::type TBase;
    return StackonlyUniquePtr<T>(new(arena.template allocate<TBase>(n)) TBase[n]);
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeStackonlyUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

