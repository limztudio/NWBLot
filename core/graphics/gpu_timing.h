// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        // Set while a frame-preamble command list contains a reset for this pool. It becomes deviceReady only after
        // that command list has been submitted successfully.
        bool frameResetRecorded = false;
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
        if(!m_enabled)
            discardFrameReset();
    }

    void collect(Device& device, Perf::TimingSink& timing, u32 epoch);
    void recordFrameReset(CommandList& commandList);
    void confirmFrameReset();
    void discardFrameReset();
    void requestQueries(u32 queryCount);
    [[nodiscard]] bool materializeRequestedQueries(Device& device);
    [[nodiscard]] bool reserveQueries(Device& device, u32 queryCount);
    [[nodiscard]] GpuTimingScope beginQuery(Device& device, CommandList& commandList, u64 frameIndex, u32 epoch);
    void endQuery(CommandList& commandList, const GpuTimingScope& scope);


private:
    [[nodiscard]] u32 findAvailableQuery()const;
    [[nodiscard]] u32 appendQuery(Device& device);


private:
    QueryVector m_queries;
    Perf::TimingScopeId m_timingScope;
    u32 m_requestedQueryCount = 0u;
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
    // Declares the capacity a scope needs. When capture is inactive this records the request without allocating GPU
    // query pools, so a later capture activation can materialize them before its first frame preamble.
    [[nodiscard]] bool prepareScopeQueries(const Name& scopeName, Device* device, u32 queryCount);
    // Materializes every declared scope before the frame preamble. Graphics calls this after capture can be toggled
    // on at runtime and before dynamic-rendering scopes need their query pools reset.
    [[nodiscard]] bool materializeRequestedQueries(Device& device);
    // Record a device-timeline reset of every available timer-query pool onto the command buffer. Graphics emits this
    // in its frame preamble before it invokes any render pass and before any dynamic render pass opens
    // (vkCmdResetQueryPool is illegal inside one), so every pool is defined before this frame's timestamp writes --
    // the validation-correct alternative to a host-side reset the layer cannot order against recorded writes.
    // Call confirmFrameReset() only after that command list submits successfully. discardFrameReset() invalidates
    // prior-frame readiness when a new preamble cannot be submitted.
    void recordFrameReset(CommandList& commandList);
    void confirmFrameReset();
    void discardFrameReset();


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

    // A timing scope may span ordered primary command buffers. Close its debug marker on the command list that
    // opened it before that list is closed, then emit the ending timestamp on the later command list.
    void finishMarker();
    void finishTiming(CommandList& commandList);
    // Discards a started scope when its producer command buffer cannot be finalized or submitted.
    void discardTiming();


private:
    GpuTimingRecorder& m_recorder;
    CommandList& m_commandList;
    GpuTimingScope m_scope;
    bool m_markerOpen = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

