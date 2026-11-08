// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "gpu_timing_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Whole-frame timestamp across accepted submissions; pending only once the ending packet is accepted.
// Rejected tail: recovery end with publishSample=false so no partial duration reports.
class GpuTimingFrameTransaction final : NoCopy{
private:
    struct State{
        enum Enum : u8{
            Idle,
            Inactive,
            BeginRecorded,
            BeginAccepted,
            EndRecorded,
            Resolved,
        };
    };


public:
    explicit GpuTimingFrameTransaction(GpuTimingRecorder& recorder)noexcept;
    ~GpuTimingFrameTransaction()noexcept;


public:
    // Must run inside a GpuTimingSubmissionTicket::RecordingScope for the submission that records the begin timestamp.
    // A disabled or under-reserved timing recorder is a valid no-op transaction.
    [[nodiscard]] b8 begin(const GpuTimingScopeDefinition& scopeDefinition, Device& device, CommandList& commandList, GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution);
    // Records the end timestamp but keeps the reservation private until the submission accepts, so a rejected final packet can take a recovery endpoint.
    [[nodiscard]] b8 recordEnd(CommandList& commandList);
    [[nodiscard]] b8 confirmBeginSubmission(const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 confirmEndSubmission(const QueueSubmissionToken& token, b8 publishSample)noexcept;
    [[nodiscard]] b8 needsRetirement()const noexcept{ return m_beginSubmission.valid() && m_scope.valid(); }
    // Drops the endpoint of an unsubmitted packet so a recovery list can write its replacement after the accepted begin.
    [[nodiscard]] b8 prepareForRecovery()noexcept;
    void discard();


private:
    void abandonWithoutCallbacks()noexcept;


private:
    GpuTimingRecorder& m_recorder;
    GpuTimingScope m_scope;
    QueueSubmissionToken m_beginSubmission;
    State::Enum m_state = State::Idle;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

