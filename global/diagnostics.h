// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "atomic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DiagnosticEventRecord{
    const char* category = nullptr;
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
inline constexpr const char* s_LoggerError = "logger_Error";
inline constexpr const char* s_LoggerFatal = "logger_Fatal";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DIAGNOSTIC_LOGGER_CATEGORY(Type) ::DiagnosticEventCategory::s_Logger ## Type


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Atomic<DiagnosticEventCallback> g_EventCallback{ nullptr };
inline AtomicFlag g_EventActive;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    if(!normalizedRecord.category)
        normalizedRecord.category = "";
    if(!normalizedRecord.message)
        normalizedRecord.message = "";
    if(!normalizedRecord.file)
        normalizedRecord.file = "";

    callback(normalizedRecord);

    DiagnosticDetail::g_EventActive.clear(MemoryOrder::release);
}

inline void CaptureDiagnosticEvent(const char* category, const char* message, const char* file = nullptr, const u32 line = 0u)noexcept{
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        category,
        message,
        file,
        line,
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

