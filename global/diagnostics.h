// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "atomic.h"

#include <charconv>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DiagnosticEventRecord{
    const char* event = nullptr;
    const char* category = nullptr;
    const char* expression = nullptr;
    const char* message = nullptr;
    const char* file = nullptr;
    u64 instructionPointer = 0u;
    u32 line = 0u;
};

using DiagnosticEventCallback = void (*)(const DiagnosticEventRecord& record)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticEventCategory{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr const char* s_Assert = "assert";
inline constexpr const char* s_FatalAssert = "fatal_assert";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticEventName{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr const char* s_Assert = "assert";
inline constexpr const char* s_Error = "error";
inline constexpr const char* s_FatalError = "fatal_error";
inline constexpr const char* s_ManualDump = "manual_dump";
inline constexpr const char* s_Crash = "crash";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Atomic<DiagnosticEventCallback> g_EventCallback{ nullptr };
inline AtomicFlag g_EventActive;
inline constexpr usize s_MaxEventTextBytes = 2048u;
inline constexpr usize s_NumberTextBufferBytes = 128u;
inline constexpr const char* s_NullText = "(null)";
inline constexpr const char* s_UnprintableText = "<unprintable>";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void CopyEventText(char (&outText)[s_MaxEventTextBytes], const std::string_view text)noexcept{
    const usize copySize = text.size() >= s_MaxEventTextBytes
        ? s_MaxEventTextBytes - 1u
        : text.size()
    ;
    for(usize i = 0u; i < copySize; ++i)
        outText[i] = text[i];
    outText[copySize] = 0;
}

inline void CopyEventText(char (&outText)[s_MaxEventTextBytes], const std::wstring_view text)noexcept{
    usize writeCursor = 0u;
    for(const wchar_t ch : text){
        if(writeCursor + 1u >= s_MaxEventTextBytes)
            break;
        outText[writeCursor++] = ch >= 0 && ch <= 0x7F
            ? static_cast<char>(ch)
            : '?'
        ;
    }
    outText[writeCursor] = 0;
}

inline void AppendEventChar(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const char ch)noexcept{
    if(outCursor + 1u >= s_MaxEventTextBytes)
        return;

    outText[outCursor++] = ch;
    outText[outCursor] = 0;
}

inline void AppendEventText(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const std::string_view text)noexcept{
    for(const char ch : text)
        AppendEventChar(outText, outCursor, ch);
}

inline void AppendEventText(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const std::wstring_view text)noexcept{
    for(const wchar_t ch : text){
        AppendEventChar(
            outText,
            outCursor,
            ch >= 0 && ch <= 0x7F
                ? static_cast<char>(ch)
                : '?'
        );
    }
}

template<typename CharT>
inline void AppendFormatLiteral(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const std::basic_string_view<CharT> text)noexcept{
    for(usize i = 0u; i < text.size(); ++i){
        const CharT ch = text[i];
        if(ch == static_cast<CharT>('{') && i + 1u < text.size() && text[i + 1u] == static_cast<CharT>('{')){
            AppendEventChar(outText, outCursor, '{');
            ++i;
            continue;
        }
        if(ch == static_cast<CharT>('}') && i + 1u < text.size() && text[i + 1u] == static_cast<CharT>('}')){
            AppendEventChar(outText, outCursor, '}');
            ++i;
            continue;
        }

        AppendEventChar(
            outText,
            outCursor,
            ch >= 0 && ch <= 0x7F
                ? static_cast<char>(ch)
                : '?'
        );
    }
}

template<typename T>
inline void AppendEventNumber(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const T value)noexcept{
    char buffer[s_NumberTextBufferBytes] = {};
    if constexpr(std::is_floating_point_v<T>){
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if(result.ec == std::errc{})
            AppendEventText(outText, outCursor, std::string_view(buffer, static_cast<usize>(result.ptr - buffer)));
    }
    else{
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if(result.ec == std::errc{})
            AppendEventText(outText, outCursor, std::string_view(buffer, static_cast<usize>(result.ptr - buffer)));
    }
}

inline void AppendEventPointer(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const void* const value)noexcept{
    if(!value){
        AppendEventText(outText, outCursor, std::string_view(s_NullText));
        return;
    }

    char buffer[s_NumberTextBufferBytes] = {};
    const usize address = reinterpret_cast<usize>(value);
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), address, 16);
    if(result.ec != std::errc{})
        return;

    AppendEventText(outText, outCursor, std::string_view("0x"));
    AppendEventText(outText, outCursor, std::string_view(buffer, static_cast<usize>(result.ptr - buffer)));
}

template<typename T>
inline void AppendEventArgument(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const T& value)noexcept{
    using RawT = std::remove_cvref_t<T>;
    if constexpr(std::is_same_v<RawT, bool>){
        AppendEventText(outText, outCursor, value ? std::string_view("true") : std::string_view("false"));
    }
    else if constexpr(std::is_same_v<RawT, char>){
        AppendEventChar(outText, outCursor, value);
    }
    else if constexpr(std::is_same_v<RawT, wchar_t>){
        AppendEventChar(outText, outCursor, value >= 0 && value <= 0x7F ? static_cast<char>(value) : '?');
    }
    else if constexpr(std::is_same_v<RawT, std::nullptr_t>){
        AppendEventText(outText, outCursor, std::string_view(s_NullText));
    }
    else if constexpr(std::is_pointer_v<RawT> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>){
        AppendEventText(outText, outCursor, value ? std::string_view(value) : std::string_view(s_NullText));
    }
    else if constexpr(std::is_pointer_v<RawT> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, wchar_t>){
        AppendEventText(outText, outCursor, value ? std::wstring_view(value) : std::wstring_view());
    }
    else if constexpr(std::is_pointer_v<RawT>){
        AppendEventPointer(outText, outCursor, value);
    }
    else if constexpr(std::is_integral_v<RawT>){
        AppendEventNumber(outText, outCursor, value);
    }
    else if constexpr(std::is_floating_point_v<RawT>){
        AppendEventNumber(outText, outCursor, value);
    }
    else if constexpr(std::is_enum_v<RawT>){
        AppendEventNumber(outText, outCursor, static_cast<std::underlying_type_t<RawT>>(value));
    }
    else if constexpr(requires{ std::string_view(value); }){
        AppendEventText(outText, outCursor, std::string_view(value));
    }
    else if constexpr(requires{ std::wstring_view(value); }){
        AppendEventText(outText, outCursor, std::wstring_view(value));
    }
    else if constexpr(requires{ value.c_str(); }){
        AppendEventArgument(outText, outCursor, value.c_str());
    }
    else if constexpr(requires{ value.value; }){
        AppendEventArgument(outText, outCursor, value.value);
    }
    else{
        AppendEventText(outText, outCursor, std::string_view(s_UnprintableText));
    }
}

template<typename CharT>
[[nodiscard]] inline usize FindReplacementEnd(const std::basic_string_view<CharT> text, const usize begin)noexcept{
    for(usize i = begin; i < text.size(); ++i){
        if(text[i] == static_cast<CharT>('}'))
            return i;
    }

    return text.size();
}

template<typename CharT>
inline void FormatEventTextArgs(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const std::basic_string_view<CharT> fmt)noexcept{
    AppendFormatLiteral(outText, outCursor, fmt);
}

template<typename CharT, typename Arg, typename... Args>
inline void FormatEventTextArgs(
    char (&outText)[s_MaxEventTextBytes],
    usize& outCursor,
    const std::basic_string_view<CharT> fmt,
    Arg&& arg,
    Args&&... args
)noexcept{
    for(usize i = 0u; i < fmt.size(); ++i){
        const CharT ch = fmt[i];
        if(ch == static_cast<CharT>('{') && i + 1u < fmt.size() && fmt[i + 1u] == static_cast<CharT>('{')){
            AppendEventChar(outText, outCursor, '{');
            ++i;
            continue;
        }
        if(ch == static_cast<CharT>('}') && i + 1u < fmt.size() && fmt[i + 1u] == static_cast<CharT>('}')){
            AppendEventChar(outText, outCursor, '}');
            ++i;
            continue;
        }
        if(ch == static_cast<CharT>('{')){
            AppendEventArgument(outText, outCursor, Forward<Arg>(arg));
            const usize replacementEnd = FindReplacementEnd(fmt, i + 1u);
            if(replacementEnd >= fmt.size())
                return;

            FormatEventTextArgs(outText, outCursor, fmt.substr(replacementEnd + 1u), Forward<Args>(args)...);
            return;
        }

        AppendEventChar(
            outText,
            outCursor,
            ch >= 0 && ch <= 0x7F
                ? static_cast<char>(ch)
                : '?'
        );
    }
}

template<typename CharT, typename... Args>
inline void FormatEventText(char (&outText)[s_MaxEventTextBytes], const std::basic_string_view<CharT> fmt, Args&&... args)noexcept{
    usize outCursor = 0u;
    outText[0] = 0;
    FormatEventTextArgs(outText, outCursor, fmt, Forward<Args>(args)...);
}

[[nodiscard]] inline bool TextEquals(const char* lhs, const char* rhs)noexcept{
    if(!lhs || !rhs)
        return lhs == rhs;

    while(*lhs != 0 && *rhs != 0){
        if(*lhs != *rhs)
            return false;
        ++lhs;
        ++rhs;
    }

    return *lhs == *rhs;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline const char* DiagnosticEventNameFromCategory(const char* const category)noexcept{
    if(DiagnosticDetail::TextEquals(category, DiagnosticEventCategory::s_Assert) || DiagnosticDetail::TextEquals(category, DiagnosticEventCategory::s_FatalAssert))
        return DiagnosticEventName::s_Assert;

    return nullptr;
}

[[nodiscard]] inline const char* DiagnosticEventNameFromRecord(const DiagnosticEventRecord& record)noexcept{
    if(record.event && record.event[0] != 0)
        return record.event;

    return DiagnosticEventNameFromCategory(record.category);
}

struct DiagnosticEventText{
    char value[DiagnosticDetail::s_MaxEventTextBytes] = {};

    [[nodiscard]] const char* c_str()const noexcept{ return value; }
};

inline DiagnosticEventText MakeDiagnosticEventText(const char* const text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, text ? std::string_view(text) : std::string_view());
    return output;
}

inline DiagnosticEventText MakeDiagnosticEventText(const wchar_t* const text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, text ? std::wstring_view(text) : std::wstring_view());
    return output;
}

inline DiagnosticEventText MakeDiagnosticEventText(const std::string_view text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, text);
    return output;
}

inline DiagnosticEventText MakeDiagnosticEventText(const std::wstring_view text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, text);
    return output;
}

template<typename Traits, typename Allocator>
inline DiagnosticEventText MakeDiagnosticEventText(const std::basic_string<char, Traits, Allocator>& text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, std::string_view(text.data(), text.size()));
    return output;
}

template<typename Traits, typename Allocator>
inline DiagnosticEventText MakeDiagnosticEventText(const std::basic_string<wchar_t, Traits, Allocator>& text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, std::wstring_view(text.data(), text.size()));
    return output;
}

template<typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(std::format_string<Args...> fmt, Args&&... args)noexcept{
    DiagnosticEventText output;
    const auto text = fmt.get();
    DiagnosticDetail::FormatEventText(output.value, std::string_view(text.data(), text.size()), Forward<Args>(args)...);
    return output;
}

template<typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(std::wformat_string<Args...> fmt, Args&&... args)noexcept{
    DiagnosticEventText output;
    const auto text = fmt.get();
    DiagnosticDetail::FormatEventText(output.value, std::wstring_view(text.data(), text.size()), Forward<Args>(args)...);
    return output;
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const char* const fmt, Arg&& arg, Args&&... args)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::FormatEventText(output.value, fmt ? std::string_view(fmt) : std::string_view(), Forward<Arg>(arg), Forward<Args>(args)...);
    return output;
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const wchar_t* const fmt, Arg&& arg, Args&&... args)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::FormatEventText(output.value, fmt ? std::wstring_view(fmt) : std::wstring_view(), Forward<Arg>(arg), Forward<Args>(args)...);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SetDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    DiagnosticDetail::g_EventCallback.store(callback, MemoryOrder::release);
}

inline void ClearDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    DiagnosticEventCallback expected = callback;
    static_cast<void>(DiagnosticDetail::g_EventCallback.compare_exchange_strong(expected, nullptr, MemoryOrder::acq_rel));
}

NWB_NOINLINE inline void CaptureDiagnosticEvent(const DiagnosticEventRecord& record)noexcept{
    const DiagnosticEventCallback callback = DiagnosticDetail::g_EventCallback.load(MemoryOrder::acquire);
    if(!callback)
        return;
    if(DiagnosticDetail::g_EventActive.test_and_set(MemoryOrder::acquire))
        return;

    DiagnosticEventRecord normalizedRecord = record;
    if(!normalizedRecord.event)
        normalizedRecord.event = "";
    if(!normalizedRecord.category)
        normalizedRecord.category = "";
    if(!normalizedRecord.expression)
        normalizedRecord.expression = "";
    if(!normalizedRecord.message)
        normalizedRecord.message = "";
    if(!normalizedRecord.file)
        normalizedRecord.file = "";
#if __has_builtin(__builtin_return_address) || defined(__GNUC__)
    if(normalizedRecord.instructionPointer == 0u)
        normalizedRecord.instructionPointer = static_cast<u64>(reinterpret_cast<usize>(__builtin_return_address(0)));
#endif

    callback(normalizedRecord);

    DiagnosticDetail::g_EventActive.clear(MemoryOrder::release);
}

inline void CaptureDiagnosticEvent(const char* category, const char* message, const char* file = nullptr, const u32 line = 0u)noexcept{
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .category = category,
        .message = message,
        .file = file,
        .line = line,
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

