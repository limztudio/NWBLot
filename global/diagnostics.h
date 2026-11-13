// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "atomic.h"
#include "error_code.h"
#include "diagnostic_event_text_formatter.h"

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





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Atomic<DiagnosticEventCallback> g_EventCallback{ nullptr };
inline AtomicFlag g_EventActive;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
inline void CopyEventText(Array<char, s_MaxEventTextBytes>& outText, const BasicStringView<CharT> text)noexcept{
    usize outCursor = 0u;
    outText[0] = 0;
    DiagnosticEventTextFormatter::AppendEventText(outText, outCursor, text);
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
    DiagnosticEventTextFormatter::FormatEventText(output.value, fmt, Forward<Args>(args)...);
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
    return DiagnosticDetail::MakeFormattedEventText(AStringView(text), Forward<Arg>(arg), Forward<Args>(args)...);
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

FOUNDATION_NOINLINE inline void CaptureDiagnosticEvent(const DiagnosticEventRecord& record)noexcept{
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

