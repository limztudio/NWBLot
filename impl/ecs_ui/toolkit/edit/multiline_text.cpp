// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "multiline_text.h"
#include "grapheme.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_text{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TextStep{
    usize sourceBytes = 0u;
    usize outputBytes = 0u;
    char replacement = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsCanonicalScalar(const u32 scalar)noexcept{
    return scalar == '\n' || (scalar >= 0x20u && (scalar < 0x7Fu || scalar > 0x9Fu) && scalar != 0x2028u && scalar != 0x2029u);
}

[[nodiscard]] static Expected<TextStep> ReadStep(const AStringView source, const usize offset)noexcept{
    const auto decoded = DecodeUtf8CodePoint(source.substr(offset, 4u));
    if(!decoded || (decoded->codePoint >= 0xD800u && decoded->codePoint <= 0xDFFFu))
        return MakeUnexpected(Failure{});
    const u32 scalar = decoded->codePoint;
    TextStep step;
    step.sourceBytes = static_cast<usize>(decoded->byteCount);
    const bool lineBreak = scalar == '\r' || scalar == 0x85u || scalar == 0x2028u || scalar == 0x2029u;
    if(lineBreak || scalar == '\t'){
        step.replacement = lineBreak ? '\n' : ' ';
        step.outputBytes = 1u;
        if(scalar == '\r' && offset + step.sourceBytes < source.size() && source[offset + step.sourceBytes] == '\n')
            ++step.sourceBytes;
    }
    else{
        if(!IsCanonicalScalar(scalar))
            return MakeUnexpected(Failure{});
        step.outputBytes = step.sourceBytes;
    }
    return step;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateMultilineText(const AStringView text)noexcept{
    if(!GraphemeSegmentation::Validate(text))
        return false;
    usize offset = 0u;
    while(offset < text.size()){
        const auto decoded = DecodeUtf8CodePoint(text.substr(offset, 4u));
        if(!decoded || !__hidden_ui_multiline_text::IsCanonicalScalar(decoded->codePoint))
            return false;
        offset += static_cast<usize>(decoded->byteCount);
    }
    return true;
}

Expected<AString<Core::Alloc::GlobalArena>, EditTextStatus::Enum> NormalizeMultilineText(
    Core::Alloc::GlobalArena& arena, const AStringView source, const usize maxBytes
){
    if(!GraphemeSegmentation::Validate(source))
        return MakeUnexpected(EditTextStatus::InvalidText);
    usize normalizedBytes = 0u;
    usize offset = 0u;
    while(offset < source.size()){
        const auto step = __hidden_ui_multiline_text::ReadStep(source, offset);
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
        const auto step = __hidden_ui_multiline_text::ReadStep(source, offset);
        if(!step)
            return MakeUnexpected(EditTextStatus::InvalidText);
        if(step->replacement != 0)
            candidate.push_back(step->replacement);
        else
            candidate.append(source.data() + offset, step->sourceBytes);
        offset += step->sourceBytes;
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

