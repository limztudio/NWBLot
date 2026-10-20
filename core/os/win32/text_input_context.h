// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <global/diagnostics.h>
#include <global/expected.h>

#include <windows.h>
#include <imm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Win32TextInputContextGuard final : private NoCopy{
private:
    static constexpr StringView s_WarningEventName = "warning";


public:
    Win32TextInputContextGuard(const HWND window, const HIMC context)noexcept
        : m_window(window)
        , m_context(context)
    {}
    ~Win32TextInputContextGuard()noexcept{
        if(!release()){
            CaptureDiagnosticEvent(DiagnosticEventRecord{
                .event = s_WarningEventName,
                .category = "win32_text_input_context_release",
                .message = "Text input: ImmReleaseContext failed",
            });
        }
    }


public:
    [[nodiscard]] Expected<void> release()noexcept{
        const HIMC context = m_context;
        m_context = nullptr;
        if(context && !ImmReleaseContext(m_window, context))
            return MakeUnexpected(Failure{});
        return {};
    }


private:
    HWND m_window;
    HIMC m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

