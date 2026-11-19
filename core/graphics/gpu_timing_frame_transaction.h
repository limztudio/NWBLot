// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "gpu_timing_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    [[nodiscard]] b8 begin(const GpuTimingScopeDefinition& scopeDefinition, Device& device, CommandList& commandList, GpuTimingSampleAttribution attribution = s_NoGpuTimingSampleAttribution);
    [[nodiscard]] b8 recordEnd(CommandList& commandList);
    [[nodiscard]] b8 confirmBeginSubmission(const QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 confirmEndSubmission(const QueueSubmissionToken& token, b8 publishSample)noexcept;
    [[nodiscard]] b8 needsRetirement()const noexcept{ return m_beginSubmission.valid() && m_scope.valid(); }
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

