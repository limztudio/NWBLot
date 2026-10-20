// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "grapheme.h"

#include <global/unicode/grapheme.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_grapheme{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<DecodedUtf8CodePoint> Decode(const AStringView text, const usize offset)noexcept{
    const auto decoded = DecodeUtf8CodePoint(text.substr(offset, s_Utf8MaxSequenceBytes));
    if(!decoded || (decoded->codePoint >= s_UnicodeSurrogateFirst && decoded->codePoint <= s_UnicodeSurrogateLast))
        return MakeUnexpected(Failure{});
    return *decoded;
}

static bool IsSingleLineScalar(const u32 scalar)noexcept{
    return scalar != 0u && scalar != s_UnicodeLineFeed && scalar != s_UnicodeCarriageReturn && scalar != s_UnicodeLineSeparator && scalar != s_UnicodeParagraphSeparator;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<EditBoundaryVector> GraphemeSegmentation::Build(Core::Alloc::GlobalArena& arena, const AStringView text, const bool singleLine){
    if(!Validate(text, singleLine))
        return MakeUnexpected(Failure{});
    EditBoundaryVector candidate(arena);
    candidate.reserve(text.size() + 1u);
    GraphemeBoundaryState state;
    usize offset = 0u;
    while(offset < text.size()){
        const auto decoded = __hidden_ui_grapheme::Decode(text, offset);
        NWB_ASSERT(decoded);
        const u32 scalar = decoded->codePoint;
        const GraphemeBreak::Enum current = LookupGraphemeBreak(scalar);
        const IndicConjunct::Enum indic = LookupIndicConjunct(scalar);
        const bool pictograph = IsExtendedPictographic(scalar);
        if(state.breaksBefore(current, indic, pictograph))
            candidate.push_back(offset);
        state.advance(current, indic, pictograph);
        offset += static_cast<usize>(decoded->byteCount);
    }
    candidate.push_back(text.size());
    return candidate;
}

bool GraphemeSegmentation::Validate(const AStringView text, const bool singleLine)noexcept{
    if(text.size() > Limit<i32>::s_Max || (!text.empty() && text.data() == nullptr))
        return false;
    usize offset = 0u;
    while(offset < text.size()){
        const auto decoded = __hidden_ui_grapheme::Decode(text, offset);
        if(!decoded)
            return false;
        if(singleLine && !__hidden_ui_grapheme::IsSingleLineScalar(decoded->codePoint))
            return false;
        offset += static_cast<usize>(decoded->byteCount);
    }
    return true;
}

bool GraphemeSegmentation::IsScalarBoundary(const AStringView text, const usize position)noexcept{
    if(position > text.size())
        return false;
    return position == text.size() || !IsUtf8Continuation(static_cast<u8>(text[position]));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

