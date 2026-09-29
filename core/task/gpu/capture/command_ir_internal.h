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

[[nodiscard]] bool IsRasterOpcode(GpuCommandIrWireOpcode::Enum opcode)noexcept;
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

