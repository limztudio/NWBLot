// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextLayoutStatus::Enum ValidateTextRequest(const ShapeRequest& request, bool allowLineBreaks)noexcept{
    if(
        request.text.size() > s_TextMaxBytes || !IsFinite(request.fontSize) || request.fontSize < 1.0f / 64.0f
        || request.fontSize > 2048.0f || request.scriptTag == 0u || request.language.empty() || request.language.size() > 63u
        || (request.direction != TextDirection::LeftToRight && request.direction != TextDirection::RightToLeft)
    )
        return TextLayoutStatus::InvalidParameters;
    for(u32 byte = 0u; byte < 4u; ++byte){
        const u32 letter = (request.scriptTag >> (byte * 8u)) & 0xffu;
        if(!(letter >= 'a' && letter <= 'z') && !(letter >= 'A' && letter <= 'Z'))
            return TextLayoutStatus::InvalidParameters;
    }
    for(const char character : request.language){
        if(
            !(character >= 'a' && character <= 'z') && !(character >= 'A' && character <= 'Z')
            && !(character >= '0' && character <= '9') && character != '-'
        )
            return TextLayoutStatus::InvalidParameters;
    }
    usize offset = 0u;
    while(offset < request.text.size()){
        const u8 first = static_cast<u8>(request.text[offset]);
        ++offset;
        u32 scalar = first;
        usize remaining = 0u;
        u32 minimum = 0u;
        if(first >= 0xc2u && first <= 0xdfu){
            scalar = first & 0x1fu;
            remaining = 1u;
            minimum = 0x80u;
        }
        else if(first >= 0xe0u && first <= 0xefu){
            scalar = first & 0x0fu;
            remaining = 2u;
            minimum = 0x800u;
        }
        else if(first >= 0xf0u && first <= 0xf4u){
            scalar = first & 0x07u;
            remaining = 3u;
            minimum = 0x10000u;
        }
        else if(first >= 0x80u || first == 0u)
            return TextLayoutStatus::InvalidUtf8;
        if(remaining > request.text.size() - offset)
            return TextLayoutStatus::InvalidUtf8;
        for(usize index = 0u; index < remaining; ++index){
            const u8 byte = static_cast<u8>(request.text[offset]);
            if((byte & 0xc0u) != 0x80u)
                return TextLayoutStatus::InvalidUtf8;
            scalar = (scalar << 6u) | (byte & 0x3fu);
            ++offset;
        }
        if(scalar < minimum || scalar > 0x10ffffu || (scalar >= 0xd800u && scalar <= 0xdfffu))
            return TextLayoutStatus::InvalidUtf8;
        if(scalar == '\r'){
            if(!allowLineBreaks || offset == request.text.size() || request.text[offset] != '\n')
                return TextLayoutStatus::UnsupportedControl;
        }
        else if(scalar == '\n'){
            if(!allowLineBreaks)
                return TextLayoutStatus::UnsupportedControl;
        }
        else if(scalar < 0x20u || (scalar >= 0x7fu && scalar <= 0x9fu) || scalar == 0x2028u || scalar == 0x2029u)
            return TextLayoutStatus::UnsupportedControl;
    }
    return TextLayoutStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

