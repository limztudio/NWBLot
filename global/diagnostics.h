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


// Text is borrowed during synchronous capture; callbacks must copy it before retaining it.
struct DiagnosticEventRecord{
    StringView event = {};
    StringView category = {};
    StringView expression = {};
    StringView message = {};
    StringView file = {};
    u64 instructionPointer = 0u;
    u32 line = 0u;
    bool terminatesProcess = false;
};

using DiagnosticEventCallback = void (*)(const DiagnosticEventRecord& record)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticEventCategory{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_Assert = "assert";
inline constexpr StringView s_FatalAssert = "fatal_assert";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticEventName{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_Assert = "assert";
inline constexpr StringView s_Error = "error";
inline constexpr StringView s_Fatal = "fatal";
inline constexpr StringView s_ManualDump = "manual_dump";
inline constexpr StringView s_Crash = "crash";
inline constexpr StringView s_GpuCrash = "gpu_crash";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Atomic<DiagnosticEventCallback> g_EventCallback{ nullptr };
inline AtomicFlag g_EventActive;
inline constexpr usize s_MaxEventTextBytes = 2048u;
inline constexpr usize s_NumberTextBufferBytes = 128u;
inline constexpr wchar_t s_AsciiCharacterMax = 0x7F;
inline constexpr StringView s_NullText = "(null)";
inline constexpr StringView s_PointerHexPrefix = "0x";
inline constexpr StringView s_TrueText = "true";
inline constexpr StringView s_FalseText = "false";
inline constexpr StringView s_UnprintableText = "<unprintable>";
inline constexpr i32 s_PointerTextRadix = 16;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void CopyEventText(char (&outText)[s_MaxEventTextBytes], const AStringView text)noexcept{
    const usize copySize = text.size() >= s_MaxEventTextBytes
        ? s_MaxEventTextBytes - 1u
        : text.size()
    ;
    for(usize i = 0u; i < copySize; ++i)
        outText[i] = text[i];
    outText[copySize] = 0;
}

inline void CopyEventText(char (&outText)[s_MaxEventTextBytes], const WStringView text)noexcept{
    usize writeCursor = 0u;
    for(const wchar_t ch : text){
        if(writeCursor + 1u >= s_MaxEventTextBytes)
            break;
        outText[writeCursor++] = ch >= 0 && ch <= s_AsciiCharacterMax
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

inline void AppendEventText(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const AStringView text)noexcept{
    for(const char ch : text)
        AppendEventChar(outText, outCursor, ch);
}

inline void AppendEventText(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const WStringView text)noexcept{
    for(const wchar_t ch : text){
        AppendEventChar(
            outText,
            outCursor,
            ch >= 0 && ch <= s_AsciiCharacterMax
                ? static_cast<char>(ch)
                : '?'
        );
    }
}

template<typename CharT>
inline void AppendFormatLiteral(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const BasicStringView<CharT> text)noexcept{
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
            ch >= 0 && ch <= s_AsciiCharacterMax
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
            AppendEventText(outText, outCursor, AStringView(buffer, static_cast<usize>(result.ptr - buffer)));
    }
    else{
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if(result.ec == std::errc{})
            AppendEventText(outText, outCursor, AStringView(buffer, static_cast<usize>(result.ptr - buffer)));
    }
}

inline void AppendEventPointer(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const void* const value)noexcept{
    if(!value){
        AppendEventText(outText, outCursor, AStringView(s_NullText));
        return;
    }

    char buffer[s_NumberTextBufferBytes] = {};
    const usize address = reinterpret_cast<usize>(value);
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), address, s_PointerTextRadix);
    if(result.ec != std::errc{})
        return;

    AppendEventText(outText, outCursor, AStringView(s_PointerHexPrefix));
    AppendEventText(outText, outCursor, AStringView(buffer, static_cast<usize>(result.ptr - buffer)));
}

template<typename T>
inline void AppendEventArgument(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const T& value)noexcept{
    using RawT = std::remove_cvref_t<T>;
    if constexpr(std::is_same_v<RawT, bool>){
        AppendEventText(outText, outCursor, value ? AStringView(s_TrueText) : AStringView(s_FalseText));
    }
    else if constexpr(std::is_same_v<RawT, char>){
        AppendEventChar(outText, outCursor, value);
    }
    else if constexpr(std::is_same_v<RawT, wchar_t>){
        AppendEventChar(outText, outCursor, value >= 0 && value <= s_AsciiCharacterMax ? static_cast<char>(value) : '?');
    }
    else if constexpr(std::is_same_v<RawT, std::nullptr_t>){
        AppendEventText(outText, outCursor, AStringView(s_NullText));
    }
    else if constexpr(std::is_pointer_v<RawT> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, char>){
        AppendEventText(outText, outCursor, value ? AStringView(value) : AStringView(s_NullText));
    }
    else if constexpr(std::is_pointer_v<RawT> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<RawT>>, wchar_t>){
        AppendEventText(outText, outCursor, value ? WStringView(value) : WStringView());
    }
    else if constexpr(std::is_pointer_v<RawT>){
        AppendEventPointer(outText, outCursor, value);
    }
    else if constexpr(std::is_integral_v<RawT> || std::is_floating_point_v<RawT>){
        AppendEventNumber(outText, outCursor, value);
    }
    else if constexpr(std::is_enum_v<RawT>){
        AppendEventNumber(outText, outCursor, static_cast<std::underlying_type_t<RawT>>(value));
    }
    else if constexpr(requires{ AStringView(value); }){
        AppendEventText(outText, outCursor, AStringView(value));
    }
    else if constexpr(requires{ WStringView(value); }){
        AppendEventText(outText, outCursor, WStringView(value));
    }
    else if constexpr(requires{ AStringView(value.resolvedText()); }){
        AppendEventText(outText, outCursor, AStringView(value.resolvedText()));
    }
    else if constexpr(requires{ WStringView(value.resolvedText()); }){
        AppendEventText(outText, outCursor, WStringView(value.resolvedText()));
    }
    else if constexpr(requires{ AStringView(value.view()); }){
        AppendEventText(outText, outCursor, AStringView(value.view()));
    }
    else if constexpr(requires{ WStringView(value.view()); }){
        AppendEventText(outText, outCursor, WStringView(value.view()));
    }
    else if constexpr(requires{ value.c_str(); }){
        AppendEventArgument(outText, outCursor, value.c_str());
    }
    else if constexpr(requires{ value.value; }){
        AppendEventArgument(outText, outCursor, value.value);
    }
    else{
        AppendEventText(outText, outCursor, AStringView(s_UnprintableText));
    }
}

template<typename CharT>
[[nodiscard]] inline usize FindReplacementEnd(const BasicStringView<CharT> text, const usize begin)noexcept{
    for(usize i = begin; i < text.size(); ++i){
        if(text[i] == static_cast<CharT>('}'))
            return i;
    }

    return text.size();
}

template<typename CharT>
inline void FormatEventTextArgs(char (&outText)[s_MaxEventTextBytes], usize& outCursor, const BasicStringView<CharT> fmt)noexcept{
    AppendFormatLiteral(outText, outCursor, fmt);
}

template<typename CharT, typename Arg, typename... Args>
inline void FormatEventTextArgs(
    char (&outText)[s_MaxEventTextBytes],
    usize& outCursor,
    const BasicStringView<CharT> fmt,
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
            ch >= 0 && ch <= s_AsciiCharacterMax
                ? static_cast<char>(ch)
                : '?'
        );
    }
}

template<typename CharT, typename... Args>
inline void FormatEventText(char (&outText)[s_MaxEventTextBytes], const BasicStringView<CharT> fmt, Args&&... args)noexcept{
    usize outCursor = 0u;
    outText[0] = 0;
    FormatEventTextArgs(outText, outCursor, fmt, Forward<Args>(args)...);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline StringView DiagnosticEventNameFromCategory(const StringView category)noexcept{
    if(category == DiagnosticEventCategory::s_Assert || category == DiagnosticEventCategory::s_FatalAssert)
        return DiagnosticEventName::s_Assert;

    return {};
}

[[nodiscard]] inline StringView DiagnosticEventNameFromRecord(const DiagnosticEventRecord& record)noexcept{
    if(!record.event.empty())
        return record.event;

    return DiagnosticEventNameFromCategory(record.category);
}

struct DiagnosticEventText{
    char value[DiagnosticDetail::s_MaxEventTextBytes] = {};

    [[nodiscard]] AStringView view()const noexcept{
        const AStringView text(value, sizeof(value));
        return text.substr(0u, text.find('\0'));
    }

    [[nodiscard]] const char* c_str()const noexcept{ return value; }
};

inline DiagnosticEventText MakeDiagnosticEventText(const char* const text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, text ? AStringView(text) : AStringView());
    return output;
}

inline DiagnosticEventText MakeDiagnosticEventText(const wchar_t* const text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, text ? WStringView(text) : WStringView());
    return output;
}

inline DiagnosticEventText MakeDiagnosticEventText(const AStringView text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, text);
    return output;
}

inline DiagnosticEventText MakeDiagnosticEventText(const WStringView text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, text);
    return output;
}

template<typename Traits, typename Allocator>
inline DiagnosticEventText MakeDiagnosticEventText(const std::basic_string<char, Traits, Allocator>& text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, AStringView(text.data(), text.size()));
    return output;
}

template<typename Traits, typename Allocator>
inline DiagnosticEventText MakeDiagnosticEventText(const std::basic_string<wchar_t, Traits, Allocator>& text)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::CopyEventText(output.value, WStringView(text.data(), text.size()));
    return output;
}

template<typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(std::format_string<Args...> fmt, Args&&... args)noexcept{
    DiagnosticEventText output;
    const auto text = fmt.get();
    DiagnosticDetail::FormatEventText(output.value, AStringView(text.data(), text.size()), Forward<Args>(args)...);
    return output;
}

template<typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(std::wformat_string<Args...> fmt, Args&&... args)noexcept{
    DiagnosticEventText output;
    const auto text = fmt.get();
    DiagnosticDetail::FormatEventText(output.value, WStringView(text.data(), text.size()), Forward<Args>(args)...);
    return output;
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const AStringView fmt, Arg&& arg, Args&&... args)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::FormatEventText(output.value, fmt, Forward<Arg>(arg), Forward<Args>(args)...);
    return output;
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const WStringView fmt, Arg&& arg, Args&&... args)noexcept{
    DiagnosticEventText output;
    DiagnosticDetail::FormatEventText(output.value, fmt, Forward<Arg>(arg), Forward<Args>(args)...);
    return output;
}


template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const char* const fmt, Arg&& arg, Args&&... args)noexcept{
    return MakeDiagnosticEventText(fmt ? AStringView(fmt) : AStringView(), Forward<Arg>(arg), Forward<Args>(args)...);
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const wchar_t* const fmt, Arg&& arg, Args&&... args)noexcept{
    return MakeDiagnosticEventText(fmt ? WStringView(fmt) : WStringView(), Forward<Arg>(arg), Forward<Args>(args)...);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SetDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    DiagnosticDetail::g_EventCallback.store(callback, MemoryOrder::release);
}

inline void ClearDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    DiagnosticEventCallback expected = callback;
    const bool clearedCallback = DiagnosticDetail::g_EventCallback.compare_exchange_strong(expected, nullptr, MemoryOrder::acq_rel);
    if(clearedCallback)
        return;
}

GLB_NOINLINE inline void CaptureDiagnosticEvent(const DiagnosticEventRecord& record)noexcept{
    if(DiagnosticDetail::g_EventActive.test_and_set(MemoryOrder::acquire))
        return;

    const DiagnosticEventCallback callback = DiagnosticDetail::g_EventCallback.load(MemoryOrder::acquire);
    if(!callback){
        DiagnosticDetail::g_EventActive.clear(MemoryOrder::release);
        DiagnosticDetail::g_EventActive.notify_all();
        return;
    }

    DiagnosticEventRecord normalizedRecord = record;
#if __has_builtin(__builtin_return_address) || defined(__GNUC__)
    if(normalizedRecord.instructionPointer == 0u)
        normalizedRecord.instructionPointer = static_cast<u64>(reinterpret_cast<usize>(__builtin_return_address(0)));
#endif

    callback(normalizedRecord);

    DiagnosticDetail::g_EventActive.clear(MemoryOrder::release);
    DiagnosticDetail::g_EventActive.notify_all();
}

inline void CaptureDiagnosticEvent(const StringView category, const StringView message, const StringView file = {}, const u32 line = 0u)noexcept{
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .category = category,
        .message = message,
        .file = file,
        .line = line,
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

