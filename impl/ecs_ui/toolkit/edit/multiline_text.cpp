// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "multiline_text.h"
#include "grapheme.h"
#include "single_line_text.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_text{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<MultilineTextStep> ReadStep(const AStringView source, const usize offset)noexcept{
    // Callers admit the whole source with GraphemeSegmentation::Validate first.
    const auto decoded = DecodeUtf8Scalar(source.substr(offset, s_Utf8MaxSequenceBytes));
    NWB_ASSERT(decoded);
    MultilineTextStep step;
    if(!TryBuildMultilineTextStep(source, offset, decoded->codePoint, static_cast<usize>(decoded->byteCount), step))
        return MakeUnexpected(Failure{});
    return step;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ValidateMultilineText(const AStringView text)noexcept{
    if(!GraphemeSegmentation::Validate(text))
        return false;
    usize offset = 0u;
    while(offset < text.size()){
        const auto decoded = DecodeUtf8Scalar(text.substr(offset, s_Utf8MaxSequenceBytes));
        NWB_ASSERT(decoded);
        if(!IsCanonicalMultilineScalar(decoded->codePoint))
            return false;
        offset += static_cast<usize>(decoded->byteCount);
    }
    return true;
}

Expected<AString<Core::Alloc::GlobalArena>, EditTextStatus::Enum> NormalizeMultilineText(Core::Alloc::GlobalArena& arena, const AStringView source, const usize maxBytes){
    if(!GraphemeSegmentation::Validate(source))
        return MakeUnexpected(EditTextStatus::InvalidText);
    return NormalizeEditTextSteps(arena, source, maxBytes, __hidden_ui_multiline_text::ReadStep, [source](const MultilineTextStep& step, const usize offset, AString<Core::Alloc::GlobalArena>& candidate){
        if(step.replacement != 0u)
            candidate.push_back(static_cast<char>(step.replacement));
        else
            candidate.append(source.data() + offset, step.sourceBytes);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

