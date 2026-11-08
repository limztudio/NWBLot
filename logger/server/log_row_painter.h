// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "clipboard_text_internal.h"

#include <logger/server/server_values.adhd.h>


#if defined(NWB_PLATFORM_WINDOWS)


NWB_LOG_BEGIN


namespace FrameDetail{

class LogRowPainter final{
public:
    static constexpr int s_FontHeight = ::NWB::Log::s_LogServerValueFrameWinLogFontHeight;
    static void measure(HWND hwnd, HWND listHwnd, MEASUREITEMSTRUCT& item, const MessageItem& message);
    static void draw(DRAWITEMSTRUCT& item, const MessageItem& message);
};

};


NWB_LOG_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

