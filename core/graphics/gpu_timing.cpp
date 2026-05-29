// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTimingAccumulator::collect(Device& device){
    m_lastStats = GpuTimingStats{};
    if(!m_enabled)
        return;

    for(QueryRecord& record : m_queries){
        if(!record.pending || !record.query)
            continue;
        if(!device.pollTimerQuery(record.query.get()))
            continue;

        m_lastStats.seconds += device.getTimerQueryTime(record.query.get());
        ++m_lastStats.sampleCount;
        device.resetTimerQuery(record.query.get());
        record.pending = false;
    }
}

GpuTimingScope GpuTimingAccumulator::beginQuery(Device& device, CommandList& commandList){
    if(!m_enabled)
        return {};

    const u32 index = acquireQuery(device);
    if(index == Limit<u32>::s_Max)
        return {};

    QueryRecord& record = m_queries[index];
    device.resetTimerQuery(record.query.get());
    commandList.beginTimerQuery(record.query.get());
    return GpuTimingScope{ this, record.query.get(), index };
}

void GpuTimingAccumulator::endQuery(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid() || scope.accumulator != this || scope.index >= m_queries.size())
        return;

    QueryRecord& record = m_queries[scope.index];
    if(record.query.get() != scope.query)
        return;

    commandList.endTimerQuery(record.query.get());
    record.pending = true;
}

u32 GpuTimingAccumulator::acquireQuery(Device& device){
    for(usize i = 0u; i < m_queries.size(); ++i){
        if(!m_queries[i].pending)
            return static_cast<u32>(i);
    }

    QueryRecord record;
    record.query = device.createTimerQuery();
    if(!record.query)
        return Limit<u32>::s_Max;

    m_queries.push_back(Move(record));
    return static_cast<u32>(m_queries.size() - 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

