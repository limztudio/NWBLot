// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "grapheme.h"

#include <global/unicode/grapheme.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_grapheme{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 IsSingleLineScalar(const u32 scalar)noexcept{
    return scalar != 0u && scalar != s_UnicodeLineFeed && scalar != s_UnicodeCarriageReturn && scalar != s_UnicodeLineSeparator && scalar != s_UnicodeParagraphSeparator;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<EditBoundaryVector> GraphemeSegmentation::Build(Core::Alloc::GlobalArena& arena, const AStringView text, const b8 singleLine){
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

b8 GraphemeSegmentation::Validate(const AStringView text, const b8 singleLine)noexcept{
    if(text.size() > Limit<i32>::s_Max || (!text.empty() && text.data() == nullptr))
        return false;
    usize offset = 0u;
    while(offset < text.size()){
        const auto decoded = DecodeUtf8Scalar(text.substr(offset, s_Utf8MaxSequenceBytes));
        if(!decoded)
            return false;
        if(singleLine && !__hidden_ui_grapheme::IsSingleLineScalar(decoded->codePoint))
            return false;
        offset += static_cast<usize>(decoded->byteCount);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

