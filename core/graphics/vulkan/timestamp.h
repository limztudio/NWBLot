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


template <typename Arena>
class GPUTimestampPool{
private:
    static constexpr const u32 s_DataPerQuery = 2;

    
public:
    GPUTimestampPool(Arena& arena)
        : m_arena(arena)
    {}

    virtual ~GPUTimestampPool(){ cleanup(); }


public:
    void init(u16 queriesPerFrame, u16 maxFrames){
        m_queriesPerFrame = queriesPerFrame;
        m_maxFrames = maxFrames;

        NWB_ASSERT(!m_timestamps && !m_data);
        m_timestamps = m_arena.template allocate<GPUTimestamp>(m_maxFrames * m_queriesPerFrame);
        m_data = m_arena.template allocate<u64>(m_maxFrames * m_queriesPerFrame * s_DataPerQuery);
        NWB_ASSERT(m_timestamps && m_data);

        reset();
    }

    void cleanup(){
        if(m_timestamps){
            m_arena.template deallocate<GPUTimestamp>(m_timestamps, m_maxFrames * m_queriesPerFrame);
            m_timestamps = nullptr;
        }
        if(m_data){
            m_arena.template deallocate<u64>(m_data, m_maxFrames * m_queriesPerFrame * s_DataPerQuery);
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
        NWB_ASSERT(currentFrame < m_maxFrames);
        NWB_MEMCPY(outTimeStamp, sizeof(GPUTimestamp) * m_currentQuery, &m_timestamps[currentFrame * m_queriesPerFrame], sizeof(GPUTimestamp) * m_currentQuery);
        return m_currentQuery;
    }

    u32 push(u32 currentFrame, const char* name){
        const u32 queryIndex = (currentFrame * m_queriesPerFrame) + m_currentQuery;

        NWB_ASSERT(queryIndex < (m_maxFrames * m_queriesPerFrame));
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

        NWB_ASSERT(queryIndex < (m_maxFrames * m_queriesPerFrame));
        const GPUTimestamp& timestamp = m_timestamps[queryIndex];

        --m_depth;

        m_parentIndex = timestamp.parentIndex;
        return (queryIndex * 2) + 1;
    }


private:
    Arena& m_arena;

private:
    GPUTimestamp* m_timestamps = nullptr;
    u64* m_data = nullptr;

private:
    u32 m_queriesPerFrame = 0;
    u32 m_maxFrames = 0;
    u32 m_currentQuery = 0;
    u32 m_parentIndex = 0;
    u32 m_depth = 0;

    bool m_currentFrameResolved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

