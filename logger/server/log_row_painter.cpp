// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "log_row_painter.h"

#if defined(NWB_PLATFORM_WINDOWS)

#include <global/diagnostics.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


namespace FrameDetail{
namespace __hidden_log_row_painter{
inline constexpr AStringView s_RowLayoutDiagnosticCategory = "logger_window_layout";
inline constexpr AStringView s_PaintDiagnosticCategory = "logger_window_paint";
inline constexpr COLORREF s_DefaultLogRowTextColor = RGB(0, 0, 0);
inline constexpr COLORREF s_DefaultLogRowBackgroundColor = RGB(230, 230, 230);

struct LogRowColors{
    COLORREF text = s_DefaultLogRowTextColor;
    COLORREF background = s_DefaultLogRowBackgroundColor;
};

inline constexpr LogRowColors s_DefaultEvenLogRowColors{};
inline constexpr LogRowColors s_InfoOddLogRowColors{ RGB(80, 80, 80), RGB(255, 255, 255) };
inline constexpr LogRowColors s_WarningEvenLogRowColors{ RGB(0, 0, 0), RGB(170, 170, 0) };
inline constexpr LogRowColors s_WarningOddLogRowColors{ RGB(60, 60, 60), RGB(200, 200, 0) };
inline constexpr LogRowColors s_CriticalWarningEvenLogRowColors{ RGB(240, 240, 240), RGB(170, 80, 0) };
inline constexpr LogRowColors s_CriticalWarningOddLogRowColors{ RGB(255, 255, 255), RGB(200, 100, 0) };
inline constexpr LogRowColors s_AssertEvenLogRowColors{ RGB(200, 200, 200), RGB(120, 0, 160) };
inline constexpr LogRowColors s_AssertOddLogRowColors{ RGB(255, 255, 255), RGB(145, 20, 190) };
inline constexpr LogRowColors s_ErrorEvenLogRowColors{ RGB(200, 200, 200), RGB(170, 0, 0) };
inline constexpr LogRowColors s_ErrorOddLogRowColors{ RGB(255, 255, 255), RGB(200, 0, 0) };
inline constexpr LogRowColors s_FatalEvenLogRowColors{ RGB(200, 200, 0), RGB(220, 0, 0) };
inline constexpr LogRowColors s_FatalOddLogRowColors{ RGB(255, 255, 0), RGB(250, 0, 0) };


static LogRowColors SelectLogRowColors(const b8 alternate, const LogRowColors& even, const LogRowColors& odd)noexcept{
    return alternate ? odd : even;
}

static LogRowColors ResolveLogRowColors(const Core::Common::LogType::Enum type, const b8 alternate)noexcept{
    switch(type){
    case Core::Common::LogType::EssentialInfo:
    case Core::Common::LogType::Info:
        return SelectLogRowColors(alternate, s_DefaultEvenLogRowColors, s_InfoOddLogRowColors);
    case Core::Common::LogType::Warning:
        return SelectLogRowColors(alternate, s_WarningEvenLogRowColors, s_WarningOddLogRowColors);
    case Core::Common::LogType::CriticalWarning:
        return SelectLogRowColors(alternate, s_CriticalWarningEvenLogRowColors, s_CriticalWarningOddLogRowColors);
    case Core::Common::LogType::Assert:
        return SelectLogRowColors(alternate, s_AssertEvenLogRowColors, s_AssertOddLogRowColors);
    case Core::Common::LogType::Error:
        return SelectLogRowColors(alternate, s_ErrorEvenLogRowColors, s_ErrorOddLogRowColors);
    case Core::Common::LogType::Fatal:
        return SelectLogRowColors(alternate, s_FatalEvenLogRowColors, s_FatalOddLogRowColors);
    default:
        return SelectLogRowColors(alternate, s_DefaultEvenLogRowColors, s_InfoOddLogRowColors);
    }
}

};

void LogRowPainter::measure(const HWND hwnd, const HWND listHwnd, MEASUREITEMSTRUCT& mis, const MessageItem& curStr){
    using namespace __hidden_log_row_painter;
    mis.itemHeight = static_cast<UINT>(s_FontHeight);
    RECT rect = {};
    if(!GetClientRect(hwnd, &rect)){
        CaptureDiagnosticEvent(s_RowLayoutDiagnosticCategory, "Log server: failed to query its text measurement rectangle", __FILE__, __LINE__);
        return;
    }
    rect.right -= rect.left;
    rect.left = rect.top = rect.bottom = 0;

    const HDC hdc = GetDC(listHwnd);
    if(!hdc){
        CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to acquire its text measurement device context", __FILE__, __LINE__);
        return;
    }
    ScopeExit releaseDc([&]()noexcept{
        if(!ReleaseDC(listHwnd, hdc))
            CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to release its text measurement device context", __FILE__, __LINE__);
    });

    const HFONT font = reinterpret_cast<HFONT>(SendMessage(listHwnd, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = nullptr;
    if(font){
        oldFont = SelectObject(hdc, font);
        if(!oldFont || oldFont == HGDI_ERROR){
            CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to select its text measurement font", __FILE__, __LINE__);
            return;
        }
    }
    ScopeExit restoreFont([&]()noexcept{
        if(oldFont){
            const HGDIOBJ restored = SelectObject(hdc, oldFont);
            if(!restored || restored == HGDI_ERROR)
                CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to restore its text measurement font", __FILE__, __LINE__);
        }
    });

    if(!curStr.first().empty()){
        if(DrawText(hdc, curStr.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT | DT_CALCRECT) == 0)
            CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to measure its log text", __FILE__, __LINE__);
        else
            mis.itemHeight = static_cast<UINT>(rect.bottom - rect.top);
    }
}

void LogRowPainter::draw(DRAWITEMSTRUCT& dis, const MessageItem& curData){
    using namespace __hidden_log_row_painter;
    HDC hdc = dis.hDC;
    RECT rect = dis.rcItem;

    const LogRowColors colors = ResolveLogRowColors(curData.second(), (dis.itemID & 1) != 0);
    const HBRUSH brush = CreateSolidBrush(colors.background);
    if(!brush){
        CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to create its row background brush", __FILE__, __LINE__);
        return;
    }
    ScopeExit releaseBrush([&]()noexcept{
        if(!DeleteObject(brush))
            CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to delete its row background brush", __FILE__, __LINE__);
    });

    if(!FillRect(hdc, &rect, brush))
        CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to fill its row background", __FILE__, __LINE__);
    const COLORREF oldTextColor = SetTextColor(hdc, colors.text);
    if(oldTextColor == CLR_INVALID){
        CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to set its log text color", __FILE__, __LINE__);
        return;
    }
    ScopeExit restoreTextColor([&]()noexcept{
        if(SetTextColor(hdc, oldTextColor) == CLR_INVALID)
            CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to restore its log text color", __FILE__, __LINE__);
    });

    const int oldBackgroundMode = SetBkMode(hdc, TRANSPARENT);
    if(oldBackgroundMode == 0){
        CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to set its log text background mode", __FILE__, __LINE__);
        return;
    }
    ScopeExit restoreBackgroundMode([&]()noexcept{
        if(!SetBkMode(hdc, oldBackgroundMode))
            CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to restore its log text background mode", __FILE__, __LINE__);
    });

    if(!curData.first().empty() && DrawText(hdc, curData.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT) == 0)
        CaptureDiagnosticEvent(s_PaintDiagnosticCategory, "Log server: failed to draw its log text", __FILE__, __LINE__);
}

};


NWB_LOG_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
