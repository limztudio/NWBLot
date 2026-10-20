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
inline constexpr u32 s_Utf8AsciiLimit = 0x80u;
inline constexpr u32 s_Utf8TwoByteLeadFirst = 0xc2u;
inline constexpr u32 s_Utf8TwoByteLeadLast = 0xdfu;
inline constexpr u32 s_Utf8TwoBytePayloadMask = 0x1fu;
inline constexpr u32 s_Utf8TwoByteMinimum = 0x80u;
inline constexpr usize s_Utf8TwoByteTrailCount = 1u;
inline constexpr u32 s_Utf8ThreeByteLeadFirst = 0xe0u;
inline constexpr u32 s_Utf8ThreeByteLeadLast = 0xefu;
inline constexpr u32 s_Utf8ThreeBytePayloadMask = 0x0fu;
inline constexpr u32 s_Utf8ThreeByteMinimum = 0x800u;
inline constexpr usize s_Utf8ThreeByteTrailCount = 2u;
inline constexpr u32 s_Utf8FourByteLeadFirst = 0xf0u;
inline constexpr u32 s_Utf8FourByteLeadLast = 0xf4u;
inline constexpr u32 s_Utf8FourBytePayloadMask = 0x07u;
inline constexpr u32 s_Utf8FourByteMinimum = 0x10000u;
inline constexpr usize s_Utf8FourByteTrailCount = 3u;
inline constexpr u32 s_Utf8ContinuationPayloadMask = 0x3fu;
inline constexpr u32 s_Utf8ContinuationPayloadBits = 6u;
inline constexpr u32 s_UnicodeMaxScalar = 0x10ffffu;
inline constexpr u32 s_UnicodeSurrogateFirst = 0xd800u;
inline constexpr u32 s_UnicodeSurrogateLast = 0xdfffu;
inline constexpr u32 s_UnicodeSpace = 0x20u;
inline constexpr u32 s_UnicodeDelete = 0x7fu;
inline constexpr u32 s_UnicodeC1Last = 0x9fu;
inline constexpr u32 s_UnicodeLineSeparator = 0x2028u;
inline constexpr u32 s_UnicodeParagraphSeparator = 0x2029u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextLayoutStatus::Enum ValidateTextRequest(const ShapeRequest& request, bool allowLineBreaks)noexcept{
    using namespace __hidden_ui_text_types;
    if(
        request.text.size() > s_TextMaxBytes || !IsFinite(request.fontSize) || request.fontSize < 1.0f / s_TextFixedPointScale
        || request.fontSize > s_TextMaxFontSize || request.scriptTag == 0u || request.language.empty() || request.language.size() > s_TextMaxLanguageBytes
        || (request.direction != TextDirection::LeftToRight && request.direction != TextDirection::RightToLeft)
    )
        return TextLayoutStatus::InvalidParameters;
    for(u32 byte = 0u; byte < s_ScriptTagLetterCount; ++byte){
        const u32 letter = (request.scriptTag >> (byte * s_BitsPerByte)) & s_ScriptTagLetterMask;
        if(!(letter >= 'a' && letter <= 'z') && !(letter >= 'A' && letter <= 'Z'))
            return TextLayoutStatus::InvalidParameters;
    }
    for(const char character : request.language){
        if(!(character >= 'a' && character <= 'z') && !(character >= 'A' && character <= 'Z') && !(character >= '0' && character <= '9') && character != '-')
            return TextLayoutStatus::InvalidParameters;
    }
    usize offset = 0u;
    while(offset < request.text.size()){
        const u8 first = static_cast<u8>(request.text[offset]);
        ++offset;
        u32 scalar = first;
        usize remaining = 0u;
        u32 minimum = 0u;
        if(first >= s_Utf8TwoByteLeadFirst && first <= s_Utf8TwoByteLeadLast){
            scalar = first & s_Utf8TwoBytePayloadMask;
            remaining = s_Utf8TwoByteTrailCount;
            minimum = s_Utf8TwoByteMinimum;
        }
        else if(first >= s_Utf8ThreeByteLeadFirst && first <= s_Utf8ThreeByteLeadLast){
            scalar = first & s_Utf8ThreeBytePayloadMask;
            remaining = s_Utf8ThreeByteTrailCount;
            minimum = s_Utf8ThreeByteMinimum;
        }
        else if(first >= s_Utf8FourByteLeadFirst && first <= s_Utf8FourByteLeadLast){
            scalar = first & s_Utf8FourBytePayloadMask;
            remaining = s_Utf8FourByteTrailCount;
            minimum = s_Utf8FourByteMinimum;
        }
        else if(first >= s_Utf8AsciiLimit || first == 0u)
            return TextLayoutStatus::InvalidUtf8;
        if(remaining > request.text.size() - offset)
            return TextLayoutStatus::InvalidUtf8;
        for(usize index = 0u; index < remaining; ++index){
            const u8 byte = static_cast<u8>(request.text[offset]);
            if((byte & s_Utf8ContinuationMask) != s_Utf8ContinuationMarker)
                return TextLayoutStatus::InvalidUtf8;
            scalar = (scalar << s_Utf8ContinuationPayloadBits) | (byte & s_Utf8ContinuationPayloadMask);
            ++offset;
        }
        if(scalar < minimum || scalar > s_UnicodeMaxScalar || (scalar >= s_UnicodeSurrogateFirst && scalar <= s_UnicodeSurrogateLast))
            return TextLayoutStatus::InvalidUtf8;
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

