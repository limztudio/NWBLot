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

static bool ReadStep(const AStringView source, const usize offset, TextStep& step){
    u32 scalar = 0u;
    const i32 decoded = DecodeUtf8CodePoint(source.substr(offset, 4u), scalar);
    if(decoded <= 0 || (scalar >= 0xD800u && scalar <= 0xDFFFu))
        return false;
    const bool space = (scalar >= 0x9u && scalar <= 0xDu) || scalar == 0x85u || scalar == 0x2028u || scalar == 0x2029u;
    if(!space && (scalar < 0x20u || scalar == 0x7Fu))
        return false;
    step.sourceBytes = static_cast<usize>(decoded);
    step.outputBytes = space ? 1u : step.sourceBytes;
    step.space = space;
    if(scalar == 0xDu && offset + step.sourceBytes < source.size() && source[offset + step.sourceBytes] == '\n')
        ++step.sourceBytes;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditTextStatus::Enum NormalizeSingleLineText(
    const AStringView source, AString<Core::Alloc::GlobalArena>& output, const usize maxBytes
){
    if(!GraphemeSegmentation::Validate(source, false))
        return EditTextStatus::InvalidText;
    usize normalizedBytes = 0u;
    usize offset = 0u;
    while(offset < source.size()){
        __hidden_ui_single_line_text::TextStep step;
        if(!__hidden_ui_single_line_text::ReadStep(source, offset, step))
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
        __hidden_ui_single_line_text::TextStep step;
        if(!__hidden_ui_single_line_text::ReadStep(source, offset, step))
            return EditTextStatus::InvalidText;
        if(step.space)
            candidate.push_back(' ');
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

