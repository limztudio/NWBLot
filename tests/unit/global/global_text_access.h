// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/compact_string.h>
#include <global/fixed_buffer.h>
#include <global/overflow.h>
#include <global/scope_exit.h>
#include <global/string_hash.h>
#include <global/text_replacements.h>
#include <global/text_utils.h>
#include <global/utf8_text.h>
#include <tests/unit/global/scope_case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases{

using __hidden_scope_exit_tests::CleanupState;

struct MoveOnlyCleanup : NoCopy{
public:
    CleanupState* state;


public:
    explicit MoveOnlyCleanup(CleanupState& cleanupState)noexcept
        : state(&cleanupState)
    {}
    MoveOnlyCleanup(MoveOnlyCleanup&& other)noexcept
        : state(other.state)
    {
        other.state = nullptr;
    }
    ~MoveOnlyCleanup()noexcept{
        if(state)
            ++state->destructions;
    }


public:
    void operator()()noexcept{ ++state->calls; }
};

template<typename T>
concept CanTrimView = requires{ TrimView(DeclVal<T>()); TrimLeftView(DeclVal<T>()); };

static_assert(CanTrimView<ACompactString&>);
static_assert(CanTrimView<const ACompactString&>);
static_assert(!CanTrimView<ACompactString>);
static_assert(!CanTrimView<const ACompactString>);
static_assert(CanTrimView<AStringView>);
static_assert(CanTrimView<AStringView&>);
static_assert(CanTrimView<AString<::ArenaBase>&>);
static_assert(!CanTrimView<AString<::ArenaBase>>);
static_assert(LiteralView("a\0b").size() == 3u);
static_assert(::TryMultiply<u64>(0u, Limit<u64>::s_Max) && *::TryMultiply<u64>(0u, Limit<u64>::s_Max) == 0u);
static_assert(::TryMultiply<i64>(Limit<i64>::s_Min, 1) && *::TryMultiply<i64>(Limit<i64>::s_Min, 1) == Limit<i64>::s_Min);

struct ThrowingReplacementComparison{
public:
    b8 value;
    explicit ThrowingReplacementComparison(const b8 inValue)noexcept : value(inValue){}
    ThrowingReplacementComparison(const ThrowingReplacementComparison&){ throw RuntimeException("comparison copy failure"); }
    explicit operator b8()const noexcept{ return value; }
};

struct ReplacementOffset{
public:
    usize value;
    operator usize()const noexcept{ return value; }
};

[[nodiscard]] FOUNDATION_INLINE const ThrowingReplacementComparison& operator>(const ReplacementOffset lhs, const ReplacementOffset rhs)noexcept{
    static const ThrowingReplacementComparison s_True(true);
    static const ThrowingReplacementComparison s_False(false);
    return lhs.value > rhs.value ? s_True : s_False;
}

struct PointerFallbackProbe{
    usize length = 0u;
    u32 selected = 0u;
    [[nodiscard]] usize size()const noexcept{ return length; }
    PointerFallbackProbe& operator+=(char)noexcept{ ++length; return *this; }
    PointerFallbackProbe& operator+=(const char*&&)noexcept{ selected = 1u; ++length; return *this; }
    PointerFallbackProbe& operator+=(const char* const&){ selected = 2u; ++length; return *this; }
};

using DecimalDigits = Array<char, s_UsizeDecimalDigitCount>;
using WideOverflowBuffer = Array<wchar, WCompactString::s_MaxLength + 2u>;
using NarrowChar = char;
using WideChar = wchar;
using WideCompact = WCompactString;
inline constexpr usize s_WideLimit = WCompactString::s_MaxLength;
inline constexpr WStringView s_UnchangedWide = L"unchanged";
inline constexpr WStringView s_MixedWideLines = L"a\r\nb\n\rc";
inline constexpr WStringView s_ExpandedWideLines = L"a\r\nb\r\n\rc";
inline constexpr const char* s_AbsentText = nullptr;
static_assert(sizeof(usize) == sizeof(u32) || sizeof(usize) == sizeof(u64));

template<typename T>
[[nodiscard]] inline auto GuardCleanup(T&& cleanup){
    return ::ScopeExit([fn = Forward<T>(cleanup)]()mutable noexcept{ fn(); });
}

template<usize Count>
[[nodiscard]] inline WStringView WidePrefix(const Array<wchar, Count>& data, usize length)noexcept{
    return { data.data(), length };
}

template<usize Count>
[[nodiscard]] inline usize ExpandWideLines(WStringView text, Array<wchar, Count>& destination)noexcept{
    return ::CopyWithCrBeforeBareLf(text, destination.data());
}

template<typename Edits, typename Text>
inline void ApplyEdits(Edits& edits, Text& text){
    using Item = RemoveReference_T<decltype(edits[0])>;
    ::ApplyTextReplacements(Span<Item>(edits.data(), edits.size()), text);
}

template<typename T, typename Arg>
[[nodiscard]] inline usize HashText(const Arg& value){ return TransparentStringHash<T>{}(value); }
template<typename T>
[[nodiscard]] inline b8 EqualText(const T& text, AStringView view){ return TransparentStringEqual<T>{}(text, view); }
[[nodiscard]] inline Span<const u8> TextBytes(AStringView text){
    return { reinterpret_cast<const u8*>(text.data()), text.size() };
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

