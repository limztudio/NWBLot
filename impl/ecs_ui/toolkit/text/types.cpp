// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_types{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ScriptTagLetterCount = 4u;
inline constexpr u32 s_ScriptTagLetterMask = 0xffu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextLayoutStatus::Enum ValidateTextRequest(const ShapeRequest& request, b8 allowLineBreaks)noexcept{
    using namespace __hidden_ui_text_types;
    if(
        request.text.size() > s_TextMaxBytes || !IsFinite(request.fontSize) || request.fontSize < 1.0f / s_TextFixedPointScale
        || request.fontSize > s_TextMaxFontSize || request.scriptTag == 0u || request.language.empty() || request.language.size() > s_TextMaxLanguageBytes
        || (request.direction != TextDirection::LeftToRight && request.direction != TextDirection::RightToLeft)
    )
        return TextLayoutStatus::InvalidParameters;
    for(u32 byte = 0u; byte < s_ScriptTagLetterCount; ++byte){
        const u32 letter = (request.scriptTag >> (byte * s_BitsPerByte)) & s_ScriptTagLetterMask;
        if(!IsAsciiAlpha(letter))
            return TextLayoutStatus::InvalidParameters;
    }
    for(const char character : request.language){
        if(!IsAsciiAlphaNumeric(character) && character != '-')
            return TextLayoutStatus::InvalidParameters;
    }
    usize offset = 0u;
    while(offset < request.text.size()){
        const auto decoded = DecodeUtf8Scalar(request.text.substr(offset));
        if(!decoded || decoded->codePoint == 0u)
            return TextLayoutStatus::InvalidUtf8;
        const u32 scalar = decoded->codePoint;
        offset += static_cast<usize>(decoded->byteCount);
        if(scalar == '\r'){
            if(!allowLineBreaks || offset == request.text.size() || request.text[offset] != '\n')
                return TextLayoutStatus::UnsupportedControl;
        }
        else if(scalar == '\n'){
            if(!allowLineBreaks)
                return TextLayoutStatus::UnsupportedControl;
        }
        else if(scalar < s_UnicodeSpace || (scalar >= s_UnicodeDelete && scalar <= s_UnicodeC1Last) || scalar == s_UnicodeLineSeparator || scalar == s_UnicodeParagraphSeparator)
            return TextLayoutStatus::UnsupportedControl;
    }
    return TextLayoutStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

