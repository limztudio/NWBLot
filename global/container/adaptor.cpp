// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/container/adaptor.h>

#include <mutex>
#include <vector>
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

struct CacheSize{
    usize m_size = s_DefaultCachelineSize;

    void initialize(){
#if defined(NWB_PLATFORM_WINDOWS)
        DWORD bufferSize = 0;
        GetLogicalProcessorInformation(nullptr, &bufferSize);
        if(bufferSize == 0)
            return;

        const DWORD elementCount = (bufferSize + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) - 1) / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(elementCount);
        if(GetLogicalProcessorInformation(info.data(), &bufferSize)){
            for(DWORD i = 0, e = bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); i < e; ++i){
                if(info[i].Relationship != RelationCache)
                    continue;

                const auto cur = static_cast<usize>(info[i].Cache.LineSize);
                if(m_size < cur)
                    m_size = cur;
            }
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
} static s_CacheSize;

static std::once_flag s_CacheSizeOnce;

CacheSize& GetCacheSize(){
    std::call_once(s_CacheSizeOnce, [](){
        s_CacheSize.initialize();
    });
    return s_CacheSize;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AdaptorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize CachelineSize(){ return __hidden_container_adaptor::GetCacheSize().m_size; }

usize CacheAlignedAlignment(usize valueAlignment)noexcept{
    const usize cachelineSize = CachelineSize();
    return cachelineSize > valueAlignment ? cachelineSize : valueAlignment;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

