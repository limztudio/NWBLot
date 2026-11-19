// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "gpu_timing_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingSubmissionTicket final : NoCopy{
    friend class GpuGraphSubmissionTransaction;
    friend class GpuRecordedGraph;
    friend class GpuTaskScheduler;
    friend class GpuTimingRecorder;


public:
    class RecordingScope final : NoCopy{
    public:
        explicit RecordingScope(GpuTimingSubmissionTicket& ticket)noexcept;
        ~RecordingScope()noexcept;


    private:
        GpuTimingSubmissionTicket& m_ticket;
        GpuTimingSubmissionTicket* m_previousTicket = nullptr;
        b8 m_activated = false;
    };


private:

    struct ScopePublicationState{
        enum Enum : u8{
            Reserved,
            Published,
            Cancelled,
        };
    };

    struct ScopeEndpointValidationResult{
        enum Enum : u8{
            Valid,
            RetryableBatchMismatch,
            InvalidEndpoint,
        };
    };

    struct ActivationFailure{
        GpuTimingSubmissionTicket* previousTicket = nullptr;
    };

    struct CommandListRecordingEndpoint{
        const CommandList* commandList = nullptr;
        u64 recordingLeaseSerial = 0u;


    public:
        [[nodiscard]] b8 valid()const noexcept{ return commandList && recordingLeaseSerial != 0u; }
    };

    struct ScopePublication{
        GpuTimingScope scope;
        CommandListRecordingEndpoint beginEndpoint;
        CommandListRecordingEndpoint endEndpoint;
        ScopePublicationState::Enum state = ScopePublicationState::Reserved;
    };


public:
    explicit GpuTimingSubmissionTicket(GpuTimingRecorder& recorder)noexcept;
    ~GpuTimingSubmissionTicket()noexcept;


public:
    [[nodiscard]] Expected<QueueSubmissionToken> submit(Device& device, CommandList* const* commandLists, usize commandListCount, CommandQueue::Enum executionQueue = CommandQueue::Graphics, const QueueSubmissionDesc& submitDesc = {});
    [[nodiscard]] Expected<QueueSubmissionToken> submit(Device& device, CommandList* const* commandLists, usize commandListCount, const GpuPhysicalQueueId& executionQueue, const QueueSubmissionDesc& submitDesc);
    void discard();


private:
    template<class ExecutionQueue>
    [[nodiscard]] FOUNDATION_INLINE Expected<QueueSubmissionToken> submitToQueue(Device& device, Span<CommandList* const> commandLists, const ExecutionQueue& executionQueue, const QueueSubmissionDesc& submitDesc);

    [[nodiscard]] b8 prepareSubmission(Span<CommandList* const> commandLists, Vector<QueueSubmissionToken, Alloc::ScratchArena>& waitTokens);
    [[nodiscard]] b8 prepareSubmissionAfterCommandListValidation(Span<CommandList* const> commandLists, Vector<QueueSubmissionToken, Alloc::ScratchArena>& waitTokens);
    [[nodiscard]] b8 prepareSubmissionState(Vector<QueueSubmissionToken, Alloc::ScratchArena>& waitTokens);
    [[nodiscard]] ScopeEndpointValidationResult::Enum validateScopePublicationEndpoints(Span<CommandList* const> commandLists)noexcept;
    [[nodiscard]] b8 resetForRecordingReuse(GpuTimingRecorder& recorder)noexcept;
    void rollbackPreparedSubmission()noexcept;
    void discardPreparedSubmission()noexcept;
    void abandonWithoutCallbacks()noexcept;
    [[nodiscard]] b8 resolveSubmission(const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] Expected<usize> reserveScopePublication();
    [[nodiscard]] b8 bindScopePublication(usize publicationIndex, const GpuTimingScope& scope, const CommandList& commandList)noexcept;
    void cancelScopePublication(usize publicationIndex)noexcept;
    [[nodiscard]] b8 publishScope(const GpuTimingScope& scope, const CommandList& commandList)noexcept;
    [[nodiscard]] b8 trackSubmissionPrerequisite(const QueueSubmissionToken& token);
    [[nodiscard]] Expected<GpuTimingSubmissionTicket*, ActivationFailure> activateOnCurrentThread()noexcept;
    void deactivateOnCurrentThread(GpuTimingSubmissionTicket* previousTicket, b8 activated)noexcept;
    [[nodiscard]] b8 confirm(const QueueSubmissionToken& token)noexcept;


private:
    GpuTimingRecorder& m_recorder;
    Vector<ScopePublication, Alloc::GlobalArena> m_scopePublications;
    Vector<QueueSubmissionToken, Alloc::GlobalArena> m_submissionPrerequisites;
    Futex m_mutex;
    u32 m_recordingScopeCount = 0u;
    b8 m_submissionPrepared = false;
    b8 m_resolved = false;
    usize m_reservedScopePublicationCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

