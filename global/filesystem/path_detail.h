// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../basic_string.h"
#include "../generic.h"
#include "../type.h"
#include "../type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemPathDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DriveRootNameLength = 2u;
inline constexpr usize s_DotDotLength = 2u;
inline constexpr usize s_RelativeParentStepLength = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
concept PathChar = SameAs<CharT, char> || SameAs<CharT, wchar> || SameAs<CharT, char8_t>;

template<typename SourceT>
concept PathSourceLike = CanMakeCharView<SourceT> || CanMakeWCharView<SourceT> || requires(const SourceT& source){ BasicStringView<char8_t>(source); };

template<typename CharT>
[[nodiscard]] inline b8 IsSeparator(const CharT ch)noexcept(IsArithmetic_V<CharT>){
    return ch == static_cast<CharT>('/')
#if defined(NWB_PLATFORM_WINDOWS)
        || ch == static_cast<CharT>('\\')
#endif
    ;
}

template<typename CharT>
[[nodiscard]] inline CharT PreferredSeparator()noexcept(IsArithmetic_V<CharT>){
#if defined(NWB_PLATFORM_WINDOWS)
    return static_cast<CharT>('\\');
#else
    return static_cast<CharT>('/');
#endif
}

template<typename CharT>
[[nodiscard]] inline usize RootNameLength([[maybe_unused]] const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
#if defined(NWB_PLATFORM_WINDOWS)
    if(text.size() >= s_DriveRootNameLength && text[1] == static_cast<CharT>(':'))
        return s_DriveRootNameLength;
    if(text.size() > 2u && IsSeparator(text[0]) && IsSeparator(text[1]) && !IsSeparator(text[2])){
        usize end = 2u;
        while(end < text.size() && !IsSeparator(text[end]))
            ++end;
        return end;
    }
#endif
    return 0u;
}

template<typename CharT>
[[nodiscard]] inline usize RootDirectoryLength(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    const usize rootNameLength = RootNameLength(text);
    if(rootNameLength < text.size() && IsSeparator(text[rootNameLength]))
        return rootNameLength + 1u;
    if(rootNameLength == 0u && !text.empty() && IsSeparator(text[0]))
        return 1u;
    return 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline b8 IsDot(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return text.size() == 1u && text[0] == static_cast<CharT>('.');
}

template<typename CharT>
[[nodiscard]] inline b8 IsDotDot(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return text.size() == GlobalFilesystemPathDetail::s_DotDotLength && text[0] == static_cast<CharT>('.') && text[1] == static_cast<CharT>('.');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

