// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command.h"

#include "gpu_buffer_state_fan_in.h"
#include "gpu_texture_state_fan_in.h"

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CommandListResourceStateHandoff::buildFanIn(const CommandListResourceStateHandoff& base, const CommandListResourceStateHandoff* const* branches, const usize branchCount, Alloc::ScratchArena& scratchArena){
    if(this == &base)
        return false;

    if(!base.valid() || base.m_deviceGeneration == 0u || (branchCount != 0u && !branches)){
        reset();
        return false;
    }

    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        const CommandListResourceStateHandoff* branch = branches[branchIndex];
        if(branch == this)
            return false;

        if(!branch || !branch->valid() || branch->m_deviceGeneration != base.m_deviceGeneration){
            reset();
            return false;
        }
    }

    usize textureStateCapacity = base.m_textureStates.size();
    usize bufferStateCapacity = base.m_bufferStates.size();
    usize permanentTextureStateCapacity = base.m_permanentTextureStates.size();
    usize permanentBufferStateCapacity = base.m_permanentBufferStates.size();
    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        const CommandListResourceStateHandoff& branch = *branches[branchIndex];
        if(
            AddOverflows<usize>(textureStateCapacity, branch.m_textureStates.size())
            || AddOverflows<usize>(bufferStateCapacity, branch.m_bufferStates.size())
            || AddOverflows<usize>(permanentTextureStateCapacity, branch.m_permanentTextureStates.size())
            || AddOverflows<usize>(permanentBufferStateCapacity, branch.m_permanentBufferStates.size())
        ){
            reset();
            return false;
        }
        textureStateCapacity += branch.m_textureStates.size();
        bufferStateCapacity += branch.m_bufferStates.size();
        permanentTextureStateCapacity += branch.m_permanentTextureStates.size();
        permanentBufferStateCapacity += branch.m_permanentBufferStates.size();
    }

    if(AddOverflows<usize>(bufferStateCapacity, bufferStateCapacity) || AddOverflows<usize>(branchCount, 1u)){
        reset();
        return false;
    }

    reset();
    m_deviceGeneration = base.m_deviceGeneration;

    m_textureStates.reserve(textureStateCapacity);
    m_bufferStates.reserve(bufferStateCapacity + bufferStateCapacity);
    m_permanentTextureStates.reserve(permanentTextureStateCapacity);
    m_permanentBufferStates.reserve(permanentBufferStateCapacity);

    if(!GpuTextureStateFanIn::MergeTransient(*this, base, branches, branchCount, textureStateCapacity, scratchArena))
        return false;

    if(!GpuBufferStateFanIn::Merge(*this, base, branches, branchCount, bufferStateCapacity, scratchArena))
        return false;

    if(!GpuTextureStateFanIn::MergePermanent(*this, base, branches, branchCount, permanentTextureStateCapacity, scratchArena))
        return false;

    if(!GpuBufferStateFanIn::MergePermanent(*this, base, branches, branchCount, permanentBufferStateCapacity, scratchArena))
        return false;

    m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

