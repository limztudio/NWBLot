// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "command.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


class GpuTextureStateFanIn final{
public:
    [[nodiscard]] static b8 mergeTransient(CommandListResourceStateHandoff& result, const CommandListResourceStateHandoff& base, const CommandListResourceStateHandoff* const* branches, usize branchCount, usize textureStateCapacity, Alloc::ScratchArena& scratchArena);
    [[nodiscard]] static b8 mergePermanent(CommandListResourceStateHandoff& result, const CommandListResourceStateHandoff& base, const CommandListResourceStateHandoff* const* branches, usize branchCount, usize permanentTextureStateCapacity, Alloc::ScratchArena& scratchArena);
};


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
