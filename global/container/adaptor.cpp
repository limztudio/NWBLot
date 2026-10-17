// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "adaptor.h"

#include <global/termination.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_APPLE)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ContainerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_container_adaptor{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_DefaultCachelineSize = 64u;

#if defined(NWB_PLATFORM_WINDOWS)
struct ProcessorInformationStorage final{
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
    ProcessorInformationStorage(const ProcessorInformationStorage&) = delete;


public:
    ProcessorInformationStorage& operator=(const ProcessorInformationStorage&) = delete;
};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CacheSize{
    usize m_size = s_DefaultCachelineSize;


public:
    CacheSize()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
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

            const auto cur = static_cast<usize>(info.m_data[i].Cache.LineSize);
            if(m_size < cur)
                m_size = cur;
        }
#elif defined(_SC_LEVEL1_DCACHE_LINESIZE)
        const isize lineSizes[] = {
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

            const auto cur = static_cast<usize>(lineSize);
            if(m_size < cur)
                m_size = cur;
        }
#endif
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const CacheSize& GetCacheSize()noexcept{
    static const CacheSize s_CacheSize;
    return s_CacheSize;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AdaptorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize CachelineSize()noexcept{ return __hidden_container_adaptor::GetCacheSize().m_size; }

usize CacheAlignedAlignment(usize valueAlignment)noexcept{
    const usize cachelineSize = CachelineSize();
    return cachelineSize > valueAlignment ? cachelineSize : valueAlignment;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

