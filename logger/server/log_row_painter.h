// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "clipboard_text_internal.h"


#if defined(FOUNDATION_PLATFORM_WINDOWS)


NWB_LOG_BEGIN


namespace FrameDetail{

class LogRowPainter final{
public:
    static constexpr int s_FontHeight = 10;
    static void Measure(HWND hwnd, HWND listHwnd, MEASUREITEMSTRUCT& item, const MessageItem& message);
    static void Draw(DRAWITEMSTRUCT& item, const MessageItem& message);
};

};


NWB_LOG_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
