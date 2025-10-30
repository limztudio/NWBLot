// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../common.h"
#include "config.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GPUTimestamp{
    u32 begin = 0;
    u32 end = 0;

    f64 msElapsed;

    u16 parentIndex;
    u16 depth;

    u32 color;
    u32 frameIndex;

    const char* name;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <template <typename> typename Allocator>
class GPUTimestampPool{
public:
    GPUTimestampPool(){}
    GPUTimestampPool(Allocator<u8> allocator)
        : m_allocator(Move(allocator))
    {}

    virtual ~GPUTimestampPool(){ cleanup(); }


public:
    void init(u16 queriesPerFrame, u16 maxFrames){
        m_queriesPerFrame = queriesPerFrame;

        constexpr u32 DataperQuery = 2;

        NWB_ASSERT(!m_timestamps && !m_data);
        m_timestamps = m_allocator.allocate<GPUTimestamp>(maxFrames * m_queriesPerFrame);
        m_data = m_allocator.allocate<u64>(maxFrames * m_queriesPerFrame * DataperQuery);
        NWB_ASSERT(m_timestamps && m_data);

#if defined(NWB_DEBUG)
        m_maxQueriesPerFrame = queriesPerFrame;
#endif

        reset();
    }

    void cleanup(){
        if(m_timestamps){
            m_allocator.deallocate(m_timestamps, m_queriesPerFrame * Config::MaxFrames);
            m_timestamps = nullptr;
        }
        if(m_data){
            m_allocator.deallocate(m_data, m_queriesPerFrame * Config::MaxFrames * 2);
            m_data = nullptr;
        }
    }

    void reset(){
        m_currentQuery = 0;
        m_parentIndex = 0;
        m_depth = 0;
        m_currentFrameResolved = false;
    }

    bool hasValidQueries()const{ return (m_currentQuery > 0) && (m_depth == 0); }

    u32 resolve(u32 currentFrame, GPUTimestamp* outTimeStamp)const{
        NWB_ASSERT(currentFrame < m_maxQueriesPerFrame);
        MWB_MEMCPY(outTimeStamp, &m_timestamps[currentFrame * m_queriesPerFrame], sizeof(GPUTimestamp) * m_currentQuery);
        return m_currentQuery;
    }

    u32 push(u32 currentFrame, const char* name){
        const u32 queryIndex = (currentFrame * m_queriesPerFrame) + m_currentQuery;

        NWB_ASSERT(queryIndex < (m_maxQueriesPerFrame * m_queriesPerFrame));
        GPUTimestamp& timestamp = m_timestamps[queryIndex];
        {
            timestamp.parentIndex = static_cast<decltype(timestamp.parentIndex)>(m_parentIndex);
            timestamp.begin = queryIndex * 2;
            timestamp.end = timestamp.begin + 1;
            timestamp.name = name;
            timestamp.depth = static_cast<decltype(timestamp.depth)>(m_depth++);
        }

        m_parentIndex = m_currentQuery++;
        return (queryIndex * 2);
    }

    u32 pop(u32 currentFrame){
        const u32 queryIndex = (currentFrame * m_queriesPerFrame) + m_parentIndex;

        NWB_ASSERT(queryIndex < (m_maxQueriesPerFrame * m_queriesPerFrame));
        const GPUTimestamp& timestamp = m_timestamps[queryIndex];

        --m_depth;

        m_parentIndex = timestamp.parentIndex;
        return (queryIndex * 2) + 1;
    }


private:
    Allocator<u8> m_allocator;

private:
    GPUTimestamp* m_timestamps = nullptr;
    u64* m_data = nullptr;

#if defined(NWB_DEBUG)
private:
    u16 m_maxQueriesPerFrame = 0;
#endif

private:
    u32 m_queriesPerFrame = 0;
    u32 m_currentQuery = 0;
    u32 m_parentIndex = 0;
    u32 m_depth = 0;

    bool m_currentFrameResolved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

