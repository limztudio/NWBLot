// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/scratch.h>
#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Checks immutable state agreement and buffer bounds; native readiness, texture bounds and ownership stay in preflight.
class GpuInitialStateHandoffValidation final : NoCopy{
private:
    using StateIndex = Vector<usize, Alloc::ScratchArena>;


public:
    [[nodiscard]] static bool ValidBufferRange(const BufferRange& range, u64 bufferSize)noexcept;


public:
    GpuInitialStateHandoffValidation(const CommandListResourceStateHandoff& states, Alloc::ScratchArena& scratchArena)noexcept
        : m_states(states)
        , m_scratchArena(scratchArena)
    {}


public:
    [[nodiscard]] bool validate();
    [[nodiscard]] Expected<ResourceStates::Mask> permanentTextureState(Texture* texture)const noexcept;
    [[nodiscard]] Expected<ResourceStates::Mask> permanentBufferState(Buffer* buffer)const noexcept;


private:
    const CommandListResourceStateHandoff& m_states;
    Alloc::ScratchArena& m_scratchArena;
    Optional<StateIndex> m_permanentTextureIndex;
    Optional<StateIndex> m_permanentBufferIndex;
    Optional<StateIndex> m_textureIndex;
    Optional<StateIndex> m_bufferIndex;
    bool m_permanentTexturesIndexed = false;
    bool m_permanentBuffersIndexed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

