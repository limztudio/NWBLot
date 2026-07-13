
#pragma once


#include "global.h"

#include <core/alloc/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TimingAccumulator;
class TimingRecorder;
class TimingSink;
class TimingView;
class CpuTimingMeasure;

struct TimingScopeId{
    u32 index = Limit<u32>::s_Max;
    u32 generation = 0u;

    [[nodiscard]] bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};

struct TimingStats{
    f64 seconds = 0.0;
    f64 minSeconds = 0.0;
    f64 maxSeconds = 0.0;
    f64 lastSeconds = 0.0;
    u32 sampleCount = 0u;
    u64 publishFrameIndex = 0u;
    u64 firstSampleFrameIndex = 0u;
    u64 lastSampleFrameIndex = 0u;

    [[nodiscard]] bool valid()const{ return sampleCount != 0u; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TimingAccumulator final : NoCopy{
public:
    void clear();
    void setEnabled(bool enabled);
    void record(f64 seconds, u64 sampleFrameIndex);
    void publish(u64 publishFrameIndex);

    [[nodiscard]] const TimingStats& lastStats()const{ return m_lastStats; }


private:
    TimingStats m_currentStats;
    TimingStats m_lastStats;
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TimingSink{
public:
    virtual ~TimingSink() = default;


public:
    [[nodiscard]] virtual bool enabled()const = 0;
    [[nodiscard]] virtual TimingScopeId registerScope(const Name& scopeName) = 0;
    virtual void recordSample(TimingScopeId scope, f64 seconds, u64 sampleFrameIndex) = 0;
    virtual void publishFrame(u64 publishFrameIndex) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TimingRecorder final : public TimingSink, NoCopy{
private:
    struct ScopeRecord : NoCopy{
        explicit ScopeRecord(const Name& scopeName)
            : name(scopeName)
        {}

        Name name = NAME_NONE;
        TimingAccumulator accumulator;
        u32 generation = 0u;
    };

    using ScopeRecordPtr = GlobalUniquePtr<ScopeRecord>;
    using ScopeVector = Vector<ScopeRecordPtr, Alloc::GlobalArena>;
    using ScopeMap = HashMap<Name, TimingScopeId, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;


public:
    explicit TimingRecorder(Alloc::GlobalArena& arena)
        : m_arena(arena)
        , m_scopes(arena)
        , m_scopeMap(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    void setEnabled(bool enabled);
    [[nodiscard]] virtual bool enabled()const override{ return m_enabled; }
    void clear();
    [[nodiscard]] virtual TimingScopeId registerScope(const Name& scopeName)override;
    virtual void recordSample(TimingScopeId scope, f64 seconds, u64 sampleFrameIndex)override;
    void recordSample(const Name& scopeName, f64 seconds);
    void recordSample(const Name& scopeName, f64 seconds, u64 sampleFrameIndex);
    void publishFrame();
    virtual void publishFrame(u64 publishFrameIndex)override;

    [[nodiscard]] const TimingStats& stats(const Name& scopeName)const;
    [[nodiscard]] const TimingStats& stats(TimingScopeId scope)const;
    [[nodiscard]] usize scopeCount()const{ return m_scopes.size(); }
    [[nodiscard]] TimingScopeId scopeAt(usize index)const;
    [[nodiscard]] Name scopeNameAt(usize index)const;
    [[nodiscard]] const TimingStats& statsAt(usize index)const;


private:
    [[nodiscard]] ScopeRecord* findScope(TimingScopeId scope)const;


private:
    Alloc::GlobalArena& m_arena;
    ScopeVector m_scopes;
    ScopeMap m_scopeMap;
    TimingStats m_emptyStats;
    u64 m_nextPublishFrameIndex = 0u;
    u32 m_generation = 1u;
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TimingView final{
public:
    TimingView() = default;
    explicit TimingView(const TimingRecorder& recorder)
        : m_recorder(&recorder)
    {}


public:
    [[nodiscard]] bool valid()const{ return m_recorder != nullptr; }
    [[nodiscard]] const TimingStats& stats(const Name& scopeName)const;
    [[nodiscard]] const TimingStats& stats(TimingScopeId scope)const;
    [[nodiscard]] usize scopeCount()const;
    [[nodiscard]] TimingScopeId scopeAt(usize index)const;
    [[nodiscard]] Name scopeNameAt(usize index)const;
    [[nodiscard]] const TimingStats& statsAt(usize index)const;


private:
    const TimingRecorder* m_recorder = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CpuTimingMeasure final : NoCopy{
public:
    CpuTimingMeasure(CpuTimingMeasure&&) = delete;
    CpuTimingMeasure& operator=(CpuTimingMeasure&&) = delete;

    CpuTimingMeasure(TimingSink& timing, const Name& scopeName, const u64 sampleFrameIndex)
        : m_timing(timing)
        , m_scope(timing.registerScope(scopeName))
        , m_sampleFrameIndex(sampleFrameIndex)
        , m_begin(TimerNow())
    {}
    CpuTimingMeasure(TimingSink& timing, const TimingScopeId scope, const u64 sampleFrameIndex)
        : m_timing(timing)
        , m_scope(scope)
        , m_sampleFrameIndex(sampleFrameIndex)
        , m_begin(TimerNow())
    {}
    ~CpuTimingMeasure(){
        m_timing.recordSample(m_scope, DurationInSeconds<f64>(TimerNow(), m_begin), m_sampleFrameIndex);
    }


private:
    TimingSink& m_timing;
    TimingScopeId m_scope;
    u64 m_sampleFrameIndex = 0u;
    Timer m_begin = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PERF_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

