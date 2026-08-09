// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_desc.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph;
class GpuTaskGraphCompiler;


// Tasks describe semantic work.  Packets are compiler-generated native-recording and submission units.  The first
// production milestone intentionally creates one packet per task; packet merging arrives after this path owns
// lifecycle and dependency handling.
struct GpuSubmissionPacket{
    GpuPhysicalQueueId queue;
    u32 taskOffset = 0u;
    u32 taskCount = 0u;
    u32 dependencyOffset = 0u;
    u32 dependencyCount = 0u;
    u32 externalDependencyOffset = 0u;
    u32 externalDependencyCount = 0u;
    bool recordsTiming = true;
};

struct GpuPacketDependency{
    GpuSubmissionPacketId producer;
    GpuSubmissionPacketId consumer;
};

struct GpuCompiledTask{
    GpuTaskId task;
    GpuPhysicalQueueId queue;
    GpuSubmissionPacketId packet;
};


class GpuCompiledGraph final : NoCopy{
    friend class GpuTaskGraphCompiler;

public:
    explicit GpuCompiledGraph(GraphicsArena& arena);


public:
    void reset();

    [[nodiscard]] bool valid()const noexcept{ return m_valid; }
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph)const noexcept;
    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }
    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }
    [[nodiscard]] usize taskCount()const noexcept{ return m_tasks.size(); }
    [[nodiscard]] usize packetCount()const noexcept{ return m_packets.size(); }
    [[nodiscard]] bool validPacket(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] GpuSubmissionPacketId packetIdAt(usize index)const noexcept;
    [[nodiscard]] const GpuCompiledTask* findTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] GpuSubmissionPacketId packetForTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuSubmissionPacket& packet(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const GpuTaskId* packetTasks(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const GpuPacketDependency* packetDependencies(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const GpuExternalCompletionId* packetExternalDependencies(
        const GpuSubmissionPacketId& packet
    )const noexcept;
    [[nodiscard]] const GpuPhysicalQueueInfo* queueInfo(const GpuPhysicalQueueId& queue)const noexcept;


private:
    GraphicsVector<GpuCompiledTask> m_tasks;
    GraphicsVector<GpuSubmissionPacket> m_packets;
    GraphicsVector<GpuTaskId> m_packetTasks;
    GraphicsVector<GpuPacketDependency> m_packetDependencies;
    GraphicsVector<GpuExternalCompletionId> m_packetExternalDependencies;
    GraphicsVector<GpuPhysicalQueueInfo> m_queueTopology;
    u64 m_generation = 0u;
    u16 m_deviceGeneration = 0u;
    usize m_graphTaskCount = 0u;
    bool m_valid = false;
};


// Recording happens only after the compiler has selected a concrete physical queue and packet.  Task record thunks
// receive immutable compiled metadata rather than a nullable renderer-specific context.
struct GpuTaskRecordContext{
    const GpuCompiledGraph& graph;
    GpuTaskId task;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
