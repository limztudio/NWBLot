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
    const auto decoded = DecodeUtf8CodePoint(source.substr(offset, 4u));
    if(!decoded || (decoded->codePoint >= 0xD800u && decoded->codePoint <= 0xDFFFu))
        return MakeUnexpected(Failure{});
    const u32 scalar = decoded->codePoint;
    const bool space = (scalar >= 0x9u && scalar <= 0xDu) || scalar == 0x85u || scalar == 0x2028u || scalar == 0x2029u;
    if(!space && (scalar < 0x20u || scalar == 0x7Fu))
        return MakeUnexpected(Failure{});
    TextStep step;
    step.sourceBytes = static_cast<usize>(decoded->byteCount);
    step.outputBytes = space ? 1u : step.sourceBytes;
    step.space = space;
    if(scalar == 0xDu && offset + step.sourceBytes < source.size() && source[offset + step.sourceBytes] == '\n')
        ++step.sourceBytes;
    return step;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AString<Core::Alloc::GlobalArena>, EditTextStatus::Enum> NormalizeSingleLineText(
    Core::Alloc::GlobalArena& arena, const AStringView source, const usize maxBytes
){
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
        if(!step)
            return MakeUnexpected(EditTextStatus::InvalidText);
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

