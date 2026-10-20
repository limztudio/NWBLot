// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using EditBoundaryVector = Vector<usize, Core::Alloc::GlobalArena>;

inline constexpr usize s_Utf8MaxSequenceBytes = 4u;
inline constexpr u32 s_UnicodeSurrogateFirst = 0xD800u;
inline constexpr u32 s_UnicodeSurrogateLast = 0xDFFFu;
inline constexpr u32 s_UnicodeTab = 0x9u;
inline constexpr u32 s_UnicodeLineFeed = 0xAu;
inline constexpr u32 s_UnicodeCarriageReturn = 0xDu;
inline constexpr u32 s_UnicodeSpace = 0x20u;
inline constexpr u32 s_UnicodeDelete = 0x7Fu;
inline constexpr u32 s_UnicodeNextLine = 0x85u;
inline constexpr u32 s_UnicodeC1Last = 0x9Fu;
inline constexpr u32 s_UnicodeLineSeparator = 0x2028u;
inline constexpr u32 s_UnicodeParagraphSeparator = 0x2029u;

// Unicode 17.0.0 UAX #29 revision 47 default extended grapheme boundaries, including GB9c.
class GraphemeSegmentation final{
public:
    // General segmentation accepts controls; single-line editing rejects line breaks and NUL.
    [[nodiscard]] static Expected<EditBoundaryVector> Build(Core::Alloc::GlobalArena& arena, AStringView text, bool singleLine = false);
    [[nodiscard]] static bool Validate(AStringView text, bool singleLine = false)noexcept;
    // The supplied text must have passed Validate; native preedit selections may use scalar boundaries inside graphemes.
    [[nodiscard]] static bool IsScalarBoundary(AStringView text, usize position)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

