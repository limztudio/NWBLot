// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "atomic.h"

#include <string>
#include <string_view>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DiagnosticEventRecord{
    const char* event = nullptr;
    const char* category = nullptr;
    const char* expression = nullptr;
    const char* message = nullptr;
    const char* file = nullptr;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SetDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    DiagnosticDetail::g_EventCallback.store(callback, MemoryOrder::release);
}

inline void ClearDiagnosticEventCallback(const DiagnosticEventCallback callback)noexcept{
    DiagnosticEventCallback expected = callback;
    static_cast<void>(DiagnosticDetail::g_EventCallback.compare_exchange_strong(expected, nullptr, MemoryOrder::acq_rel));
}

inline void CaptureDiagnosticEvent(const DiagnosticEventRecord& record)noexcept{
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

