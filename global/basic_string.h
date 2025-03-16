// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <string_view>
#include <string>
#include <format>

#include "generic.h"
#include "type.h"

#ifdef NWB_PLATFORM_WINDOWS
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{
    namespace Core{
        namespace Alloc{
            template<typename T>
            class GeneralAllocator;
        };
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using BasicStringView = std::basic_string_view<T>;
using AStringView = BasicStringView<char>;
using WStringView = BasicStringView<wchar>;
using TStringView = BasicStringView<tchar>;

template <typename T>
using BasicString = std::basic_string<T>;
using AString = BasicString<char>;
using WString = BasicString<wchar>;
using TString = BasicString<tchar>;

using AFormatContext = std::format_context;
using WFormatContext = std::wformat_context;
template <typename T>
using BasicFormatContext = Conditional_T<SameAs<char, T>, AFormatContext, Conditional_T<SameAs<wchar, T>, WFormatContext, void>>;
using TStringFormatContext = BasicFormatContext<tchar>;

template <typename T>
using BasicFormatArgs = std::basic_format_args<BasicFormatContext<T>>;
using AFormatArgs = BasicFormatArgs<char>;
using WFormatArgs = BasicFormatArgs<wchar>;
using TStringFormatArgs = BasicFormatArgs<tchar>;

template <typename... T>
using AFormatString = std::format_string<T...>;
template <typename... T>
using WFormatString = std::wformat_string<T...>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_basic_string{
    template<typename In>
    concept FromWcharView = IsConvertible_V<In, WStringView>;

    template<typename In>
    concept FromCharView = IsConvertible_V<In, AStringView>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename In> requires __hidden_basic_string::FromWcharView<In>
inline AString stringConvert(const In& raw){
    WStringView src(raw);
    if(src.empty())
        return AString();
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), nullptr, 0, nullptr, nullptr);
    assert(len != 0);
    AString dst(len, 0);
    WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), dst.data(), len, nullptr, nullptr);
    return dst;
#endif
}
template<typename In> requires __hidden_basic_string::FromWcharView<In>
inline AString stringConvert(In&& raw){
    WStringView src(Move(raw));
    if(src.empty())
        return AString();
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), nullptr, 0, nullptr, nullptr);
    assert(len != 0);
    AString dst(len, 0);
    WideCharToMultiByte(CP_ACP, 0, src.data(), static_cast<int>(src.length()), dst.data(), len, nullptr, nullptr);
    return dst;
#endif
}
template<typename In>
inline AString stringConvert(const In& src){ return src; }
template<typename In>
inline AString stringConvert(In&& src){ return src; }

template<typename In> requires __hidden_basic_string::FromCharView<In>
inline WString stringConvert(const In& raw){
    AStringView src(raw);
    if(src.empty())
        return WString();
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0);
    assert(len != 0);
    WString dst(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len);
    return dst;
#endif
}
template<typename In> requires __hidden_basic_string::FromCharView<In>
inline WString stringConvert(In&& raw){
    AStringView src(Move(raw));
    if(src.empty())
        return WString();
#ifdef NWB_PLATFORM_WINDOWS
    const auto len = MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), nullptr, 0);
    assert(len != 0);
    WString dst(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, src.data(), static_cast<int>(src.length()), dst.data(), len);
    return dst;
#endif
}
template<typename In>
inline WString stringConvert(const In& src){ return src; }
template<typename In>
inline WString stringConvert(In&& src){ return src; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename... T>
inline AString stringFormat(AFormatString<T...> fmt, T&&... args){
    return std::vformat(fmt.get(), std::make_format_args<AFormatContext>(args...));
}
template <typename... T>
inline WString stringFormat(WFormatString<T...> fmt, T&&... args){
    return std::vformat(fmt.get(), std::make_format_args<WFormatContext>(args...));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

