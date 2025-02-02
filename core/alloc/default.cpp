// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "alloc.h"

#include <cstdlib>
#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#define _DEF_ALIGN MEMORY_ALLOCATION_ALIGNMENT
#elif (defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_APPLE))
#include <unistd.h>
#define _DEF_ALIGN sysconf(_SC_PAGESIZE)
#else
#define _DEF_ALIGN alignof(std::max_align_t)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* operator new(std::size_t size){
    return NWB::Core::Alloc::memAlloc(_DEF_ALIGN, size, "default: operator new");
}
void operator delete(void* ptr) noexcept{
    NWB::Core::Alloc::memFree(ptr, 0, "default: operator delete");
}
void* operator new[](std::size_t size){
    return NWB::Core::Alloc::memAlloc(_DEF_ALIGN, size, "default: operator new[]");
}
void operator delete[](void* ptr) noexcept{
    NWB::Core::Alloc::memFree(ptr, 0, "default: operator delete[]");
}

void* operator new(std::size_t size, std::align_val_t alignment){
    return NWB::Core::Alloc::memAlloc(static_cast<size_t>(alignment), size, "default: operator new (aligned)");
}
void operator delete(void* ptr, std::align_val_t alignment) noexcept{
    NWB::Core::Alloc::memFree(ptr, 0, "default: operator delete (aligned)");
}
void* operator new[](std::size_t size, std::align_val_t alignment){
    return NWB::Core::Alloc::memAlloc(static_cast<size_t>(alignment), size, "default: operator new[] (aligned)");
}
void operator delete[](void* ptr, std::align_val_t alignment)noexcept{
    NWB::Core::Alloc::memFree(ptr, 0, "default: operator delete[] (aligned)");
}

void operator delete(void* ptr, std::size_t size)noexcept{
    NWB::Core::Alloc::memFree(ptr, size, "default: operator delete (sized)");
}
void operator delete[](void* ptr, std::size_t size)noexcept{
    NWB::Core::Alloc::memFree(ptr, size, "default: operator delete[] (sized)");
}
void operator delete(void* ptr, std::size_t size, std::align_val_t alignment)noexcept{
    NWB::Core::Alloc::memFree(ptr, size, "default: operator delete (aligned, sized)");
}
void operator delete[](void* ptr, std::size_t size, std::align_val_t alignment)noexcept{
    NWB::Core::Alloc::memFree(ptr, size, "default: operator delete[] (aligned, sized)");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern "C"{
    void* malloc(std::size_t size){
        return NWB::Core::Alloc::memAlloc(_DEF_ALIGN, size, "default: malloc");
    }
    void* calloc(std::size_t num, std::size_t size){
        const auto totalSize = num * size;
        auto* ptr = NWB::Core::Alloc::memAlloc(alignof(std::max_align_t), totalSize, "default: calloc");
        if(ptr)
            std::memset(ptr, 0, totalSize);
        return ptr;
    }
    void* realloc(void* ptr, std::size_t size){
        if(ptr){
            if(!size){
                NWB::Core::Alloc::memFree(ptr, 0, "default: free(realloc)");
                return nullptr;
            }
        }
        else
            return NWB::Core::Alloc::memAlloc(_DEF_ALIGN, size, "default: realloc");
        auto* newPtr = NWB::Core::Alloc::memAlloc(_DEF_ALIGN, size, "default: realloc");
        if(newPtr && ptr){
            NWB_MEMCPY(newPtr, size, ptr, size);
            NWB::Core::Alloc::memFree(ptr, 0, "default: free(realloc)");
        }
        return newPtr;
    }
    void free(void* ptr){
        NWB::Core::Alloc::memFree(ptr, 0, "default: free");
    }

#if defined(_MSC_VER)
    void* _aligned_malloc(std::size_t size, std::size_t alignment){
        return NWB::Core::Alloc::memAlloc(alignment, size, "default: _aligned_malloc");
    }
    void* _aligned_realloc(void* ptr, std::size_t size, std::size_t alignment){
        if(ptr){
            if(!size){
                NWB::Core::Alloc::memFree(ptr, 0, "default: free(_aligned_realloc)");
                return nullptr;
            }
        }
        else
            return NWB::Core::Alloc::memAlloc(alignment, size, "default: _aligned_realloc");
        auto* newPtr = NWB::Core::Alloc::memAlloc(alignment, size, "default: _aligned_realloc");
        if(newPtr && ptr){
            NWB_MEMCPY(newPtr, size, ptr, size);
            NWB::Core::Alloc::memFree(ptr, 0, "default: free(_aligned_realloc)");
        }
        return newPtr;
    }
    void _aligned_free(void* ptr){
        NWB::Core::Alloc::memFree(ptr, 0, "default: _aligned_free");
    }
#endif
#if defined(__clang__) || defined(__GNUC__)
    int posix_memalign(void** memptr, std::size_t alignment, std::size_t size){
        auto* ptr = NWB::Core::Alloc::memAlloc(alignment, size, "default: posix_memalign");
        if(!ptr)
            return ENOMEM;
        *memptr = ptr;
        return 0;
    }
    void* aligned_alloc(std::size_t alignment, std::size_t size){
        return memAlloc(NWB::Core::Alloc::alignment, size, "default: aligned_alloc");
    }
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef _DEF_ALIGN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

