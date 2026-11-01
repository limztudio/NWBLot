// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_ALLOC_NODISCARD [[nodiscard]]

#if defined(_MSC_VER) && defined(_Ret_notnull_) && defined(_Post_writable_byte_size_)
// stay consistent with VCRT definitions
#define NWB_ALLOC_NEW_ATTRIBUTES(n) NWB_ALLOC_NODISCARD _Ret_notnull_ _Post_writable_byte_size_(n)
#define NWB_ALLOC_NEW_NOTHROW_ATTRIBUTES(n) NWB_ALLOC_NODISCARD _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n)
#else
#define NWB_ALLOC_NEW_ATTRIBUTES(n) NWB_ALLOC_NODISCARD
#define NWB_ALLOC_NEW_NOTHROW_ATTRIBUTES(n) NWB_ALLOC_NODISCARD
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_new{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename AllocateFunc>
[[nodiscard]] NotNull<void*> RetryOperatorNew(AllocateFunc allocate){
    void* result = allocate();
    while(!result){
        const NewHandler handler = GetNewHandler();
        if(!handler)
            throw AllocationException();

        (*handler)();
        result = allocate();
    }
    return MakeNotNull(result);
}

[[nodiscard]] NotNull<void*> InternalOperatorNew(const usize size){
    return RetryOperatorNew([size]()noexcept{
        return NWB::Core::Alloc::CoreAlloc(size);
    });
}

[[nodiscard]] NotNull<void*> InternalOperatorNew(const usize size, const usize alignment){
    return RetryOperatorNew([size, alignment]()noexcept{
        return NWB::Core::Alloc::CoreAllocAligned(size, alignment);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void operator delete(void* p)noexcept{
    NWB::Core::Alloc::CoreFree(p);
}

void operator delete(void* p, const NoThrowT&)noexcept{
    NWB::Core::Alloc::CoreFree(p);
}

void operator delete(void* p, usize n)noexcept{
    NWB::Core::Alloc::CoreFreeSize(p, n);
}

void operator delete(void* p, AlignValT)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p);
}

void operator delete(void* p, usize n, AlignValT)noexcept{
    NWB::Core::Alloc::CoreFreeSizeAligned(p, n);
}

void operator delete(void* p, AlignValT, const NoThrowT&)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p);
}

void operator delete[](void* p)noexcept{
    NWB::Core::Alloc::CoreFree(p);
}

void operator delete[](void* p, const NoThrowT&)noexcept{
    NWB::Core::Alloc::CoreFree(p);
}

void operator delete[](void* p, usize n)noexcept{
    NWB::Core::Alloc::CoreFreeSize(p, n);
}

void operator delete[](void* p, AlignValT)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p);
}

void operator delete[](void* p, usize n, AlignValT)noexcept{
    NWB::Core::Alloc::CoreFreeSizeAligned(p, n);
}

void operator delete[](void* p, AlignValT, const NoThrowT&)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p);
}

NWB_ALLOC_NEW_ATTRIBUTES(n) void* operator new(usize n)noexcept(false){
    return __hidden_new::InternalOperatorNew(n).get();
}

NWB_ALLOC_NEW_NOTHROW_ATTRIBUTES(n) void* operator new(usize n, const NoThrowT&)noexcept{
    return NWB::Core::Alloc::CoreAlloc(n);
}

void* operator new(usize n, AlignValT alignment)noexcept(false){
    return __hidden_new::InternalOperatorNew(n, static_cast<usize>(alignment)).get();
}

void* operator new(usize n, AlignValT alignment, const NoThrowT&)noexcept{
    return NWB::Core::Alloc::CoreAllocAligned(n, static_cast<usize>(alignment));
}

NWB_ALLOC_NEW_ATTRIBUTES(n) void* operator new[](usize n)noexcept(false){
    return __hidden_new::InternalOperatorNew(n).get();
}

NWB_ALLOC_NEW_NOTHROW_ATTRIBUTES(n) void* operator new[](usize n, const NoThrowT&)noexcept{
    return NWB::Core::Alloc::CoreAlloc(n);
}

void* operator new[](usize n, AlignValT alignment)noexcept(false){
    return __hidden_new::InternalOperatorNew(n, static_cast<usize>(alignment)).get();
}

void* operator new[](usize n, AlignValT alignment, const NoThrowT&)noexcept{
    return NWB::Core::Alloc::CoreAllocAligned(n, static_cast<usize>(alignment));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_ALLOC_NEW_ATTRIBUTES
#undef NWB_ALLOC_NEW_NOTHROW_ATTRIBUTES

#undef NWB_ALLOC_NODISCARD


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

