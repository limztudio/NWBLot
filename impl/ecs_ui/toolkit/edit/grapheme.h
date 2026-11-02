// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>
#include <impl/ecs_ui/toolkit/edit/types.adhd.h>

#include <global/unicode/grapheme.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using EditBoundaryVector = Vector<usize, Core::Alloc::GlobalArena>;

// Unicode 17.0.0 UAX #29 revision 47 default extended grapheme boundaries, including GB9c.
namespace GraphemeSegmentation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 Validate(const AStringView text, const b8 singleLine = false)noexcept{
    return ValidateGraphemeText(text, singleLine);
}

// General segmentation accepts controls; single-line editing rejects line breaks and NUL.
[[nodiscard]] inline Expected<EditBoundaryVector> Build(Core::Alloc::GlobalArena& arena, const AStringView text, const b8 singleLine = false){
    if(!Validate(text, singleLine))
        return MakeUnexpected(Failure{});
    EditBoundaryVector candidate(arena);
    candidate.reserve(text.size() + 1u);
    GraphemeBoundaryState state;
    usize offset = 0u;
    while(offset < text.size()){
        const auto decoded = DecodeUtf8Scalar(text.substr(offset, s_Utf8MaxSequenceBytes));
        NWB_ASSERT(decoded);
        const u32 scalar = decoded->codePoint;
        const GraphemeBreak::Enum current = LookupGraphemeBreak(scalar);
        const IndicConjunct::Enum indic = LookupIndicConjunct(scalar);
        const b8 pictograph = IsExtendedPictographic(scalar);
        if(state.breaksBefore(current, indic, pictograph))
            candidate.push_back(offset);
        state.advance(current, indic, pictograph);
        offset += static_cast<usize>(decoded->byteCount);
    }
    candidate.push_back(text.size());
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

