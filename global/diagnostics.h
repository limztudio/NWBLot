// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "atomic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DiagnosticCrashRecord{
    const char* category = nullptr;
    const char* message = nullptr;
    const char* file = nullptr;
    u32 line = 0u;
};

using DiagnosticCrashCaptureCallback = void (*)(const DiagnosticCrashRecord& record)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DiagnosticDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Atomic<DiagnosticCrashCaptureCallback> g_CrashCaptureCallback{ nullptr };
inline AtomicFlag g_CrashCaptureActive;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SetDiagnosticCrashCaptureCallback(const DiagnosticCrashCaptureCallback callback)noexcept{
    DiagnosticDetail::g_CrashCaptureCallback.store(callback, MemoryOrder::release);
}

inline void ClearDiagnosticCrashCaptureCallback(const DiagnosticCrashCaptureCallback callback)noexcept{
    DiagnosticCrashCaptureCallback expected = callback;
    static_cast<void>(DiagnosticDetail::g_CrashCaptureCallback.compare_exchange_strong(expected, nullptr, MemoryOrder::acq_rel));
}

inline void CaptureDiagnosticCrash(const DiagnosticCrashRecord& record)noexcept{
    const DiagnosticCrashCaptureCallback callback = DiagnosticDetail::g_CrashCaptureCallback.load(MemoryOrder::acquire);
    if(!callback)
        return;
    if(DiagnosticDetail::g_CrashCaptureActive.test_and_set(MemoryOrder::acquire))
        return;

    DiagnosticCrashRecord normalizedRecord = record;
    if(!normalizedRecord.category)
        normalizedRecord.category = "";
    if(!normalizedRecord.message)
        normalizedRecord.message = "";
    if(!normalizedRecord.file)
        normalizedRecord.file = "";

    callback(normalizedRecord);

    DiagnosticDetail::g_CrashCaptureActive.clear(MemoryOrder::release);
}

inline void CaptureDiagnosticCrash(const char* category, const char* message, const char* file = nullptr, const u32 line = 0u)noexcept{
    CaptureDiagnosticCrash(DiagnosticCrashRecord{
        category,
        message,
        file,
        line,
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

