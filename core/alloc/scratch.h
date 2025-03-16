// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const usize s_maxAlignSize = 512;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <usize maxAlignSize = s_maxAlignSize>
class ScratchArena : NoCopy{
private:
    class Chunk{
    public:
        inline Chunk(usize align, usize size)
            : m_remaining(getAlignment(align, size))
            , m_next(nullptr)
            , m_buffer(coreAllocAligned(m_remaining, align, "NWB::Core::Alloc::ScratchArena::Chunk::constructor"))
            , m_available(m_buffer)
        {}
        ~Chunk(){
            coreFree(m_buffer, "NWB::Core::Alloc::ScratchArena::Chunk::destructor");
        }


    public:
        inline void* buffer()const{ return m_buffer; }
        inline Chunk* next()const{ return m_next; }
        inline usize remaining()const{ return m_remaining; }

        inline void* allocate(usize size){
            auto* ret = m_available;
            m_available = reinterpret_cast<u8*>(m_available) + size;
            m_remaining -= size;
            return ret;
        }

        inline void add(Chunk* next){
            m_next = next;
        }


    private:
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
    ScratchArena(usize initSize = 1024)
    {
        for(usize i = 0; i < LengthOf(m_bucket); ++i){
            auto& bucket = m_bucket[i];
            bucket.head = nullptr;
            bucket.last = nullptr;
            bucket.size = getAlignment(static_cast<usize>(1) << i, initSize);
        }
    }
    ~ScratchArena(){
        for(auto& bucket : m_bucket){
            for(auto* cur = bucket.head; cur;){
                auto* next = cur->next();
                delete cur;
                cur = next;
            }
        }
    }


public:
    inline void* allocate(usize align, usize size){
        auto& bucket = m_bucket[floorlog2(align)];

        size = getAlignment(align, size);
        if(size > bucket.size)
            bucket.size = size << 1;

        if(!bucket.head){
            bucket.head = new Chunk(align, bucket.size);
            bucket.last = bucket.head;
        }
        else if(size > bucket.last->remaining()){
            bucket.last->add(new Chunk(align, bucket.size));
            bucket.last = bucket.last->next();
        }
        return bucket.last->allocate(size);
    }
    template <typename T>
    inline T* allocate(usize count){
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
        const usize bytes = getSizeOf<sizeof(T)>(count);

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


private:
    ChunkWrapper m_bucket[floorlog2(maxAlignSize) + 1];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, usize maxAlignSize = s_maxAlignSize>
class ScratchAllocator{
public:
    static_assert(IsConst_V<T>, "The C++ Standard forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!IsFunction_V<T>, "The C++ Standard forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!IsReference_V<T>, "The C++ Standard forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = ScratchAllocator;
    using value_type = T;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = TrueType;
    using is_always_equal = TrueType;


public:
    constexpr ScratchAllocator(ScratchArena<maxAlignSize>& arena)noexcept : m_arena(arena){}
    constexpr ScratchAllocator(const ScratchAllocator&)noexcept = default;
    template <class F>
    constexpr ScratchAllocator(const ScratchAllocator<F, maxAlignSize>&)noexcept{}

    constexpr ~ScratchAllocator() = default;
    constexpr ScratchAllocator& operator=(const ScratchAllocator&) = default;


public:
    constexpr void deallocate(T* const buffer, const usize count)noexcept{
        assert((buffer != nullptr || count == 0) && "null pointer cannot point to a block of non-zero size");

        const usize bytes = sizeof(T) * count;
        (void)bytes;
    }

    constexpr __declspec(allocator) T* allocate(const usize count){
        return m_arena.allocate<T>(count);
    }
#if _HAS_CXX23
    constexpr AllocationResult<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif


private:
    ScratchArena<maxAlignSize>& m_arena;
};
template <typename T, typename F, usize maxAlignSize>
inline bool operator==(const ScratchAllocator<T, maxAlignSize>&, const ScratchAllocator<F, maxAlignSize>&)noexcept{ return true; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
using ScratchUniquePtr = UniquePtr<T, EmptyDeleter<T>>;

template <typename T, usize maxAlignSize = NWB::Core::Alloc::s_maxAlignSize, typename... Args>
inline typename EnableIf<!IsArray<T>::value, ScratchUniquePtr<T>>::type makeScratchUnique(NWB::Core::Alloc::ScratchArena<maxAlignSize>& arena, Args&&... args){
    return ScratchUniquePtr<T>(new(arena.allocate<T>(1)) T(Forward<Args>(args)...));
}
template <typename T, usize maxAlignSize = NWB::Core::Alloc::s_maxAlignSize>
inline typename EnableIf<IsUnboundedArray<T>::value, ScratchUniquePtr<T>>::type makeScratchUnique(NWB::Core::Alloc::ScratchArena<maxAlignSize>& arena, size_t n){
    typedef typename RemoveExtent<T>::type TBase;
    return ScratchUniquePtr<T>(new(arena.allocate<TBase>(n)) TBase[n]);
}
template <typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
makeScratchUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

