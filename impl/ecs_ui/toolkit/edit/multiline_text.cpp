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

[[nodiscard]] static bool ReadStep(const AStringView source, const usize offset, TextStep& step){
    u32 scalar = 0u;
    const i32 decoded = DecodeUtf8CodePoint(source.substr(offset, 4u), scalar);
    if(decoded <= 0 || (scalar >= 0xD800u && scalar <= 0xDFFFu))
        return false;
    step.sourceBytes = static_cast<usize>(decoded);
    const bool lineBreak = scalar == '\r' || scalar == 0x85u || scalar == 0x2028u || scalar == 0x2029u;
    if(lineBreak || scalar == '\t'){
        step.replacement = lineBreak ? '\n' : ' ';
        step.outputBytes = 1u;
        if(scalar == '\r' && offset + step.sourceBytes < source.size() && source[offset + step.sourceBytes] == '\n')
            ++step.sourceBytes;
    }
    else{
        if(!IsCanonicalScalar(scalar))
            return false;
        step.outputBytes = step.sourceBytes;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateMultilineText(const AStringView text){
    if(!GraphemeSegmentation::Validate(text))
        return false;
    usize offset = 0u;
    while(offset < text.size()){
        u32 scalar = 0u;
        const i32 decoded = DecodeUtf8CodePoint(text.substr(offset, 4u), scalar);
        if(decoded <= 0 || !__hidden_ui_multiline_text::IsCanonicalScalar(scalar))
            return false;
        offset += static_cast<usize>(decoded);
    }
    return true;
}

EditTextStatus::Enum NormalizeMultilineText(
    const AStringView source, AString<Core::Alloc::GlobalArena>& output, const usize maxBytes){
    if(!GraphemeSegmentation::Validate(source))
        return EditTextStatus::InvalidText;
    usize normalizedBytes = 0u;
    usize offset = 0u;
    while(offset < source.size()){
        __hidden_ui_multiline_text::TextStep step;
        if(!__hidden_ui_multiline_text::ReadStep(source, offset, step))
            return EditTextStatus::InvalidText;
        if(step.outputBytes > maxBytes - normalizedBytes)
            return EditTextStatus::TooLarge;
        normalizedBytes += step.outputBytes;
        offset += step.sourceBytes;
    }
    AString<Core::Alloc::GlobalArena> candidate(output.get_allocator());
    candidate.reserve(normalizedBytes);
    offset = 0u;
    while(offset < source.size()){
        __hidden_ui_multiline_text::TextStep step;
        if(!__hidden_ui_multiline_text::ReadStep(source, offset, step))
            return EditTextStatus::InvalidText;
        if(step.replacement != 0)
            candidate.push_back(step.replacement);
        else
            candidate.append(source.data() + offset, step.sourceBytes);
        offset += step.sourceBytes;
    }
    output = Move(candidate);
    return EditTextStatus::Accepted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

