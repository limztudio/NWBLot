// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adaptor.h"

#include <global/termination.h>

#if defined(FOUNDATION_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(FOUNDATION_PLATFORM_LINUX) || defined(FOUNDATION_PLATFORM_APPLE)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ContainerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_container_adaptor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_DefaultCacheLineSize = 64u;

#if defined(FOUNDATION_PLATFORM_WINDOWS)
struct ProcessorInformationStorage final : NoCopy{
    HANDLE m_heap;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* m_data;


public:
    explicit ProcessorInformationStorage(const usize byteCount)noexcept
        : m_heap(GetProcessHeap())
        , m_data(m_heap ? static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(HeapAlloc(m_heap, 0u, byteCount)) : nullptr)
    {}
    ~ProcessorInformationStorage()noexcept{
        if(m_data && !HeapFree(m_heap, 0u, m_data))
            TerminateInvariant();
    }
};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CacheLineSizeProbe{
    usize m_largestLineSize = s_DefaultCacheLineSize;


public:
    CacheLineSizeProbe()noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        DWORD bufferSize = 0u;
        if(GetLogicalProcessorInformation(nullptr, &bufferSize) || GetLastError() != ERROR_INSUFFICIENT_BUFFER || bufferSize < sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION))
            return;

        const DWORD capacity = bufferSize;
        ProcessorInformationStorage info(static_cast<usize>(capacity));
        if(!info.m_data || !GetLogicalProcessorInformation(info.m_data, &bufferSize) || bufferSize > capacity || bufferSize % sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) != 0u)
            return;

        const usize elementCount = static_cast<usize>(bufferSize) / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        for(usize i = 0u; i < elementCount; ++i){
            if(info.m_data[i].Relationship != RelationCache)
                continue;

            const auto detectedLineSize = static_cast<usize>(info.m_data[i].Cache.LineSize);
            if(m_largestLineSize < detectedLineSize)
                m_largestLineSize = detectedLineSize;
        }
#elif defined(_SC_LEVEL1_DCACHE_LINESIZE)
        const Array<isize, 3u> lineSizes = {
            static_cast<isize>(sysconf(_SC_LEVEL1_DCACHE_LINESIZE)),
#if defined(_SC_LEVEL2_DCACHE_LINESIZE)
            static_cast<isize>(sysconf(_SC_LEVEL2_DCACHE_LINESIZE)),
#else
            static_cast<isize>(-1),
#endif
#if defined(_SC_LEVEL3_DCACHE_LINESIZE)
            static_cast<isize>(sysconf(_SC_LEVEL3_DCACHE_LINESIZE)),
#else
            static_cast<isize>(-1),
#endif
        };
        for(const isize lineSize : lineSizes){
            if(lineSize <= 0)
                continue;

            const auto detectedLineSize = static_cast<usize>(lineSize);
            if(m_largestLineSize < detectedLineSize)
                m_largestLineSize = detectedLineSize;
        }
#endif
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const CacheLineSizeProbe& GetCacheLineSizeProbe()noexcept{
    static const CacheLineSizeProbe s_CacheLineSizeProbe;
    return s_CacheLineSizeProbe;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AdaptorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize CacheLineSize()noexcept{ return __hidden_container_adaptor::GetCacheLineSizeProbe().m_largestLineSize; }

usize CacheLineAllocationAlignment(usize valueAlignment)noexcept{
    const usize cacheLineSize = CacheLineSize();
    return cacheLineSize > valueAlignment ? cacheLineSize : valueAlignment;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

