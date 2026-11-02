// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "atomic.h"
#include "error_code.h"

#include <charconv>
#include <format>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... T>
using AFormatString = std::format_string<T...>;
template<typename... T>
using WFormatString = std::wformat_string<T...>;


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
    b8 terminatesProcess = false;
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
inline constexpr StringView s_Warning = "warning";
inline constexpr StringView s_Error = "error";
inline constexpr StringView s_Fatal = "fatal";
inline constexpr StringView s_ManualDump = "manual_dump";
inline constexpr StringView s_Crash = "crash";
inline constexpr StringView s_GpuCrash = "gpu_crash";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MaxEventTextBytes = 2048u;
inline constexpr StringView s_TrueText = "true";
inline constexpr StringView s_FalseText = "false";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Atomic<DiagnosticEventCallback> g_EventCallback{ nullptr };
inline AtomicFlag g_EventActive;
inline constexpr usize s_NumberTextBufferBytes = 128u;
inline constexpr wchar s_AsciiCharacterMax = 0x7F;
inline constexpr StringView s_NullText = "(null)";
inline constexpr StringView s_PointerHexPrefix = "0x";
inline constexpr StringView s_UnprintableText = "<unprintable>";
inline constexpr i32 s_PointerTextRadix = 16;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] constexpr char ToEventAsciiChar(const CharT ch)noexcept{
    if constexpr(IsSame_V<CharT, char>)
        return ch;
    else
        return ch >= 0 && ch <= s_AsciiCharacterMax ? static_cast<char>(ch) : '?';
}

inline void AppendEventChar(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const char ch)noexcept{
    if(outCursor + 1u >= s_MaxEventTextBytes)
        return;

    outText[outCursor++] = ch;
    outText[outCursor] = 0;
}

template<typename CharT>
inline void AppendEventText(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const BasicStringView<CharT> text)noexcept{
    for(const CharT ch : text)
        AppendEventChar(outText, outCursor, ToEventAsciiChar(ch));
}

template<typename CharT>
[[nodiscard]] inline b8 TryAppendEscapedBrace(const BasicStringView<CharT> text, usize& inoutIndex, Array<char, s_MaxEventTextBytes>& outText, usize& outCursor)noexcept(IsArithmetic_V<CharT>){
    const CharT ch = text[inoutIndex];
    if(ch == static_cast<CharT>('{') && inoutIndex + 1u < text.size() && text[inoutIndex + 1u] == static_cast<CharT>('{')){
        AppendEventChar(outText, outCursor, '{');
        ++inoutIndex;
        return true;
    }
    if(ch == static_cast<CharT>('}') && inoutIndex + 1u < text.size() && text[inoutIndex + 1u] == static_cast<CharT>('}')){
        AppendEventChar(outText, outCursor, '}');
        ++inoutIndex;
        return true;
    }
    return false;
}

template<typename CharT>
inline void AppendFormatLiteral(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(usize i = 0u; i < text.size(); ++i){
        if(TryAppendEscapedBrace(text, i, outText, outCursor))
            continue;
        AppendEventChar(outText, outCursor, ToEventAsciiChar(text[i]));
    }
}

template<typename T>
inline void AppendEventNumber(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const T value)noexcept(IsArithmetic_V<T>){
    Array<char, s_NumberTextBufferBytes> buffer = {};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if(result.ec == ErrorCondition{})
        AppendEventText(outText, outCursor, AStringView(buffer.data(), static_cast<usize>(result.ptr - buffer.data())));
}

inline void AppendEventPointer(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const void* const value)noexcept{
    if(!value){
        AppendEventText(outText, outCursor, AStringView(s_NullText));
        return;
    }

    Array<char, s_NumberTextBufferBytes> buffer = {};
    const usize address = reinterpret_cast<usize>(value);
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), address, s_PointerTextRadix);
    if(result.ec != ErrorCondition{})
        return;

    AppendEventText(outText, outCursor, AStringView(s_PointerHexPrefix));
    AppendEventText(outText, outCursor, AStringView(buffer.data(), static_cast<usize>(result.ptr - buffer.data())));
}

template<typename T>
inline void AppendEventArgument(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const T& value)noexcept{
    using RawT = RemoveCVRef_T<T>;
    if constexpr(IsSame_V<RawT, b8>){
        AppendEventText(outText, outCursor, value ? AStringView(s_TrueText) : AStringView(s_FalseText));
    }
    else if constexpr(IsSame_V<RawT, char>){
        AppendEventChar(outText, outCursor, value);
    }
    else if constexpr(IsSame_V<RawT, wchar>){
        AppendEventChar(outText, outCursor, ToEventAsciiChar(value));
    }
    else if constexpr(IsSame_V<RawT, Nullptr>){
        AppendEventText(outText, outCursor, AStringView(s_NullText));
    }
    else if constexpr(IsPointer_V<RawT> && IsSame_V<RemoveCV_T<RemovePointer_T<RawT>>, char>){
        AppendEventText(outText, outCursor, value ? AStringView(value) : AStringView(s_NullText));
    }
    else if constexpr(IsPointer_V<RawT> && IsSame_V<RemoveCV_T<RemovePointer_T<RawT>>, wchar>){
        AppendEventText(outText, outCursor, value ? WStringView(value) : WStringView());
    }
    else if constexpr(IsPointer_V<RawT>){
        AppendEventPointer(outText, outCursor, value);
    }
    else if constexpr(IsIntegral_V<RawT> || IsFloatingPoint_V<RawT>){
        AppendEventNumber(outText, outCursor, value);
    }
    else if constexpr(IsEnum_V<RawT>){
        AppendEventNumber(outText, outCursor, static_cast<UnderlyingType_T<RawT>>(value));
    }
    else if constexpr(requires{ { AStringView(value) }noexcept; }){
        AppendEventText(outText, outCursor, AStringView(value));
    }
    else if constexpr(requires{ { WStringView(value) }noexcept; }){
        AppendEventText(outText, outCursor, WStringView(value));
    }
    else if constexpr(requires{ { AStringView(value.logText()) }noexcept; }){
        AppendEventText(outText, outCursor, AStringView(value.logText()));
    }
    else if constexpr(requires{ { AStringView(value.resolvedText()) }noexcept; }){
        AppendEventText(outText, outCursor, AStringView(value.resolvedText()));
    }
    else if constexpr(requires{ { WStringView(value.resolvedText()) }noexcept; }){
        AppendEventText(outText, outCursor, WStringView(value.resolvedText()));
    }
    else if constexpr(requires{ { AStringView(value.view()) }noexcept; }){
        AppendEventText(outText, outCursor, AStringView(value.view()));
    }
    else if constexpr(requires{ { WStringView(value.view()) }noexcept; }){
        AppendEventText(outText, outCursor, WStringView(value.view()));
    }
    else if constexpr(requires{ { AppendEventArgument(outText, outCursor, value.c_str()) }noexcept; }){
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
[[nodiscard]] inline usize FindReplacementEnd(const BasicStringView<CharT> text, const usize begin)noexcept(IsArithmetic_V<CharT>){
    for(usize i = begin; i < text.size(); ++i){
        if(text[i] == static_cast<CharT>('}'))
            return i;
    }

    return text.size();
}

template<typename CharT>
inline void FormatEventTextArgs(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const BasicStringView<CharT> fmt)noexcept(IsArithmetic_V<CharT>){
    AppendFormatLiteral(outText, outCursor, fmt);
}

template<typename CharT, typename Arg, typename... Args>
inline void FormatEventTextArgs(Array<char, s_MaxEventTextBytes>& outText, usize& outCursor, const BasicStringView<CharT> fmt, Arg&& arg, Args&&... args)noexcept(IsArithmetic_V<CharT>){
    for(usize i = 0u; i < fmt.size(); ++i){
        if(TryAppendEscapedBrace(fmt, i, outText, outCursor))
            continue;
        const CharT ch = fmt[i];
        if(ch == static_cast<CharT>('{')){
            AppendEventArgument(outText, outCursor, Forward<Arg>(arg));
            const usize replacementEnd = FindReplacementEnd(fmt, i + 1u);
            if(replacementEnd >= fmt.size())
                return;

            FormatEventTextArgs(outText, outCursor, fmt.substr(replacementEnd + 1u), Forward<Args>(args)...);
            return;
        }

        AppendEventChar(outText, outCursor, ToEventAsciiChar(ch));
    }
}

template<typename CharT, typename... Args>
inline void FormatEventText(Array<char, s_MaxEventTextBytes>& outText, const BasicStringView<CharT> fmt, Args&&... args)noexcept(IsArithmetic_V<CharT>){
    usize outCursor = 0u;
    outText[0] = 0;
    FormatEventTextArgs(outText, outCursor, fmt, Forward<Args>(args)...);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
inline void CopyEventText(Array<char, s_MaxEventTextBytes>& outText, const BasicStringView<CharT> text)noexcept{
    usize outCursor = 0u;
    outText[0] = 0;
    DiagnosticDetail::AppendEventText(outText, outCursor, text);
}

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
    Array<char, s_MaxEventTextBytes> value = {};


public:
    [[nodiscard]] AStringView view()const noexcept{
        const AStringView text(value.data(), value.size());
        return text.substr(0u, text.find('\0'));
    }

    [[nodiscard]] const char* c_str()const noexcept{ return value.data(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline DiagnosticEventText MakeCopiedEventText(const BasicStringView<CharT> text)noexcept{
    DiagnosticEventText output;
    CopyEventText(output.value, text);
    return output;
}

template<typename CharT, typename... Args>
[[nodiscard]] inline DiagnosticEventText MakeFormattedEventText(const BasicStringView<CharT> fmt, Args&&... args)noexcept{
    DiagnosticEventText output;
    FormatEventText(output.value, fmt, Forward<Args>(args)...);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DiagnosticEventText MakeDiagnosticEventText(const AStringView text)noexcept{
    return DiagnosticDetail::MakeCopiedEventText(text);
}

inline DiagnosticEventText MakeDiagnosticEventText(const WStringView text)noexcept{
    return DiagnosticDetail::MakeCopiedEventText(text);
}

inline DiagnosticEventText MakeDiagnosticEventText(const char* const text)noexcept{
    return MakeDiagnosticEventText(text ? AStringView(text) : AStringView());
}

inline DiagnosticEventText MakeDiagnosticEventText(const wchar* const text)noexcept{
    return MakeDiagnosticEventText(text ? WStringView(text) : WStringView());
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const AStringView fmt, Arg&& arg, Args&&... args)noexcept{
    return DiagnosticDetail::MakeFormattedEventText(fmt, Forward<Arg>(arg), Forward<Args>(args)...);
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const WStringView fmt, Arg&& arg, Args&&... args)noexcept{
    return DiagnosticDetail::MakeFormattedEventText(fmt, Forward<Arg>(arg), Forward<Args>(args)...);
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(AFormatString<Arg, Args...> fmt, Arg&& arg, Args&&... args)noexcept{
    const auto text = fmt.get();
    return DiagnosticDetail::MakeFormattedEventText(AStringView(text.data(), text.size()), Forward<Arg>(arg), Forward<Args>(args)...);
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(WFormatString<Arg, Args...> fmt, Arg&& arg, Args&&... args)noexcept{
    const auto text = fmt.get();
    return DiagnosticDetail::MakeFormattedEventText(WStringView(text.data(), text.size()), Forward<Arg>(arg), Forward<Args>(args)...);
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const char* const fmt, Arg&& arg, Args&&... args)noexcept{
    return MakeDiagnosticEventText(fmt ? AStringView(fmt) : AStringView(), Forward<Arg>(arg), Forward<Args>(args)...);
}

template<typename Arg, typename... Args>
inline DiagnosticEventText MakeDiagnosticEventText(const wchar* const fmt, Arg&& arg, Args&&... args)noexcept{
    return MakeDiagnosticEventText(fmt ? WStringView(fmt) : WStringView(), Forward<Arg>(arg), Forward<Args>(args)...);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SetDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    DiagnosticDetail::g_EventCallback.store(callback, MemoryOrder::release);
}

[[nodiscard]] inline b8 TryClaimDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    DiagnosticEventCallback expected = nullptr;
    return DiagnosticDetail::g_EventCallback.compare_exchange_strong(expected, callback, MemoryOrder::acq_rel);
}

inline void ClearDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    DiagnosticEventCallback expected = callback;
    const b8 clearedCallback = DiagnosticDetail::g_EventCallback.compare_exchange_strong(expected, nullptr, MemoryOrder::acq_rel);
    if(clearedCallback)
        return;
}

[[nodiscard]] inline b8 ReleaseDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    while(DiagnosticDetail::g_EventActive.testAndSet(MemoryOrder::acquire))
        DiagnosticDetail::g_EventActive.wait(true, MemoryOrder::relaxed);

    DiagnosticEventCallback expected = callback;
    const b8 releasedCallback = DiagnosticDetail::g_EventCallback.compare_exchange_strong(expected, nullptr, MemoryOrder::acq_rel);
    DiagnosticDetail::g_EventActive.clear(MemoryOrder::release);
    DiagnosticDetail::g_EventActive.notifyAll();
    return releasedCallback;
}

NWB_NOINLINE inline void CaptureDiagnosticEvent(const DiagnosticEventRecord& record)noexcept{
    if(DiagnosticDetail::g_EventActive.testAndSet(MemoryOrder::acquire))
        return;

    const DiagnosticEventCallback callback = DiagnosticDetail::g_EventCallback.load(MemoryOrder::acquire);
    if(!callback){
        DiagnosticDetail::g_EventActive.clear(MemoryOrder::release);
        DiagnosticDetail::g_EventActive.notifyAll();
        return;
    }

    DiagnosticEventRecord normalizedRecord = record;
    if(normalizedRecord.instructionPointer == 0u)
        normalizedRecord.instructionPointer = static_cast<u64>(reinterpret_cast<usize>(__builtin_return_address(0)));

    callback(normalizedRecord);

    DiagnosticDetail::g_EventActive.clear(MemoryOrder::release);
    DiagnosticDetail::g_EventActive.notifyAll();
}

inline void CaptureDiagnosticEvent(const StringView category, const StringView message, const StringView file = {}, const u32 line = 0u)noexcept{
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .category = category, .message = message, .file = file, .line = line, });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

