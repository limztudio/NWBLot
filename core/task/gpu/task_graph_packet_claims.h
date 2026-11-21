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
    void activateWithinLock(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, u64 preparationSerial, b8 previousPlanWasActive)noexcept;
    void completeWithinLock()noexcept;
    [[nodiscard]] b8 validPreparationWithinLock(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, u64 preparationSerial)const noexcept;


private:
    const GpuTaskGraph* m_graph = nullptr;
    const GpuCompiledGraph* m_compiledGraph = nullptr;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_preparationSerial = 0u;
    b8 m_previousPlanWasActive = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuPacketRecordingAbort final : NoCopy{
    friend class GpuTaskGraph;


public:
    GpuPacketRecordingAbort()noexcept = default;
    GpuPacketRecordingAbort(GpuPacketRecordingAbort&& other)noexcept;
    ~GpuPacketRecordingAbort()noexcept;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return m_packet.valid() && m_planGeneration != 0u && m_recordingAttemptGeneration != 0u && m_claimGeneration != 0u;
    }


private:
    void reset()noexcept;


private:
    GpuSubmissionPacketId m_packet;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_claimGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuPacketRecordingLease final : NoCopy{
    friend class GpuTaskGraph;
    friend class GpuPacketRecordingAccess;


public:
    GpuPacketRecordingLease()noexcept = default;
    GpuPacketRecordingLease(GpuPacketRecordingLease&&) = delete;


public:
    GpuPacketRecordingLease& operator=(GpuPacketRecordingLease&&) = delete;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return m_packet.valid() && m_planGeneration != 0u && m_recordingAttemptGeneration != 0u && m_claimGeneration != 0u;
    }
    [[nodiscard]] b8 validFor(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet)const noexcept;


private:
    void reset()noexcept;


private:
    GpuSubmissionPacketId m_packet;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_claimGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuPacketRecordingAccess final : NoCopy{
    friend class GpuTaskGraph;
    friend class GpuNativePacketRecorder;


public:
    GpuPacketRecordingAccess(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuPacketRecordingLease& lease)noexcept;


public:
    [[nodiscard]] b8 validFor(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet)const noexcept;
    [[nodiscard]] b8 validForTask(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuTaskId& task)const noexcept;


private:
    GpuPacketRecordingAccess()noexcept = default;
    void bind(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuPacketRecordingLease& lease)noexcept;


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


class GpuPacketSubmissionLease final : NoCopy{
    friend class GpuTaskGraph;
    friend class GpuGraphSubmissionTransaction;


public:
    GpuPacketSubmissionLease()noexcept = default;
    GpuPacketSubmissionLease(GpuPacketSubmissionLease&&) = delete;


public:
    GpuPacketSubmissionLease& operator=(GpuPacketSubmissionLease&&) = delete;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return m_packet.valid() && m_planGeneration != 0u && m_recordingAttemptGeneration != 0u && m_claimGeneration != 0u && m_submissionBinding.valid();
    }


private:
    void reset()noexcept;


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

