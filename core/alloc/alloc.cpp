// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "alloc.h"

#include <memory.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
#define MEM_TRACK
#endif

#ifdef MEM_TRACK
#include <global/containers.h>
#include <unordered_set>
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef MEM_TRACK
static MallocMutex s_memTrackMtx;
static std::unordered_set<void*> s_assignedMemoryChunks;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DLL_API void* __nwb_core_alloc_memAlloc(usize align, usize len, const char* where){
    if(!align)
        align = sizeof(void*);

#ifdef MEM_TRACK
    MallocMutex::scoped_lock lock(NWB::Core::Alloc::s_memTrackMtx);
#endif

    auto* block = _aligned_malloc(len, align);
    if(!block)
        return nullptr;

#ifdef MEM_TRACK
    NWB::Core::Alloc::s_assignedMemoryChunks.emplace(block);
#endif

    return block;
}
NWB_DLL_API void* __nwb_core_alloc_memRealloc(void* memblock, usize align, usize len, const char* where){
    if(!align)
        align = sizeof(void*);

#ifdef MEM_TRACK
    MallocMutex::scoped_lock lock(NWB::Core::Alloc::s_memTrackMtx);
#endif

    auto* block = _aligned_realloc(memblock, len, align);
    if(!block)
        return nullptr;

#ifdef MEM_TRACK
    NWB::Core::Alloc::s_assignedMemoryChunks.emplace(block);
#endif

    return block;
}
NWB_DLL_API void __nwb_core_alloc_memFree(void* ptr, usize len, const char* where)noexcept{
#ifdef MEM_TRACK
    MallocMutex::scoped_lock lock(NWB::Core::Alloc::s_memTrackMtx);
#endif

#ifdef MEM_TRACK
    auto f = NWB::Core::Alloc::s_assignedMemoryChunks.find(ptr);
    if(f == NWB::Core::Alloc::s_assignedMemoryChunks.end()){
        assert(false && "ptr is never been assigned");
        return;
    }
    NWB::Core::Alloc::s_assignedMemoryChunks.erase(f);
#endif

    _aligned_free(ptr);
}
NWB_DLL_API usize __nwb_core_msize(void* ptr)noexcept{
#ifdef MEM_TRACK
    MallocMutex::scoped_lock lock(NWB::Core::Alloc::s_memTrackMtx);
#endif

#ifdef MEM_TRACK
    auto f = NWB::Core::Alloc::s_assignedMemoryChunks.find(ptr);
    if(f == NWB::Core::Alloc::s_assignedMemoryChunks.end()){
        assert(false && "ptr is never been assigned");
        return static_cast<usize>(-1);
    }
#endif

    return _msize(ptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef MEM_TRACK
#undef MEM_TRACK
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

