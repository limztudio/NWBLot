// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <usize maxAlignSize = 512>
class ScratchArena : NoCopy{
private:
    class Chunk{
    public:
        inline Chunk(usize align, usize size)
            : m_buffer(nullptr)
            , m_next(nullptr)
            , m_available(nullptr)
            , m_remaining(getAlignment(align, size))
        {
            m_buffer = mi_aligned_alloc(align, m_remaining);
        }
        ~Chunk(){
            mi_free(m_buffer);
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
        void* m_buffer;

    private:
        Chunk* m_next;

        void* m_available;
        usize m_remaining;
    };
    struct ChunkWrapper{
        Chunk* head;
        Chunk* last;
        usize size;
    };


public:
    ScratchArena(usize initSize = 1024)
    {
        for(usize i = 0; i < std::size(m_bucket); ++i){
            auto& bucket = m_bucket[i];
            bucket.head = nullptr;
            bucket.last = nullptr;
            bucket.size = getAlignment(1 << i, initSize);
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
            bucket.size = size;

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


private:
    ChunkWrapper m_bucket[floorlog2(maxAlignSize) + 1];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, usize maxAlignSize>
class ScratchAllocator{
public:
    static_assert(!std::is_const_v<T>, "The C++ Standard forbids containers of const elements because allocator<const T> is ill-formed.");
    static_assert(!std::is_function_v<T>, "The C++ Standard forbids allocators for function elements because of [allocator.requirements].");
    static_assert(!std::is_reference_v<T>, "The C++ Standard forbids allocators for reference elements because of [allocator.requirements].");


public:
    using _From_primary = ScratchAllocator;
    using value_type = T;

    using size_type = usize;
    using difference_type = isize;

    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;


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
        static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");

        T* output = nullptr;

        const usize bytes = getSizeOf<sizeof(T)>(count);

        if(bytes){
            if(std::is_constant_evaluated()){
                output = reinterpret_cast<T*>(m_arena.allocate(1, bytes));
            }
            else{
                constexpr usize alignSize = alignof(T);

                output = reinterpret_cast<T*>(m_arena.allocate(alignSize, bytes));
            }
        }

        return output;
    }
#if _HAS_CXX23
    constexpr std::allocation_result<T*> allocate_at_least(const usize count){ return { allocate(count), count }; }
#endif


private:
    ScratchArena<maxAlignSize>& m_arena;
};
template <typename T, typename F, usize maxAlignSize>
inline bool operator==(const ScratchAllocator<T, maxAlignSize>&, const ScratchAllocator<F, maxAlignSize>&)noexcept{ return true; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

