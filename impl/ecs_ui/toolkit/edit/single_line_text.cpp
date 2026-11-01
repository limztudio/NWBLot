// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "single_line_text.h"
#include "grapheme.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_single_line_text{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<SingleLineTextStep> ReadStep(const AStringView source, const usize offset)noexcept{
    // Callers admit the whole source with GraphemeSegmentation::Validate first.
    const auto decoded = DecodeUtf8Scalar(source.substr(offset, s_Utf8MaxSequenceBytes));
    NWB_ASSERT(decoded);
    const u32 scalar = decoded->codePoint;
    if(IsSingleLineRejectedScalar(scalar))
        return MakeUnexpected(Failure{});
    const b8 space = IsSingleLineSpaceScalar(scalar);
    SingleLineTextStep step;
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
    return NormalizeEditTextSteps(arena, source, maxBytes, __hidden_ui_single_line_text::ReadStep, [source](const SingleLineTextStep& step, const usize offset, AString<Core::Alloc::GlobalArena>& candidate){
        if(step.space)
            candidate.push_back(' ');
        else
            candidate.append(source.data() + offset, step.sourceBytes);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

