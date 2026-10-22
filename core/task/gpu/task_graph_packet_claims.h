// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_graph_submission_binding.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph;
class GpuGraphSubmissionTransaction;
class GpuNativePacketRecorder;
class GpuTaskScheduler;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuRecordingAttemptScope final : NoCopy{
    friend class GpuTaskGraph;
    friend class GpuGraphSubmissionTransaction;
    friend class GpuNativePacketRecorder;
    friend class GpuTaskScheduler;


public:
    GpuRecordingAttemptScope()noexcept = default;
    ~GpuRecordingAttemptScope()noexcept;


public:
    void complete()noexcept;


private:
    void activateWithinLock(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, u64 preparationSerial, bool previousPlanWasActive)noexcept;
    void completeWithinLock()noexcept;
    [[nodiscard]] bool validPreparationWithinLock(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, u64 preparationSerial)const noexcept;


private:
    const GpuTaskGraph* m_graph = nullptr;
    const GpuCompiledGraph* m_compiledGraph = nullptr;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_preparationSerial = 0u;
    bool m_previousPlanWasActive = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A failed parallel recorder transfers its exact packet claim into this capability before leaving its worker.
// The ready-frontier owner can then invoke typed discard callbacks serially after every worker has joined.
class GpuPacketRecordingAbort final : NoCopy{
    friend class GpuTaskGraph;


public:
    GpuPacketRecordingAbort()noexcept = default;
    GpuPacketRecordingAbort(GpuPacketRecordingAbort&& other)noexcept;
    ~GpuPacketRecordingAbort()noexcept;


public:
    [[nodiscard]] bool valid()const noexcept{
        return m_packet.valid() && m_planGeneration != 0u && m_recordingAttemptGeneration != 0u && m_claimGeneration != 0u;
    }


private:
    void reset()noexcept{
        m_packet = {};
        m_planGeneration = 0u;
        m_recordingAttemptGeneration = 0u;
        m_claimGeneration = 0u;
    }


private:
    GpuSubmissionPacketId m_packet;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_claimGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A packet claim is an opaque runtime capability. Only the recorder that acquired it can invoke task thunks, complete native recording, or abandon that exact packet attempt.
class GpuPacketRecordingLease final : NoCopy{
    friend class GpuTaskGraph;
    friend class GpuPacketRecordingAccess;


public:
    GpuPacketRecordingLease()noexcept = default;
    GpuPacketRecordingLease(GpuPacketRecordingLease&&) = delete;


public:
    GpuPacketRecordingLease& operator=(GpuPacketRecordingLease&&) = delete;


public:
    [[nodiscard]] bool valid()const noexcept{
        return m_packet.valid() && m_planGeneration != 0u && m_recordingAttemptGeneration != 0u && m_claimGeneration != 0u;
    }
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet)const noexcept;


private:
    void reset()noexcept{
        m_packet = {};
        m_planGeneration = 0u;
        m_recordingAttemptGeneration = 0u;
        m_claimGeneration = 0u;
    }


private:
    GpuSubmissionPacketId m_packet;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_claimGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Authenticates one exact recording claim once, then carries the lexical graph/plan read proof through preflight and barrier/state lowering without repeatedly taking the lifecycle mutex.
class GpuPacketRecordingAccess final : NoCopy{
    friend class GpuTaskGraph;
    friend class GpuNativePacketRecorder;


public:
    GpuPacketRecordingAccess(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuPacketRecordingLease& lease)noexcept;


public:
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet)const noexcept;
    [[nodiscard]] bool validForTask(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuTaskId& task)const noexcept;


private:
    const GpuTaskGraph* m_graph = nullptr;
    const GpuCompiledGraph* m_compiledGraph = nullptr;
    const GpuPacketRecordingLease* m_lease = nullptr;
    GpuSubmissionPacketId m_packet;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_claimGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Native submission has the same exclusive ownership rule as recording: cancellation cannot discard payload while Device owns the packet in executeCommandLists(), and only the transaction resolves it.
class GpuPacketSubmissionLease final : NoCopy{
    friend class GpuTaskGraph;
    friend class GpuGraphSubmissionTransaction;


public:
    GpuPacketSubmissionLease()noexcept = default;
    GpuPacketSubmissionLease(GpuPacketSubmissionLease&&) = delete;


public:
    GpuPacketSubmissionLease& operator=(GpuPacketSubmissionLease&&) = delete;


public:
    [[nodiscard]] bool valid()const noexcept{
        return m_packet.valid() && m_planGeneration != 0u && m_recordingAttemptGeneration != 0u && m_claimGeneration != 0u && m_submissionBinding.valid();
    }


private:
    void reset()noexcept{
        m_packet = {};
        m_planGeneration = 0u;
        m_recordingAttemptGeneration = 0u;
        m_claimGeneration = 0u;
        m_submissionBinding = {};
    }


private:
    GpuSubmissionPacketId m_packet;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_claimGeneration = 0u;
    GpuGraphSubmissionBinding m_submissionBinding;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

