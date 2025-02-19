// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include "../alloc.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Binder{
public:
    static inline void* rawAlloc(usize align, usize len, const char* where){ return __nwb_core_alloc_memAlloc(align, len, where); }
    static inline void* rawRealloc(void* memblock, usize align, usize len, const char* where){ return __nwb_core_alloc_memRealloc(memblock, align, len, where); }
    static inline void rawFree(void* ptr, usize len, const char* where)noexcept{ return __nwb_core_alloc_memFree(ptr, len, where); }
    static inline usize rawMsize(void* ptr)noexcept{ return __nwb_core_msize(ptr); }


public:
    inline void* safeRealloc(void* orgFunc, void* memblock, usize align, usize len, const char* where){
        void* output = nullptr;
        if(!memblock)
            output = rawAlloc(align, len, where);
        else if(isInitialized()){
            if(!len){
                rawFree(memblock, 0, where);
                return nullptr;
            }
            else
                output = rawRealloc(memblock, align, len, where);
        }
        else if(orgFunc && len){
            auto* orgPtrs = static_cast<orig_ptrs*>(orgFunc);
            if(orgPtrs->msize){
                auto oldLen = orgPtrs->msize(memblock);
                output = rawAlloc(align, len, where);
                if(output){
                    auto curLen = len < oldLen ? len : oldLen;
                    NWB_MEMCPY(output, curLen, memblock, curLen);
                    if(orgPtrs->free)
                        orgPtrs->free(memblock);
                }
            }
        }
        if(!output)
            errno = ENOMEM;
        return output;
    }

    inline void safeFree(void (*orgFunc)(void*), void* ptr, usize len, const char* where)noexcept{
        if(!ptr)
            return;

        if(isInitialized()){
            rawFree(ptr, len, where);
            return;
        }

        if(orgFunc)
            orgFunc(ptr);
    }


public:
    inline bool isInitialized()const noexcept{ return m_initialized.load(MemoryOrder::memory_order_acquire) != 0; }


private:
    Atomic<char> m_initialized{0};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

