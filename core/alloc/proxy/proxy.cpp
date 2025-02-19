// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_UNIX) && (!defined(NWB_PLATFORM_ANDROID))
// include <bits/c++config.h> indirectly so that <cstdlib> is not included
#include <cstddef>
// include <features.h> indirectly so that <stdlib.h> is not included
#include <unistd.h>
// Working around compiler issue with Anaconda's gcc 7.3 compiler package.
// New gcc ported for old libc may provide their inline implementation
// of aligned_alloc as required by new C++ standard, this makes it hard to
// redefine aligned_alloc here. However, running on systems with new libc
// version, it still needs it to be redefined, thus tricking system headers
#if defined(__GLIBC_PREREQ)
#if !__GLIBC_PREREQ(2, 16) && _GLIBCXX_HAVE_ALIGNED_ALLOC
// tell <cstdlib> that there is no aligned_alloc
#undef _GLIBCXX_HAVE_ALIGNED_ALLOC
// trick <stdlib.h> to define another symbol instead
#define aligned_alloc __hidden_redefined_aligned_alloc
// Fix the state and undefine the trick
#include <cstdlib>
#undef aligned_alloc
#endif // !__GLIBC_PREREQ(2, 16) && _GLIBCXX_HAVE_ALIGNED_ALLOC
#endif // defined(__GLIBC_PREREQ)
#include <cstdlib>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "proxy.h"

#include "binder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB::Core::Alloc::Binder s_memBinder;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_WINDOWS)
/*** internal global operator new implementation (Linux, Windows) ***/
#include <new>

// Adds aliasing and copy attributes to function if available
#if defined(__has_attribute)
#if __has_attribute(__copy__)
#define __NWB_ALIAS_ATTR_COPY(name) __attribute__((alias (#name), __copy__(name)))
#endif
#endif

#ifndef __NWB_ALIAS_ATTR_COPY
#define __NWB_ALIAS_ATTR_COPY(name) __attribute__((alias (#name)))
#endif

#endif

#if defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_APPLE)
#ifndef __THROW
#define __THROW
#endif

/*** service functions and variables ***/
#include <string.h> // for memset
#include <unistd.h> // for sysconf

static long memoryPageSize;

static inline void initPageSize(){ memoryPageSize = sysconf(_SC_PAGESIZE); }

#if defined(NWB_PLATFORM_LINUX)
#include <dlfcn.h>
#include <malloc.h>    // mallinfo

/* __NWB_malloc_proxy used as a weak symbol by alloc_proxy for:
   1) detection that the proxy library is loaded
   2) check that dlsym("malloc") found something different from our replacement malloc
*/
extern "C" NWB_MEMPROXY_API void* __NWB_malloc_proxy(size_t) __NWB_ALIAS_ATTR_COPY(malloc);

static void* orig_msize;

#elif defined(NWB_PLATFORM_APPLE)
#include <AvailabilityMacros.h>
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <stdlib.h>

static kern_return_t enumerator(task_t, void*, unsigned, vm_address_t, memory_reader_t, vm_range_recorder_t){ return KERN_FAILURE; }

static size_t good_size(malloc_zone_t*, size_t size){ return size; }

/* Consistency checker */
static boolean_t zone_check(malloc_zone_t*){ return true; }

static void zone_print(malloc_zone_t*, boolean_t){}
static void zone_log(malloc_zone_t*, void*){}
static void zone_force_lock(malloc_zone_t*){}
static void zone_force_unlock(malloc_zone_t*){}

static void zone_statistics(malloc_zone_t*, malloc_statistics_t* s){
    s->blocks_in_use = 0;
    s->size_in_use = s->max_size_in_use = s->size_allocated = 0;
}

static boolean_t zone_locked(malloc_zone_t*){ return false; }

static boolean_t impl_zone_enable_discharge_checking(malloc_zone_t*){ return false; }

static void impl_zone_disable_discharge_checking(malloc_zone_t*){}
static void impl_zone_discharge(malloc_zone_t*, void*){}
static void impl_zone_destroy(struct _malloc_zone_t*){}

/* note: impl_malloc_usable_size() is called for each free() call, so it must be fast */
static size_t impl_malloc_usable_size(struct _malloc_zone_t*, const void* ptr){
    // malloc_usable_size() is used by macOS* to recognize which memory manager
    // allocated the address, so our wrapper must not redirect to the original function.
    return __nwb_core_msize(const_cast<void*>(ptr));
}

static void* impl_malloc(struct _malloc_zone_t*, size_t size);
static void* impl_calloc(struct _malloc_zone_t*, size_t num_items, size_t size);
static void* impl_valloc(struct _malloc_zone_t*, size_t size);
static void impl_free(struct _malloc_zone_t*, void* ptr);
static void* impl_realloc(struct _malloc_zone_t*, void* ptr, size_t size);
static void* impl_memalign(struct _malloc_zone_t*, size_t alignment, size_t size);

/* ptr is in zone and have reported size */
static void impl_free_definite_size(struct _malloc_zone_t*, void* ptr, size_t size){
    s_memBinder.safeFree(nullptr, ptr, size, "default replacement free");
}

/* Empty out caches in the face of memory pressure. */
static size_t impl_pressure_relief(struct _malloc_zone_t*, size_t  /* goal */){ return 0; }

static malloc_zone_t* system_zone = nullptr;

struct DoMallocReplacement{
    DoMallocReplacement(){
        static malloc_introspection_t introspect;
        memset(&introspect, 0, sizeof(malloc_introspection_t));
        static malloc_zone_t zone;
        memset(&zone, 0, sizeof(malloc_zone_t));

        introspect.enumerator = &enumerator;
        introspect.good_size = &good_size;
        introspect.check = &zone_check;
        introspect.print = &zone_print;
        introspect.log = zone_log;
        introspect.force_lock = &zone_force_lock;
        introspect.force_unlock = &zone_force_unlock;
        introspect.statistics = zone_statistics;
        introspect.zone_locked = &zone_locked;
        introspect.enable_discharge_checking = &impl_zone_enable_discharge_checking;
        introspect.disable_discharge_checking = &impl_zone_disable_discharge_checking;
        introspect.discharge = &impl_zone_discharge;

        zone.size = &impl_malloc_usable_size;
        zone.malloc = &impl_malloc;
        zone.calloc = &impl_calloc;
        zone.valloc = &impl_valloc;
        zone.free = &impl_free;
        zone.realloc = &impl_realloc;
        zone.destroy = &impl_zone_destroy;
        zone.zone_name = "NWBmalloc";
        zone.introspect = &introspect;
        zone.version = 8;
        zone.memalign = impl_memalign;
        zone.free_definite_size = &impl_free_definite_size;
        zone.pressure_relief = &impl_pressure_relief;

        // make sure that default purgeable zone is initialized
        malloc_default_purgeable_zone();
        void* ptr = malloc(1);
        // get all registered memory zones
        unsigned zcount = 0;
        malloc_zone_t** zone_array = nullptr;
        kern_return_t errorcode = malloc_get_all_zones(mach_task_self(), nullptr, (vm_address_t**)&zone_array, &zcount);
        if(!errorcode && zone_array && zcount > 0){
            // find the zone that allocated ptr
            for(unsigned i = 0; i < zcount; ++i){
                malloc_zone_t* z = zone_array[i];
                if(z && z->size(z, ptr) > 0){ // the right one is found
                    system_zone = z;
                    break;
                }
            }
        }
        free(ptr);

        malloc_zone_register(&zone);
        if(system_zone){
            // after unregistration of the system zone, the last registered (i.e. our) zone becomes the default
            malloc_zone_unregister(system_zone);
            // register the system zone back
            malloc_zone_register(system_zone);
        }
    }
};

static DoMallocReplacement doMallocReplacement;
#endif

// Original (i.e., replaced) functions,
// they are never changed for MALLOC_ZONE_OVERLOAD_ENABLED.
static void *orig_free, *orig_realloc;

#if defined(NWB_PLATFORM_LINUX)
#define ZONE_ARG
#define PREFIX(name) name

static void *orig_libc_free, *orig_libc_realloc;

// We already tried to find ptr to original functions.
static Atomic<bool> origFuncSearched{ false };

inline void InitOrigPointers(){
    // race is OK here, as different threads found same functions
    if(!origFuncSearched.load(std::memory_order_acquire)){
        orig_free = dlsym(RTLD_NEXT, "free");
        orig_realloc = dlsym(RTLD_NEXT, "realloc");
        orig_msize = dlsym(RTLD_NEXT, "malloc_usable_size");
        orig_libc_free = dlsym(RTLD_NEXT, "__libc_free");
        orig_libc_realloc = dlsym(RTLD_NEXT, "__libc_realloc");

        origFuncSearched.store(true, std::memory_order_release);
    }
}

/*** replacements for malloc and the family ***/
extern "C"{
#elif defined(NWB_PLATFORM_APPLE)

// each impl_* function has such 1st argument, it's unused
#define ZONE_ARG struct _malloc_zone_t *,
#define PREFIX(name) impl_##name
// not interested in original functions for zone overload
inline void InitOrigPointers(){}

#endif

NWB_MEMPROXY_API void* PREFIX(malloc)(ZONE_ARG size_t size)__THROW{
    if(!size)
        return nullptr;
    return s_memBinder.rawAlloc(0, size, "default replacement malloc");
}

NWB_MEMPROXY_API void* PREFIX(calloc)(ZONE_ARG size_t num, size_t size)__THROW{
    // it's square root of maximal size_t value
    const size_t mult_not_overflow = size_t(1) << (sizeof(size_t) * CHAR_BIT / 2);
    const size_t arraySize = nobj * size;

    // check for overflow during multiplication:
    if(nobj >= mult_not_overflow || size >= mult_not_overflow){ // 1) heuristic check
        if(nobj && arraySize / nobj != size){             // 2) exact check
            errno = ENOMEM;
            return nullptr;
        }
    }
    void* result = __nwb_core_alloc_memAlloc(0, size, "default replacement calloc");
    if(result)
        memset(result, 0, arraySize);
    else
        errno = ENOMEM;
    return result;
}

NWB_MEMPROXY_API void PREFIX(free)(ZONE_ARG void* object)__THROW{
    InitOrigPointers();
    s_memBinder.safeFree((void (*)(void*))orig_free, object, 0, "default replacement free");
}

NWB_MEMPROXY_API void* PREFIX(realloc)(ZONE_ARG void* ptr, size_t sz)__THROW{
    InitOrigPointers();
    return s_memBinder.safeRealloc(orig_realloc, ptr, 0, sz, "default replacement realloc");
}

/* The older *NIX interface for aligned allocations;
   it's formally substituted by posix_memalign and deprecated,
   so we do not expect it to cause cyclic dependency with C RTL. */
NWB_MEMPROXY_API void* PREFIX(memalign)(ZONE_ARG size_t alignment, size_t size)__THROW{
    if(!size)
        return nullptr;
    return __nwb_core_alloc_memAlloc(alignment, size, "default replacement memalign");
}

/* valloc allocates memory aligned on a page boundary */
NWB_MEMPROXY_API void* PREFIX(valloc)(ZONE_ARG size_t size)__THROW{
    if(!memoryPageSize)
        initPageSize();

    if(!size)
        return nullptr;
    return __nwb_core_alloc_memAlloc(memoryPageSize, size, "default replacement valloc");
}

#undef ZONE_ARG
#undef PREFIX

#if defined(NWB_PLATFORM_LINUX)

// match prototype from system headers
#if defined(NWB_PLATFORM_ANDROID)
NWB_MEMPROXY_API size_t malloc_usable_size(const void* ptr)__THROW
#else
NWB_MEMPROXY_API size_t malloc_usable_size(void* ptr)__THROW
#endif
{
    InitOrigPointers();
    if(ptr)
        return __nwb_core_msize(ptr);
    return 0;
}

NWB_MEMPROXY_API int posix_memalign(void** memptr, size_t alignment, size_t size)__THROW{
    if(0 != (alignment & (alignment - sizeof(void*))))
        return EINVAL;
    if(size){
        void* result = __nwb_core_alloc_memAlloc(alignment, size, "default replacement memalign");
        if(!result)
            return ENOMEM;
        *memptr = result;
    }
    else
        *memptr = nullptr;
    return 0;
}

/* pvalloc allocates smallest set of complete pages which can hold
   the requested number of bytes. Result is aligned on page boundary. */
NWB_MEMPROXY_API void* pvalloc(size_t size)__THROW{
    if(!memoryPageSize)
        initPageSize();
    // align size up to the page size,
    // pvalloc(0) returns 1 page, see man libmpatrol
    size = size ? ((size - 1) | (memoryPageSize - 1)) + 1 : memoryPageSize;

    return __nwb_core_alloc_memAlloc(memoryPageSize, size, "default replacement pvalloc");
}

NWB_MEMPROXY_API int mallopt(int /*param*/, int /*value*/)__THROW{
    return 1;
}

#if defined(__GLIBC__) || defined(NWB_PLATFORM_ANDROID)
NWB_MEMPROXY_API struct mallinfo mallinfo()__THROW{
    struct mallinfo m;
    memset(&m, 0, sizeof(struct mallinfo));

    return m;
}
#endif

#if defined(NWB_PLATFORM_ANDROID)
// Android doesn't have malloc_usable_size, provide it to be compatible
// with Linux, in addition overload dlmalloc_usable_size() that presented
// under Android.
NWB_MEMPROXY_API size_t dlmalloc_usable_size(const void* ptr) __NWB_ALIAS_ATTR_COPY(malloc_usable_size);
#else
// TODO: consider using __typeof__ to guarantee the correct declaration types
// C11 function, supported starting GLIBC 2.16
NWB_MEMPROXY_API void* aligned_alloc(size_t alignment, size_t size) __NWB_ALIAS_ATTR_COPY(memalign);
// Those non-standard functions are exported by GLIBC, and might be used
// in conjunction with standard malloc/free, so we must overload them.
// Bionic doesn't have them. Not removing from the linker scripts,
// as absent entry points are ignored by the linker.

NWB_MEMPROXY_API void* __libc_malloc(size_t size) __NWB_ALIAS_ATTR_COPY(malloc);
NWB_MEMPROXY_API void* __libc_calloc(size_t num, size_t size) __NWB_ALIAS_ATTR_COPY(calloc);
NWB_MEMPROXY_API void* __libc_memalign(size_t alignment, size_t size) __NWB_ALIAS_ATTR_COPY(memalign);
NWB_MEMPROXY_API void* __libc_pvalloc(size_t size) __NWB_ALIAS_ATTR_COPY(pvalloc);
NWB_MEMPROXY_API void* __libc_valloc(size_t size) __NWB_ALIAS_ATTR_COPY(valloc);

// call original __libc_* to support naive replacement of free via __libc_free etc
NWB_MEMPROXY_API void __libc_free(void* ptr){
    InitOrigPointers();
    s_memBinder.safeFree((void (*)(void*))orig_libc_free, ptr, 0, "default replacement free");
}

NWB_MEMPROXY_API void* __libc_realloc(void* ptr, size_t size){
    InitOrigPointers();
    return s_memBinder.safeRealloc(orig_libc_realloc, ptr, 0, size, "default replacement realloc");
}
#endif

}

/*** replacements for global operators new and delete ***/

NWB_MEMPROXY_API void* operator new(size_t sz){
    if(!sz)
        return nullptr;
    void* res = s_memBinder.rawAlloc(0, sz, "default replacement new");
    while(!res){
        std::new_handler handler = std::get_new_handler();
        if(handler)
            (*handler)();
        else
            throw std::bad_alloc();
        res = s_memBinder.rawAlloc(0, sz, "default replacement new");
    }
    return res;
}
NWB_MEMPROXY_API void* operator new[](size_t sz){
    if(!sz)
        return nullptr;
    void* res = s_memBinder.rawAlloc(0, sz, "default replacement new[]");
    while(!res){
        std::new_handler handler = std::get_new_handler();
        if(handler)
            (*handler)();
        else
            throw std::bad_alloc();
        res = s_memBinder.rawAlloc(0, sz, "default replacement new[]");
    }
    return res;
}
NWB_MEMPROXY_API void operator delete(void* ptr)noexcept{
    InitOrigPointers();
    s_memBinder.safeFree((void (*)(void*))orig_free, ptr, 0, "default replacement delete");
}
NWB_MEMPROXY_API void operator delete[](void* ptr)noexcept{
    InitOrigPointers();
    s_memBinder.safeFree((void (*)(void*))orig_free, ptr, 0, "default replacement delete[]");
}
NWB_MEMPROXY_API void* operator new(size_t sz, const std::nothrow_t&)noexcept{
    if(!sz)
        return nullptr;
    return s_memBinder.rawAlloc(0, sz, "default replacement new");
}
NWB_MEMPROXY_API void* operator new[](std::size_t sz, const std::nothrow_t&)noexcept{
    if(!sz)
        return nullptr;
    return s_memBinder.rawAlloc(0, sz, "default replacement new[]");
}
NWB_MEMPROXY_API void operator delete(void* ptr, const std::nothrow_t&)noexcept{
    InitOrigPointers();
    s_memBinder.safeFree((void (*)(void*))orig_free, ptr, 0, "default replacement delete(noexcept)");
}
NWB_MEMPROXY_API void operator delete[](void* ptr, const std::nothrow_t&)noexcept{
    InitOrigPointers();
    s_memBinder.safeFree((void (*)(void*))orig_free, ptr, 0, "default replacement delete[](noexcept)");
}

#endif
#endif

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>

#include <stdio.h>

#include "replacement.h"

template<typename T, size_t N> // generic function to find length of array
inline size_t arrayLength(const T(&)[N]){ return N; }

void* safer_aligned_malloc(usize align, usize len, const char* where){
    // workaround for "is power of 2 pow N" bug that accepts zeros
    return __nwb_core_alloc_memAlloc(align > sizeof(size_t*) ? align : sizeof(size_t*), len, where);
}

// we do not support _expand();
void* safer_expand(void*, size_t){
    return nullptr;
}

#define __NWB_ORIG_ALLOCATOR_REPLACEMENT_WRAPPER(CRTLIB)                                                                \
void (*orig_free_##CRTLIB)(void*);                                                                                      \
void __NWB_malloc_safer_free_##CRTLIB(void *ptr){                                                                       \
    s_memBinder.safeFree(orig_free_##CRTLIB, ptr, 0, "default replacement free("#CRTLIB")");                            \
}                                                                                                                       \
void (*orig__aligned_free_##CRTLIB)(void*);                                                                             \
void __NWB_malloc_safer__aligned_free_##CRTLIB(void *ptr){                                                              \
    s_memBinder.safeFree(orig__aligned_free_##CRTLIB, ptr, 0, "default replacement aligned_free("#CRTLIB")");           \
}                                                                                                                       \
size_t (*orig__msize_##CRTLIB)(void*);                                                                                  \
size_t __NWB_malloc_safer__msize_##CRTLIB(void *ptr){                                                                   \
    return __nwb_core_msize(ptr);                                                                                       \
}                                                                                                                       \
size_t (*orig__aligned_msize_##CRTLIB)(void*, size_t, size_t);                                                          \
size_t __NWB_malloc_safer__aligned_msize_##CRTLIB(void *ptr, size_t alignment, size_t offset){                          \
    return __nwb_core_msize(ptr);                                                                                       \
}                                                                                                                       \
void* __NWB_malloc_safer_realloc_##CRTLIB(void *ptr, size_t size){                                                      \
    orig_ptrs func_ptrs = {orig_free_##CRTLIB, orig__msize_##CRTLIB};                                                   \
    return s_memBinder.safeRealloc(&func_ptrs, ptr, 0, size, "default replacement realloc("#CRTLIB")");                 \
}                                                                                                                       \
void* __NWB_malloc_safer__aligned_realloc_##CRTLIB(void *ptr, size_t size, size_t alignment){                           \
    orig_aligned_ptrs func_ptrs = { orig__aligned_free_##CRTLIB, orig__aligned_msize_##CRTLIB };                        \
    return s_memBinder.safeRealloc(&func_ptrs, ptr, alignment, size, "default replacement aligned_realloc("#CRTLIB")"); \
}

#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning(push)
#pragma warning(disable : 4702)
#endif

// Only for ucrtbase: substitution for _o_free
void (*orig__o_free)(void*);
void __NWB_malloc__o_free(void* ptr){
    s_memBinder.safeFree(orig__o_free, ptr, 0, "default replacement o_free");
}
// Only for ucrtbase: substitution for _free_base
void(*orig__free_base)(void*);
void __NWB_malloc__free_base(void* ptr){
    s_memBinder.safeFree(orig__free_base, ptr, 0, "default replacement free_base");
}

#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning(pop)
#endif

// Size limit is MAX_PATTERN_SIZE (28) byte codes / 56 symbols per line.
// * can be used to match any digit in byte codes.
// # followed by several * indicate a relative address that needs to be corrected.
// Purpose of the pattern is to mark an instruction bound; it should consist of several
// full instructions plus one extra byte code. It's not required for the patterns
// to be unique (i.e., it's OK to have same pattern for unrelated functions).
// TODO: use hot patch prologues if exist
const char* known_bytecodes[] = {
#if _WIN64
//  "========================================================" - 56 symbols
    "E9********CCCC",         // multiple - jmp(0xE9) with address followed by empty space (0xCC - INT 3)
    "4883EC284885C974",       // release free()
    "4883EC284885C975",       // release _msize()
    "4885C974375348",         // release free() 8.0.50727.42, 10.0
    "C7442410000000008B",     // release free() ucrtbase.dll 10.0.14393.33
    "48895C24085748",         // release _aligned_msize() ucrtbase.dll 10.0.14393.33
    "48894C24084883EC28BA",   // debug prologue
    "4C894424184889542410",   // debug _aligned_msize() 10.0
    "48894C24084883EC2848",   // debug _aligned_free 10.0
    "488BD1488D0D#*******E9", // _o_free(), ucrtbase.dll
#else // _WIN32
//  "========================================================" - 56 symbols
    "8BFF558BEC8B",           // multiple
    "8BFF558BEC83",           // release free() & _msize() 10.0.40219.325, _msize() ucrtbase.dll
    "8BFF558BECFF",           // release _aligned_msize ucrtbase.dll
    "8BFF558BEC51",           // release free() & _msize() ucrtbase.dll 10.0.14393.33
    "558BEC8B450885C074",     // release _aligned_free 11.0
    "558BEC837D08000F",       // release _msize() 11.0.51106.1
    "558BEC837D08007419FF",   // release free() 11.0.50727.1
    "558BEC8B450885C075",     // release _aligned_msize() 11.0.50727.1
    "558BEC6A018B",           // debug free() & _msize() 11.0
    "558BEC8B451050",         // debug _aligned_msize() 11.0
    "558BEC8B450850",         // debug _aligned_free 11.0
    "8BFF558BEC6A",           // debug free() & _msize() 10.0.40219.325
#endif // _WIN64/_WIN32
    nullptr
    };

#define __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_ENTRY(CRT_VER,function_name,dbgsuffix) \
    ReplaceFunctionWithStore(NWB_TEXT(#CRT_VER #dbgsuffix ".dll"), #function_name, \
      (FUNCPTR)__NWB_malloc_safer_##function_name##_##CRT_VER##dbgsuffix, \
      known_bytecodes, (FUNCPTR*)&orig_##function_name##_##CRT_VER##dbgsuffix);

#define __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_ENTRY_NO_FALLBACK(CRT_VER,function_name,dbgsuffix) \
    ReplaceFunctionWithStore(NWB_TEXT(#CRT_VER #dbgsuffix ".dll"), #function_name, \
      (FUNCPTR)__NWB_malloc_safer_##function_name##_##CRT_VER##dbgsuffix, 0, nullptr);

#define __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_ENTRY_REDIRECT(CRT_VER,function_name,dest_func,dbgsuffix) \
    ReplaceFunctionWithStore(NWB_TEXT(#CRT_VER #dbgsuffix ".dll"), #function_name, \
      (FUNCPTR)__NWB_malloc_safer_##dest_func##_##CRT_VER##dbgsuffix, 0, nullptr);

#define __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_IMPL(CRT_VER,dbgsuffix)                             \
    if(BytecodesAreKnown(NWB_TEXT(#CRT_VER #dbgsuffix ".dll"))){                                  \
      __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_ENTRY(CRT_VER,free,dbgsuffix)                         \
      __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_ENTRY(CRT_VER,_msize,dbgsuffix)                       \
      __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_ENTRY_NO_FALLBACK(CRT_VER,realloc,dbgsuffix)          \
      __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_ENTRY(CRT_VER,_aligned_free,dbgsuffix)                \
      __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_ENTRY(CRT_VER,_aligned_msize,dbgsuffix)               \
      __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_ENTRY_NO_FALLBACK(CRT_VER,_aligned_realloc,dbgsuffix) \
    }                                                                                             \
    else                                                                                          \
        SkipReplacement(NWB_TEXT(#CRT_VER #dbgsuffix ".dll"));

#define __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_RELEASE(CRT_VER) __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_IMPL(CRT_VER,)
#define __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_DEBUG(CRT_VER) __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_IMPL(CRT_VER,d)

#define __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL(CRT_VER)     \
    __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_RELEASE(CRT_VER) \
    __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_DEBUG(CRT_VER)

#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning(push)
#pragma warning(disable : 4100)
#pragma warning(disable : 4702)
#endif

__NWB_ORIG_ALLOCATOR_REPLACEMENT_WRAPPER(msvcr100d);
__NWB_ORIG_ALLOCATOR_REPLACEMENT_WRAPPER(msvcr100);
__NWB_ORIG_ALLOCATOR_REPLACEMENT_WRAPPER(msvcr110d);
__NWB_ORIG_ALLOCATOR_REPLACEMENT_WRAPPER(msvcr110);
__NWB_ORIG_ALLOCATOR_REPLACEMENT_WRAPPER(msvcr120d);
__NWB_ORIG_ALLOCATOR_REPLACEMENT_WRAPPER(msvcr120);
__NWB_ORIG_ALLOCATOR_REPLACEMENT_WRAPPER(ucrtbase);

#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning(pop)
#endif

/*** replacements for global operators new and delete ***/

#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning(push)
#pragma warning(disable : 4290)
#endif

static void* replace_malloc(size_t size){
    if(!size)
        return nullptr;
    return s_memBinder.rawAlloc(0, size, "default replacement replace_malloc");
}
static void* replace_calloc(size_t size){
    if(!size)
        return nullptr;
    void* result = s_memBinder.rawAlloc(0, size, "default replacement replace_calloc");
    if(result)
        memset(result, 0, size);
    return result;
}

/*** operator new overloads internals (Linux, Windows) ***/

void* operator_new(size_t sz){
    if(!sz)
        return nullptr;
    void* res = s_memBinder.rawAlloc(0, sz, "default replacement operator_new");
    while(!res){
        std::new_handler handler = std::get_new_handler();
        if(handler)
            (*handler)();
        else
            throw std::bad_alloc();
        res = s_memBinder.rawAlloc(0, sz, "default replacement operator_new");
    }
    return res;
}
void* operator_new_arr(size_t sz){
    if(!sz)
        return nullptr;
    void* res = s_memBinder.rawAlloc(0, sz, "default replacement operator_new_arr");
    while(!res){
        std::new_handler handler = std::get_new_handler();
        if(handler)
            (*handler)();
        else
            throw std::bad_alloc();
        res = s_memBinder.rawAlloc(0, sz, "default replacement operator_new_arr");
    }
    return res;
}
void operator_delete(void* ptr)noexcept{
    if(!ptr)
        return;
    s_memBinder.safeFree(nullptr, ptr, 0, "default replacement operator_delete");
}
#if _MSC_VER && !defined(__INTEL_COMPILER)
#pragma warning(pop)
#endif

void operator_delete_arr(void* ptr)noexcept{
    if(!ptr)
        return;
    s_memBinder.safeFree(nullptr, ptr, 0, "default replacement operator_delete_arr");
}
void* operator_new_t(size_t sz, const std::nothrow_t&)noexcept{
    if(!sz)
        return nullptr;
    return s_memBinder.rawAlloc(0, sz, "default replacement operator_new_t");
}
void* operator_new_arr_t(std::size_t sz, const std::nothrow_t&)noexcept{
    if(!sz)
        return nullptr;
    return s_memBinder.rawAlloc(0, sz, "default replacement operator_new_arr_t");
}
void operator_delete_t(void* ptr, const std::nothrow_t&)noexcept{
    if(!ptr)
        return;
    s_memBinder.safeFree(nullptr, ptr, 0, "default replacement operator_delete_t");
}
void operator_delete_arr_t(void* ptr, const std::nothrow_t&)noexcept{
    if(!ptr)
        return;
    s_memBinder.safeFree(nullptr, ptr, 0, "default replacement operator_delete_arr_t");
}

struct Module{
    const tchar* name;
    bool         doFuncReplacement; // do replacement in the DLL
};

Module modules_to_replace[] = {
    { NWB_TEXT("msvcr100d.dll"), true },
    { NWB_TEXT("msvcr100.dll"), true },
    { NWB_TEXT("msvcr110d.dll"), true },
    { NWB_TEXT("msvcr110.dll"), true },
    { NWB_TEXT("msvcr120d.dll"), true },
    { NWB_TEXT("msvcr120.dll"), true },
    { NWB_TEXT("ucrtbase.dll"), true },
    };

/*
We need to replace following functions:
malloc
calloc
_aligned_malloc
_expand (by dummy implementation)
??2@YAPAXI@Z      operator new                         (ia32)
??_U@YAPAXI@Z     void * operator new[] (size_t size)  (ia32)
??3@YAXPAX@Z      operator delete                      (ia32)
??_V@YAXPAX@Z     operator delete[]                    (ia32)
??2@YAPEAX_K@Z    void * operator new(unsigned __int64)   (intel64)
??_V@YAXPEAX@Z    void * operator new[](unsigned __int64) (intel64)
??3@YAXPEAX@Z     operator delete                         (intel64)
??_V@YAXPEAX@Z    operator delete[]                       (intel64)
??2@YAPAXIABUnothrow_t@std@@@Z      void * operator new (size_t sz, const std::nothrow_t&) noexcept  (optional)
??_U@YAPAXIABUnothrow_t@std@@@Z     void * operator new[] (size_t sz, const std::nothrow_t&) noexcept (optional)

and these functions have runtime-specific replacement:
realloc
free
_msize
_aligned_realloc
_aligned_free
_aligned_msize
*/

typedef struct FRData_t{
    //char *_module;
    const char *_func;
    FUNCPTR _fptr;
    FRR_ON_ERROR _on_error;
} FRDATA;

FRDATA c_routines_to_replace[] = {
    { "malloc",  (FUNCPTR)replace_malloc, FRR_FAIL },
    { "calloc",  (FUNCPTR)replace_calloc, FRR_FAIL },
    { "_aligned_malloc",  (FUNCPTR)safer_aligned_malloc, FRR_FAIL },
    { "_expand",  (FUNCPTR)safer_expand, FRR_IGNORE },
};

FRDATA cxx_routines_to_replace[] = {
#if _WIN64
    { "??2@YAPEAX_K@Z", (FUNCPTR)operator_new, FRR_FAIL },
    { "??_U@YAPEAX_K@Z", (FUNCPTR)operator_new_arr, FRR_FAIL },
    { "??3@YAXPEAX@Z", (FUNCPTR)operator_delete, FRR_FAIL },
    { "??_V@YAXPEAX@Z", (FUNCPTR)operator_delete_arr, FRR_FAIL },
#else
    { "??2@YAPAXI@Z", (FUNCPTR)operator_new, FRR_FAIL },
    { "??_U@YAPAXI@Z", (FUNCPTR)operator_new_arr, FRR_FAIL },
    { "??3@YAXPAX@Z", (FUNCPTR)operator_delete, FRR_FAIL },
    { "??_V@YAXPAX@Z", (FUNCPTR)operator_delete_arr, FRR_FAIL },
#endif
    { "??2@YAPAXIABUnothrow_t@std@@@Z", (FUNCPTR)operator_new_t, FRR_IGNORE },
    { "??_U@YAPAXIABUnothrow_t@std@@@Z", (FUNCPTR)operator_new_arr_t, FRR_IGNORE }
};

#ifndef NWB_UNICODE
#define WCHAR_SPEC "%s"
#define STR_CMP strcmp
#else
#define WCHAR_SPEC "%ls"
#define STR_CMP wcscmp
#endif

// Check that we recognize bytecodes that should be replaced by trampolines.
// If some functions have unknown prologue patterns, replacement should not be done.
bool BytecodesAreKnown(const tchar* dllName){
    const char* funcName[] = { "free", "_msize", "_aligned_free", "_aligned_msize", 0 };
    HMODULE module = GetModuleHandle(dllName);

    if(!module)
        return false;
    for(int i = 0; funcName[i]; ++i){
        if(!IsPrologueKnown(dllName, funcName[i], known_bytecodes, module)){
            fprintf(stderr, "NWBmalloc: skip allocation functions replacement in " WCHAR_SPEC ": unknown prologue for function %s\n", dllName, funcName[i]);
            return false;
        }
    }
    return true;
}

void SkipReplacement(const tchar* dllName){
    for(size_t i = 0; i < arrayLength(modules_to_replace); ++i){
        if(!STR_CMP(modules_to_replace[i].name, dllName)){
            modules_to_replace[i].doFuncReplacement = false;
            break;
        }
    }
}

void ReplaceFunctionWithStore(const tchar* dllName, const char* funcName, FUNCPTR newFunc, const char** opcodes, FUNCPTR* origFunc, FRR_ON_ERROR on_error = FRR_FAIL){
    FRR_TYPE res = ReplaceFunction(dllName, funcName, newFunc, opcodes, origFunc);

    if(res == FRR_OK || res == FRR_NODLL || (res == FRR_NOFUNC && on_error == FRR_IGNORE))
        return;

    fprintf(stderr, "Failed to %s function %s in module " WCHAR_SPEC "\n", res == FRR_NOFUNC ? "find" : "replace", funcName, dllName);

    // Unable to replace a required function
    // Aborting because incomplete replacement of memory management functions
    // may leave the program in an invalid state
    abort();
}

void doMallocReplacement(){
    // Replace functions and keep backup of original code (separate for each runtime)
    __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL(msvcr100)
    __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL(msvcr110)
    __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL(msvcr120)
    __NWB_ORIG_ALLOCATOR_REPLACEMENT_CALL_RELEASE(ucrtbase)

    // Replace functions without storing original code
    for(size_t j = 0; j < arrayLength(modules_to_replace); ++j){
        if(!modules_to_replace[j].doFuncReplacement)
            continue;
        for(size_t i = 0; i < arrayLength(c_routines_to_replace); ++i)
            ReplaceFunctionWithStore(modules_to_replace[j].name, c_routines_to_replace[i]._func, c_routines_to_replace[i]._fptr, nullptr, nullptr, c_routines_to_replace[i]._on_error);
        if(STR_CMP(modules_to_replace[j].name, NWB_TEXT("ucrtbase.dll")) == 0){
            HMODULE ucrtbase_handle = GetModuleHandle(NWB_TEXT("ucrtbase.dll"));
            if(!ucrtbase_handle)
                continue;
            // If _o_free function is present and patchable, redirect it to nwbmalloc as well
            // This prevents issues with other _o_* functions which might allocate memory with malloc
            if(IsPrologueKnown(NWB_TEXT("ucrtbase.dll"), "_o_free", known_bytecodes, ucrtbase_handle))
                ReplaceFunctionWithStore(NWB_TEXT("ucrtbase.dll"), "_o_free", (FUNCPTR)__NWB_malloc__o_free, known_bytecodes, (FUNCPTR*)&orig__o_free, FRR_FAIL);
            // Similarly for _free_base
            if(IsPrologueKnown(NWB_TEXT("ucrtbase.dll"), "_free_base", known_bytecodes, ucrtbase_handle))
                ReplaceFunctionWithStore(NWB_TEXT("ucrtbase.dll"), "_free_base", (FUNCPTR)__NWB_malloc__free_base, known_bytecodes, (FUNCPTR*)&orig__free_base, FRR_FAIL);
            // ucrtbase.dll does not export operator new/delete, so skip the rest of the loop.
            continue;
        }

        for(size_t i = 0; i < arrayLength(cxx_routines_to_replace); ++i){
#if !_WIN64
            // in Microsoft* Visual Studio* 2012 and 2013 32-bit operator delete consists of 2 bytes only: short jump to free(ptr);
            // replacement should be skipped for this particular case.
            if(((STR_CMP(modules_to_replace[j].name, NWB_TEXT("msvcr110.dll")) == 0) || (STR_CMP(modules_to_replace[j].name, NWB_TEXT("msvcr120.dll")) == 0)) && (STR_CMP(cxx_routines_to_replace[i]._func, NWB_TEXT("??3@YAXPAX@Z")) == 0))
                continue;
            // in Microsoft* Visual Studio* 2013 32-bit operator delete[] consists of 2 bytes only: short jump to free(ptr);
            // replacement should be skipped for this particular case.
            if((STR_CMP(modules_to_replace[j].name, NWB_TEXT("msvcr120.dll")) == 0) && (STR_CMP(cxx_routines_to_replace[i]._func, NWB_TEXT("??_V@YAXPAX@Z")) == 0))
                continue;
#endif
            ReplaceFunctionWithStore(modules_to_replace[j].name, cxx_routines_to_replace[i]._func, cxx_routines_to_replace[i]._fptr, nullptr, nullptr, cxx_routines_to_replace[i]._on_error);
        }
    }
}

#endif

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
// Suppress warning for UWP build ('main' signature found without threading model)
#pragma warning(push)
#pragma warning(disable:4447)
#endif

extern "C" BOOL WINAPI DllMain(HINSTANCE hInst, DWORD callReason, LPVOID reserved){
    if(callReason == DLL_PROCESS_ATTACH && reserved && hInst)
        doMallocReplacement();

    return TRUE;
}

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma warning(pop)
#endif

// Just to make the linker happy and link the DLL to the application
extern "C" __declspec(dllexport) void __NWB_malloc_proxy(){}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

