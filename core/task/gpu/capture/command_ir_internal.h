// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "command_ir.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskGraphTaskView;


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DecodedRecordContext{
    GpuTaskId task;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
};

[[nodiscard]] inline Expected<DecodedRecordContext> DecodeRecordContext(
    const GpuCommandIrRecordContext& context,
    const u64 graphGeneration,
    const u64 planGeneration
)noexcept{
    const DecodedRecordContext decoded{
        .task = { .generation = graphGeneration, .index = context.taskIndex },
        .packet = { .generation = planGeneration, .index = context.packetIndex },
        .queue = { .index = context.queueIndex, .deviceGeneration = context.queueDeviceGeneration },
    };
    if(!decoded.task.valid() || !decoded.packet.valid() || !decoded.queue.valid())
        return MakeUnexpected(Failure{});
    return decoded;
}


[[nodiscard]] bool ValidateBuiltinRecord(const GpuCommandIrBuiltinTaskRecord& record)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateUploadOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateUploadBackendOperand(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraphDeclarationReadView& graph,
    CommandList& commandList,
    const GpuPhysicalQueueInfo& queue
)noexcept;
[[nodiscard]] bool LowerUploadOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    CommandList& commandList
)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateReplayCommandListQueue(
    const CommandListParameters& commandListDescription,
    const GpuPhysicalQueueId& packetQueue,
    CommandQueue::Enum packetQueueClass
)noexcept;

struct RasterReplayState{
    GpuCommandIrRasterTaskRecord graphicsState;
    GpuTaskId task;
    bool active = false;
    bool heapBound = false;
    bool pushConstantsSet = false;
};

[[nodiscard]] inline bool IsRasterOpcode(const GpuCommandIrWireOpcode::Enum opcode)noexcept{
    return
        opcode == GpuCommandIrWireOpcode::SetGraphicsState
        || opcode == GpuCommandIrWireOpcode::BindGraphicsHeap
        || opcode == GpuCommandIrWireOpcode::SetPushConstants
        || opcode == GpuCommandIrWireOpcode::Draw
        || opcode == GpuCommandIrWireOpcode::DrawIndexed
        || opcode == GpuCommandIrWireOpcode::EndRenderPass
    ;
}

[[nodiscard]] inline bool IsRasterViewportValid(const Viewport& viewport)noexcept{
    return
        IsFinite(viewport.minX) && IsFinite(viewport.maxX)
        && IsFinite(viewport.minY) && IsFinite(viewport.maxY)
        && IsFinite(viewport.minZ) && IsFinite(viewport.maxZ)
        && viewport.minX < viewport.maxX && viewport.minY < viewport.maxY
        && viewport.minZ < viewport.maxZ && viewport.minZ >= 0.f && viewport.maxZ <= 1.f
    ;
}

[[nodiscard]] GpuCommandIrReplayError::Enum ValidateRasterGraphOperation(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    RasterReplayState& state
)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateRasterBuiltinBoundary(
    const GpuTaskId& task,
    const RasterReplayState& state
)noexcept;
[[nodiscard]] GpuCommandIrReplayError::Enum ValidateRasterBackendOperand(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuCommandIrOwnedStream& stream,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    CommandList& commandList,
    u64 recordIndex,
    RasterReplayState& state
)noexcept;
[[nodiscard]] bool LowerRasterOperation(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuCommandIrOwnedStream& stream,
    BinaryByteView blobBytes,
    CommandList& commandList
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

