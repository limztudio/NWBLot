// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "log_row_painter.h"

#include <logger/server/log_row_colors.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/diagnostics.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(static_cast<u8>(Core::Common::LogType::Info) == 0u);
static_assert(static_cast<u8>(Core::Common::LogType::EssentialInfo) == 1u);
static_assert(static_cast<u8>(Core::Common::LogType::Warning) == 2u);
static_assert(static_cast<u8>(Core::Common::LogType::CriticalWarning) == 3u);
static_assert(static_cast<u8>(Core::Common::LogType::Error) == 4u);
static_assert(static_cast<u8>(Core::Common::LogType::Fatal) == 5u);
static_assert(static_cast<u8>(Core::Common::LogType::Assert) == 6u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LogRowPainter::Measure(const HWND hwnd, const HWND listHwnd, MEASUREITEMSTRUCT& mis, const MessageItem& curStr){
    mis.itemHeight = static_cast<UINT>(s_FontHeight);
    RECT rect = {};
    if(!GetClientRect(hwnd, &rect)){
        CaptureDiagnosticEvent(s_LogServerValueFrameWinLayoutDiagnosticCategory, "Log server: failed to query its text measurement rectangle", __FILE__, __LINE__);
        return;
    }
    rect.right -= rect.left;
    rect.left = rect.top = rect.bottom = 0;

    const HDC hdc = GetDC(listHwnd);
    if(!hdc){
        CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to acquire its text measurement device context", __FILE__, __LINE__);
        return;
    }
    ScopeExit releaseDc([&]()noexcept{
        if(!ReleaseDC(listHwnd, hdc))
            CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to release its text measurement device context", __FILE__, __LINE__);
    });

    const HFONT font = reinterpret_cast<HFONT>(SendMessage(listHwnd, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = nullptr;
    if(font){
        oldFont = SelectObject(hdc, font);
        if(!oldFont || oldFont == HGDI_ERROR){
            CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to select its text measurement font", __FILE__, __LINE__);
            return;
        }
    }
    ScopeExit restoreFont([&]()noexcept{
        if(oldFont){
            const HGDIOBJ restored = SelectObject(hdc, oldFont);
            if(!restored || restored == HGDI_ERROR)
                CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to restore its text measurement font", __FILE__, __LINE__);
        }
    });

    if(!curStr.first().empty()){
        if(DrawText(hdc, curStr.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT | DT_CALCRECT) == 0)
            CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to measure its log text", __FILE__, __LINE__);
        else
            mis.itemHeight = static_cast<UINT>(rect.bottom - rect.top);
    }
}

void LogRowPainter::Draw(DRAWITEMSTRUCT& dis, const MessageItem& curData){
    HDC hdc = dis.hDC;
    RECT rect = dis.rcItem;

    const LogRowColorPair colors = ResolveLogRowColorsByType(static_cast<u8>(curData.second()), (dis.itemID & 1) != 0);
    const HBRUSH brush = CreateSolidBrush(static_cast<COLORREF>(colors.background));
    if(!brush){
        CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to create its row background brush", __FILE__, __LINE__);
        return;
    }
    ScopeExit releaseBrush([&]()noexcept{
        if(!DeleteObject(brush))
            CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to delete its row background brush", __FILE__, __LINE__);
    });

    if(!FillRect(hdc, &rect, brush))
        CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to fill its row background", __FILE__, __LINE__);
    const COLORREF oldTextColor = SetTextColor(hdc, static_cast<COLORREF>(colors.text));
    if(oldTextColor == CLR_INVALID){
        CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to set its log text color", __FILE__, __LINE__);
        return;
    }
    ScopeExit restoreTextColor([&]()noexcept{
        if(SetTextColor(hdc, oldTextColor) == CLR_INVALID)
            CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to restore its log text color", __FILE__, __LINE__);
    });

    const int oldBackgroundMode = SetBkMode(hdc, TRANSPARENT);
    if(oldBackgroundMode == 0){
        CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to set its log text background mode", __FILE__, __LINE__);
        return;
    }
    ScopeExit restoreBackgroundMode([&]()noexcept{
        if(!SetBkMode(hdc, oldBackgroundMode))
            CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to restore its log text background mode", __FILE__, __LINE__);
    });

    if(!curData.first().empty() && DrawText(hdc, curData.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT) == 0)
        CaptureDiagnosticEvent(s_LogServerValueFrameWinPaintDiagnosticCategory, "Log server: failed to draw its log text", __FILE__, __LINE__);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

