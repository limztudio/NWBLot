// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/paint.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_TextMaxBytes = 1024u * 1024u;
inline constexpr usize s_TextMaxFontSources = 8u;
inline constexpr usize s_TextMaxLanguageBytes = 63u;
inline constexpr f32 s_TextDefaultFontSize = 16.0f;
inline constexpr StringView s_TextDefaultLanguage = "en";
inline constexpr f32 s_TextFixedPointScale = 64.0f;
inline constexpr f64 s_TextFixedPointScaleF64 = static_cast<f64>(s_TextFixedPointScale);
inline constexpr f32 s_TextMaxFontSize = 2048.0f;
inline constexpr u32 s_TextMaxRasterPpem = 4096u;
inline constexpr f32 s_TextMaxRasterPpemF32 = static_cast<f32>(s_TextMaxRasterPpem);
inline constexpr u32 s_Utf8ContinuationMask = 0xc0u;
inline constexpr u32 s_Utf8ContinuationMarker = 0x80u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextDirection{
    enum Enum : u8{ LeftToRight, RightToLeft };
};

namespace TextLayoutStatus{
    enum Enum : u8{ Success, InvalidUtf8, UnsupportedControl, MissingGlyph, InvalidParameters, FontFailure };
};

namespace TextCaretEdge{
    enum Enum : u8{ Leading, Trailing };
};

[[nodiscard]] constexpr u32 TextScriptTag(char a, char b, char c, char d)noexcept{
    return (static_cast<u32>(a) << 24u) | (static_cast<u32>(b) << 16u) | (static_cast<u32>(c) << 8u) | static_cast<u32>(d);
}

inline constexpr u32 s_TextDefaultScriptTag = TextScriptTag('L', 'a', 't', 'n');

struct ShapeRequest{
    StringView text;
    f32 fontSize = s_TextDefaultFontSize;
    TextDirection::Enum direction = TextDirection::LeftToRight;
    u32 scriptTag = s_TextDefaultScriptTag;
    StringView language = s_TextDefaultLanguage;
};

struct FontMetrics{
    f32 ascender = 0.0f;
    f32 descender = 0.0f;
    f32 lineGap = 0.0f;
};

// This validates scalar UTF-8 and admitted label controls, not Unicode grapheme or paragraph segmentation.
[[nodiscard]] TextLayoutStatus::Enum ValidateTextRequest(const ShapeRequest& request, bool allowLineBreaks)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

