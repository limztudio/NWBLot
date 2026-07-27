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
class GpuTimingRecorder;
class GpuTimingSubmissionTicket;

struct GpuTimingScope{
    GpuTimingAccumulator* accumulator = nullptr;
    TimerQuery* query = nullptr;
    u32 index = Limit<u32>::s_Max;
    // Distinguishes a reused query-pool slot from an earlier scope that was discarded after its command list failed
    // to submit. Submission tickets only release a record when this reservation still owns it.
    u64 reservation = 0u;
    GpuTimingSubmissionTicket* submissionTicket = nullptr;

    [[nodiscard]] bool valid()const{ return accumulator != nullptr && query != nullptr && index != Limit<u32>::s_Max && reservation != 0u; }
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
        u64 reservation = 0u;
        // Held from beginQuery() until the matching endQuery() or an explicit discard. A submission ticket can only
        // roll back completed scopes, so this also keeps two concurrently recording workers from selecting one slot.
        bool recording = false;
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
    [[nodiscard]] bool endQuery(CommandList& commandList, const GpuTimingScope& scope);
    void discardQuery(const GpuTimingScope& scope);


private:
    [[nodiscard]] u32 findAvailableQuery()const;
    [[nodiscard]] u32 appendQuery(Device& device);


private:
    QueryVector m_queries;
    Perf::TimingScopeId m_timingScope;
    u32 m_requestedQueryCount = 0u;
    u64 m_nextReservation = 0u;
    bool m_enabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder final : NoCopy{
    friend class GpuTimingMeasure;
    friend class GpuTimingSubmissionTicket;

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
    void discardScope(const GpuTimingScope& scope);
    [[nodiscard]] GpuTimingAccumulator* findOrCreateAccumulator(const Name& scopeName);
    [[nodiscard]] GpuTimingSubmissionTicket* activeSubmissionTicket()const;
    void collectLocked(Device& device, u64 publishFrameIndex);
    void discardFrameResetLocked();
    void syncActiveState();
    void advanceEpoch();


private:
    Alloc::GlobalArena& m_arena;
    Perf::TimingSink& m_timing;
    AccumulatorMap m_accumulators;
    // A submission ticket protects its own rollback list, while this lock serializes every query-pool mutation and
    // recorder-map access. Worker command-list recordings may share a ticket and begin timing scopes concurrently.
    Futex m_mutex;
    u64 m_currentFrameIndex = 0u;
    u32 m_epoch = 1u;
    bool m_accumulatorsActive = false;
    bool m_enabled = false;

    static thread_local GpuTimingSubmissionTicket* s_activeSubmissionTicket;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Couples timestamp-query ownership to one complete queue submission. Keep a RecordingScope active while recording
// every primary command buffer that can contain a timing endpoint, then submit that whole ordered batch through this
// ticket. If recording aborts or the submission is rejected, the ticket releases the reserved query slots; a
// successful submission retains them until collect() observes their results. This supports a timing scope whose
// start and end timestamps live in separate primary command buffers.
class GpuTimingSubmissionTicket final : NoCopy{
public:
    class RecordingScope final : NoCopy{
    public:
        explicit RecordingScope(GpuTimingSubmissionTicket& ticket);
        ~RecordingScope();


    private:
        GpuTimingSubmissionTicket& m_ticket;
        GpuTimingSubmissionTicket* m_previousTicket = nullptr;
    };


public:
    explicit GpuTimingSubmissionTicket(GpuTimingRecorder& recorder);
    ~GpuTimingSubmissionTicket();


public:
    // Validates that every supplied command list still owns a ready command buffer, submits them together in their
    // supplied order, and resolves every timing scope recorded under this ticket. A partial split submission is
    // rejected before reaching Vulkan so a cross-list timing scope can never be left waiting for a missing endpoint.
    [[nodiscard]] bool submit(
        Device& device,
        CommandList* const* commandLists,
        usize commandListCount,
        CommandQueue::Enum executionQueue = CommandQueue::Graphics
    );
    // Explicitly releases all timing scopes when a caller elects not to submit its recorded command lists.
    void discard();


private:
    friend class GpuTimingRecorder;

    void trackScope(const GpuTimingScope& scope);
    [[nodiscard]] GpuTimingSubmissionTicket* activateOnCurrentThread();
    void deactivateOnCurrentThread(GpuTimingSubmissionTicket* previousTicket);
    void confirm();


private:
    GpuTimingRecorder& m_recorder;
    Vector<GpuTimingScope, Alloc::GlobalArena> m_scopes;
    Futex m_mutex;
    u32 m_recordingScopeCount = 0u;
    bool m_resolved = false;
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

