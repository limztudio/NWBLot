// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "command.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


class GpuBufferStateFanIn final{
public:
    [[nodiscard]] static b8 Merge(
        CommandListResourceStateHandoff& result,
        const CommandListResourceStateHandoff& base,
        const CommandListResourceStateHandoff* const* branches,
        usize branchCount,
        usize bufferStateCapacity,
        Alloc::ScratchArena& scratchArena
    );
    [[nodiscard]] static b8 MergePermanent(
        CommandListResourceStateHandoff& result,
        const CommandListResourceStateHandoff& base,
        const CommandListResourceStateHandoff* const* branches,
        usize branchCount,
        usize permanentBufferStateCapacity,
        Alloc::ScratchArena& scratchArena
    );
};


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
