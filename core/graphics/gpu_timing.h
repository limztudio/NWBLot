// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingAccumulator;
class GpuTimingMeasure;

struct GpuTimingStats{
    f32 seconds = 0.0f;
    u32 sampleCount = 0u;

    [[nodiscard]] bool valid()const{ return sampleCount != 0u; }
};

struct GpuTimingScope{
    GpuTimingAccumulator* accumulator = nullptr;
    ITimerQuery* query = nullptr;
    u32 index = Limit<u32>::s_Max;

    [[nodiscard]] bool valid()const{ return accumulator != nullptr && query != nullptr && index != Limit<u32>::s_Max; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingAccumulator final : NoCopy{
private:
    struct QueryRecord{
        TimerQueryHandle query;
        bool pending = false;
    };

    using QueryVector = Vector<QueryRecord, Alloc::GlobalArena>;


public:
    explicit GpuTimingAccumulator(Alloc::GlobalArena& arena)
        : m_queries(arena)
    {}


public:
    void setEnabled(const bool enabled){
        m_enabled = enabled;
        if(!m_enabled)
            m_lastStats = GpuTimingStats{};
    }

    void collect(IDevice& device){
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

    [[nodiscard]] GpuTimingScope beginQuery(IDevice& device, ICommandList& commandList){
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

    void endQuery(ICommandList& commandList, const GpuTimingScope& scope){
        if(!scope.valid() || scope.accumulator != this || scope.index >= m_queries.size())
            return;

        QueryRecord& record = m_queries[scope.index];
        if(record.query.get() != scope.query)
            return;

        commandList.endTimerQuery(record.query.get());
        record.pending = true;
    }

    [[nodiscard]] const GpuTimingStats& lastStats()const{ return m_lastStats; }


private:
    [[nodiscard]] u32 acquireQuery(IDevice& device){
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


private:
    QueryVector m_queries;
    GpuTimingStats m_lastStats;
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder final : NoCopy{
    friend class GpuTimingMeasure;

private:
    using AccumulatorPtr = GlobalUniquePtr<GpuTimingAccumulator>;
    using AccumulatorMap = HashMap<Name, AccumulatorPtr, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;


public:
    explicit GpuTimingRecorder(Alloc::GlobalArena& arena)
        : m_arena(arena)
        , m_accumulators(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    void setEnabled(const bool enabled){
        m_enabled = enabled;
        for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
            it.value()->setEnabled(enabled);
        if(!m_enabled)
            m_emptyStats = GpuTimingStats{};
    }

    void clear(){
        m_accumulators.clear();
        m_emptyStats = GpuTimingStats{};
    }

    void collect(IDevice& device){
        if(!m_enabled)
            return;

        for(auto it = m_accumulators.begin(); it != m_accumulators.end(); ++it)
            it.value()->collect(device);
    }

    [[nodiscard]] const GpuTimingStats& stats(const Name& scopeName)const{
        const auto found = m_accumulators.find(scopeName);
        if(found == m_accumulators.end())
            return m_emptyStats;

        return found.value()->lastStats();
    }


private:
    [[nodiscard]] GpuTimingScope beginScope(const Name& scopeName, IDevice* device, ICommandList& commandList){
        if(!m_enabled || !scopeName || !device)
            return {};

        GpuTimingAccumulator* accumulator = findOrCreateAccumulator(scopeName);
        if(!accumulator)
            return {};

        return accumulator->beginQuery(*device, commandList);
    }

    void endScope(ICommandList& commandList, const GpuTimingScope& scope){
        if(!scope.valid())
            return;

        scope.accumulator->endQuery(commandList, scope);
    }

    [[nodiscard]] GpuTimingAccumulator* findOrCreateAccumulator(const Name& scopeName){
        auto found = m_accumulators.find(scopeName);
        if(found != m_accumulators.end())
            return found.value().get();

        AccumulatorPtr accumulator = MakeGlobalUnique<GpuTimingAccumulator>(m_arena, m_arena);
        if(!accumulator)
            return nullptr;

        auto [it, inserted] = m_accumulators.try_emplace(scopeName, Move(accumulator));
        static_cast<void>(inserted);
        it.value()->setEnabled(m_enabled);
        return it.value().get();
    }


private:
    Alloc::GlobalArena& m_arena;
    AccumulatorMap m_accumulators;
    GpuTimingStats m_emptyStats;
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingMeasure final : NoCopy{
public:
    GpuTimingMeasure(GpuTimingMeasure&&) = delete;
    GpuTimingMeasure& operator=(GpuTimingMeasure&&) = delete;

    GpuTimingMeasure(
        GpuTimingRecorder& recorder,
        const Name& scopeName,
        IDevice* device,
        ICommandList& commandList
    )
        : m_recorder(recorder)
        , m_commandList(commandList)
        , m_scope(recorder.beginScope(scopeName, device, commandList))
    {}
    ~GpuTimingMeasure(){
        m_recorder.endScope(m_commandList, m_scope);
    }


private:
    GpuTimingRecorder& m_recorder;
    ICommandList& m_commandList;
    GpuTimingScope m_scope;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

