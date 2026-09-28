// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ui/paint.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_TextMaxBytes = 1024u * 1024u;

namespace TextDirection{
    enum Enum : u8{ LeftToRight, RightToLeft };
};

namespace TextLayoutStatus{
    enum Enum : u8{ Success, InvalidUtf8, UnsupportedControl, MissingGlyph, InvalidParameters, FontFailure };
};

namespace TextCaretEdge{
    enum Enum : u8{ Leading, Trailing };
};

[[nodiscard]] constexpr u32 TextScriptTag(char a, char b, char c, char d){
    return (static_cast<u32>(a) << 24u) | (static_cast<u32>(b) << 16u) | (static_cast<u32>(c) << 8u) | static_cast<u32>(d);
}

struct ShapeRequest{
    StringView text;
    f32 fontSize = 16.0f;
    TextDirection::Enum direction = TextDirection::LeftToRight;
    u32 scriptTag = TextScriptTag('L', 'a', 't', 'n');
    StringView language = "en";
};

struct FontMetrics{
    f32 ascender = 0.0f;
    f32 descender = 0.0f;
    f32 lineGap = 0.0f;
};

// This validates scalar UTF-8 and admitted label controls, not Unicode grapheme or paragraph segmentation.
[[nodiscard]] TextLayoutStatus::Enum ValidateTextRequest(const ShapeRequest& request, bool allowLineBreaks);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

