// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuPacketRecordingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_PacketTimingScratchArena("core/task/gpu/packet_timing_scratch");
inline constexpr AStringView s_PacketMarkerLabel = "GPU Task Packet";
inline constexpr AStringView s_DefaultTaskMarkerLabel = "GPU Task";


[[nodiscard]] inline GpuTimingScopeDefinition TimingScopeDefinition(
    const Name& identity,
    const AStringView markerLabel
)noexcept{
    GpuTimingScopeDefinition definition;
    definition.identity = identity;
    definition.markerLabel = markerLabel;
    return definition;
}

[[nodiscard]] inline Name PacketTimingScopeName(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId& packet
){
    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    if(!packetView.valid() || packetView.plan->taskCount == 0u)
        return NAME_NONE;
    const GpuTaskGraphTaskView task = declarationAccess.taskAt(packetView.tasks[0u].index);
    return task.id == packetView.tasks[0u] ? GpuTaskPacketTimingScopeName(task.identity) : NAME_NONE;
}

[[nodiscard]] bool PrepareCompiledTimingQueries(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    GpuTimingRecorder* const timingRecorder,
    Alloc::ScratchArena& scratchArena
);

[[nodiscard]] bool HasExplicitKnownInitialState(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledBarrier& barrier,
    CommandList& commandList
);

#if defined(NWB_DEBUG)
[[nodiscard]] inline bool HasQueueCapabilities(
    const GpuQueueCapability::Mask available,
    const GpuQueueCapability::Mask required
)noexcept{
    return (static_cast<u8>(available) & static_cast<u8>(required)) == static_cast<u8>(required);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
