#pragma once


#include "api.h"

#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingAccumulator;
class GpuTimingMeasure;

struct GpuTimingScope{
    GpuTimingAccumulator* accumulator = nullptr;
    TimerQuery* query = nullptr;
    u32 index = Limit<u32>::s_Max;

    [[nodiscard]] bool valid()const{ return accumulator != nullptr && query != nullptr && index != Limit<u32>::s_Max; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTimingScopeDefinition{
    Name identity = NAME_NONE;
    AStringView markerLabel;


    constexpr GpuTimingScopeDefinition() = default;
    constexpr explicit GpuTimingScopeDefinition(const char* const label)
        : identity(label)
        , markerLabel(label)
    {}


    [[nodiscard]] constexpr bool valid()const{ return identity && !markerLabel.empty(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingAccumulator final : NoCopy{
private:
    struct QueryRecord{
        TimerQueryHandle query;
        u64 frameIndex = 0u;
        u32 epoch = 0u;
        bool pending = false;
        // False until this pool has been reset on the DEVICE timeline by recordFrameReset() at a frame open. Pools
        // created outside a render pass can self-reset before their first write; render-pass scopes must use prewarmed
        // pools that have already passed through recordFrameReset().
        bool deviceReady = false;
    };

    using QueryVector = Vector<QueryRecord, Alloc::GlobalArena>;


public:
    explicit GpuTimingAccumulator(Alloc::GlobalArena& arena, const Perf::TimingScopeId timingScope)
        : m_queries(arena)
        , m_timingScope(timingScope)
    {}


public:
    void setEnabled(const bool enabled){
        m_enabled = enabled;
    }

    void collect(Device& device, Perf::TimingSink& timing, u32 epoch);
    void recordFrameReset(CommandList& commandList);
    [[nodiscard]] bool reserveQueries(Device& device, u32 queryCount);
    [[nodiscard]] GpuTimingScope beginQuery(Device& device, CommandList& commandList, u64 frameIndex, u32 epoch);
    void endQuery(CommandList& commandList, const GpuTimingScope& scope);


private:
    [[nodiscard]] u32 findAvailableQuery()const;
    [[nodiscard]] u32 appendQuery(Device& device);


private:
    QueryVector m_queries;
    Perf::TimingScopeId m_timingScope;
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder final : NoCopy{
    friend class GpuTimingMeasure;

private:
    using AccumulatorPtr = GlobalUniquePtr<GpuTimingAccumulator>;
    using AccumulatorMap = HashMap<Name, AccumulatorPtr, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;


public:
    GpuTimingRecorder(Alloc::GlobalArena& arena, Perf::TimingSink& timing);


public:
    void setQueryCollectionEnabled(bool enabled);
    [[nodiscard]] bool queryCollectionEnabled()const{ return m_enabled; }
    void resetQueries();
    void collect(Device& device);
    void collect(Device& device, u64 publishFrameIndex);
    void beginFrame(u64 frameIndex);
    [[nodiscard]] bool prepareScopeQueries(const Name& scopeName, Device* device, u32 queryCount);
    // Record a device-timeline reset of every timer query pool onto the command buffer. The renderer MUST call
    // this at frame open, before opening any dynamic render pass (vkCmdResetQueryPool is illegal inside one), so
    // every pool is defined before this frame's timestamp writes -- the validation-correct alternative to a
    // host-side reset the layer cannot order against the recorded writes.
    void recordFrameReset(CommandList& commandList);


private:
    [[nodiscard]] GpuTimingScope beginScope(const Name& scopeName, Device* device, CommandList& commandList);
    void endScope(CommandList& commandList, const GpuTimingScope& scope);
    [[nodiscard]] GpuTimingAccumulator* findOrCreateAccumulator(const Name& scopeName);
    void syncActiveState();
    void advanceEpoch();


private:
    Alloc::GlobalArena& m_arena;
    Perf::TimingSink& m_timing;
    AccumulatorMap m_accumulators;
    u64 m_currentFrameIndex = 0u;
    u32 m_epoch = 1u;
    bool m_accumulatorsActive = false;
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingMeasure final : NoCopy{
public:
    GpuTimingMeasure(GpuTimingMeasure&&) = delete;
    GpuTimingMeasure& operator=(GpuTimingMeasure&&) = delete;

    // Defined out-of-line: the ctor/dtor call CommandList::beginMarker/endMarker, and CommandList is only
    // forward-declared in this header (the marker calls need the complete type, available in the .cpp).
    GpuTimingMeasure(
        GpuTimingRecorder& recorder,
        const GpuTimingScopeDefinition& scopeDefinition,
        Device* device,
        CommandList& commandList
    );
    ~GpuTimingMeasure();


private:
    GpuTimingRecorder& m_recorder;
    CommandList& m_commandList;
    GpuTimingScope m_scope;
    bool m_markerOpen = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

