// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "command_ir_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


class GpuCommandIrBuiltinPreflight final{
public:
    [[nodiscard]] static GpuCommandIrReplayError::Enum Validate(const GpuCommandIrBuiltinTaskRecord& record, BinaryByteView blobBytes, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& queue)noexcept;
    [[nodiscard]] static GpuCommandIrReplayError::Enum ValidateWire(
        GpuCommandIrWireOpcode::Enum opcode,
        const u8* recordPtr,
        u64 graphGeneration,
        BinaryByteView blobBytes,
        const GpuTaskGraphDeclarationReadView& graph,
        const GpuTaskGraphTaskView& task,
        const GpuPhysicalQueueInfo& queue
    )noexcept;
};


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

