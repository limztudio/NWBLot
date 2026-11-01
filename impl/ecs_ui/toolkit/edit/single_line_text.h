// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>
#include <impl/ecs_ui/toolkit/edit/types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CRLF becomes one space.
// Other line breaks and tabs become spaces; NUL and other ASCII controls are rejected.
// UTF8 and normalized byte length are checked before allocation.
template<typename ReadStep, typename EmitStep>
[[nodiscard]] Expected<AString<Core::Alloc::GlobalArena>, EditTextStatus::Enum> NormalizeEditTextSteps(
    Core::Alloc::GlobalArena& arena,
    const AStringView source,
    const usize maxBytes,
    ReadStep readStep,
    EmitStep emitStep
){
    usize normalizedBytes = 0u;
    usize offset = 0u;
    while(offset < source.size()){
        const auto step = readStep(source, offset);
        if(!step)
            return MakeUnexpected(EditTextStatus::InvalidText);
        if(step->outputBytes > maxBytes - normalizedBytes)
            return MakeUnexpected(EditTextStatus::TooLarge);
        normalizedBytes += step->outputBytes;
        offset += step->sourceBytes;
    }
    AString<Core::Alloc::GlobalArena> candidate(arena);
    candidate.reserve(normalizedBytes);
    offset = 0u;
    while(offset < source.size()){
        const auto step = readStep(source, offset);
        NWB_ASSERT(step);
        emitStep(*step, offset, candidate);
        offset += step->sourceBytes;
    }
    return candidate;
}

// CRLF becomes one space.
// Other line breaks and tabs become spaces; NUL and other ASCII controls are rejected.
// UTF8 and normalized byte length are checked before allocation.
[[nodiscard]] Expected<AString<Core::Alloc::GlobalArena>, EditTextStatus::Enum> NormalizeSingleLineText(Core::Alloc::GlobalArena& arena, AStringView source, usize maxBytes);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

