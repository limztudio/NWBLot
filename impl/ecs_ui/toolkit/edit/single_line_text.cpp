// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "single_line_text.h"
#include "grapheme.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_single_line_text{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TextStep{
    usize sourceBytes = 0u;
    usize outputBytes = 0u;
    bool space = false;
};

static Expected<TextStep> ReadStep(const AStringView source, const usize offset)noexcept{
    // Callers admit the whole source with GraphemeSegmentation::Validate first.
    const auto decoded = DecodeUtf8CodePoint(source.substr(offset, s_Utf8MaxSequenceBytes));
    NWB_ASSERT(decoded && !(decoded->codePoint >= s_UnicodeSurrogateFirst && decoded->codePoint <= s_UnicodeSurrogateLast));
    const u32 scalar = decoded->codePoint;
    const bool space = (scalar >= s_UnicodeTab && scalar <= s_UnicodeCarriageReturn) || scalar == s_UnicodeNextLine || scalar == s_UnicodeLineSeparator || scalar == s_UnicodeParagraphSeparator;
    if(!space && (scalar < s_UnicodeSpace || scalar == s_UnicodeDelete))
        return MakeUnexpected(Failure{});
    TextStep step;
    step.sourceBytes = static_cast<usize>(decoded->byteCount);
    step.outputBytes = space ? 1u : step.sourceBytes;
    step.space = space;
    if(scalar == s_UnicodeCarriageReturn && offset + step.sourceBytes < source.size() && source[offset + step.sourceBytes] == '\n')
        ++step.sourceBytes;
    return step;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AString<Core::Alloc::GlobalArena>, EditTextStatus::Enum> NormalizeSingleLineText(Core::Alloc::GlobalArena& arena, const AStringView source, const usize maxBytes){
    if(!GraphemeSegmentation::Validate(source, false))
        return MakeUnexpected(EditTextStatus::InvalidText);
    usize normalizedBytes = 0u;
    usize offset = 0u;
    while(offset < source.size()){
        const auto step = __hidden_ui_single_line_text::ReadStep(source, offset);
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
        const auto step = __hidden_ui_single_line_text::ReadStep(source, offset);
        NWB_ASSERT(step);
        if(step->space)
            candidate.push_back(' ');
        else
            candidate.append(source.data() + offset, step->sourceBytes);
        offset += step->sourceBytes;
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

