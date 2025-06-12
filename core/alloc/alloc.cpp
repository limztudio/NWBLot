// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "alloc.h"
#include <core/common/common.h>

#include <cstddef>
#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_APPLE)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_alloc{
    struct CacheSize : Core::Common::Initializerable{
        bool initialize()override{
#if defined(_WIN32)
            DWORD bufferSize = 0;
            GetLogicalProcessorInformation(nullptr, &bufferSize);

            UniquePtr<u8[]> buffer(new u8[bufferSize]);
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(buffer.get());

            if(GetLogicalProcessorInformation(info, &bufferSize)){
                for(DWORD i = 0, e = bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); i < e; ++i){
                    if(info[i].Relationship != RelationCache)
                        continue;

                    const auto cur = static_cast<usize>(info[i].Cache.LineSize);
                    if(size < cur)
                        size = cur;
                }
            }
#elif defined(_SC_LEVEL1_DCACHE_LINESIZE)
            auto lineSizes[] = {
                sysconf(_SC_LEVEL1_DCACHE_LINESIZE),
                sysconf(_SC_LEVEL2_DCACHE_LINESIZE),
                sysconf(_SC_LEVEL3_DCACHE_LINESIZE),
            };
            for(auto lineSize : lineSizes){
                if(lineSize <= 0)
                    continue;

                const auto cur = static_cast<usize>(lineSize);
                if(size < cur)
                    size = cur;
            }
#endif
            return true;
        }
        void finalize()override{}

        usize size = 64;
    } static cacheSize;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize CachelineSize(){ return __hidden_alloc::cacheSize.size; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

