// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <logger/server/log_row_painter.h>
#include <global/scope_exit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_log_row_painter_tests{
using namespace NWB;

TEST(LogRowPainter, EmptyErrorRowsUseAlternatingColorsAndRestoreDeviceContext){
    Tests::TestArena<> arena;
    const HDC dc = CreateCompatibleDC(nullptr);
    ASSERT_NE(dc, nullptr);
    ScopeExit releaseDc([&]()noexcept{ DeleteDC(dc); });
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 4;
    info.bmiHeader.biHeight = -4;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    const HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0u);
    ASSERT_NE(bitmap, nullptr);
    ScopeExit releaseBitmap([&]()noexcept{ DeleteObject(bitmap); });
    const HGDIOBJ previous = SelectObject(dc, bitmap);
    ASSERT_TRUE(previous && previous != HGDI_ERROR);
    ScopeExit restoreBitmap([&]()noexcept{ SelectObject(dc, previous); });
    ASSERT_NE(pixels, nullptr);
    ASSERT_NE(SetTextColor(dc, RGB(3, 4, 5)), CLR_INVALID);
    ASSERT_NE(SetBkMode(dc, OPAQUE), 0);
    Log::FrameDetail::MessageItem message(Log::LogString(NWB_TEXT(""), arena.arena), Core::Common::LogType::Error);
    DRAWITEMSTRUCT item{};
    item.hDC = dc;
    item.rcItem = RECT{ 0, 0, 4, 4 };
    Log::FrameDetail::LogRowPainter::draw(item, message);
    EXPECT_EQ(GetPixel(dc, 1, 1), RGB(170, 0, 0));
    item.itemID = 1u;
    Log::FrameDetail::LogRowPainter::draw(item, message);
    EXPECT_EQ(GetPixel(dc, 1, 1), RGB(200, 0, 0));
    EXPECT_EQ(GetTextColor(dc), RGB(3, 4, 5));
    EXPECT_EQ(GetBkMode(dc), OPAQUE);
}


TEST(LogRowPainter, LongTextWrapsWithinHiddenParentClientWidth){
    Tests::TestArena<> arena;
    const HWND parent = CreateWindowEx(0, NWB_TEXT("STATIC"), NWB_TEXT(""), WS_POPUP, 0, 0, 60, 100, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    ASSERT_NE(parent, nullptr);
    ScopeExit destroyParent([&]()noexcept{ DestroyWindow(parent); });
    const HWND child = CreateWindowEx(0, NWB_TEXT("STATIC"), NWB_TEXT(""), WS_CHILD, 0, 0, 60, 100, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    ASSERT_NE(child, nullptr);
    SendMessage(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
    Log::FrameDetail::MessageItem message(Log::LogString(NWB_TEXT("A long message that must wrap across several narrow rows of the hidden logger window."), arena.arena), Core::Common::LogType::Info);
    MEASUREITEMSTRUCT item{};
    Log::FrameDetail::LogRowPainter::measure(parent, child, item, message);
    EXPECT_GT(item.itemHeight, static_cast<UINT>(Log::FrameDetail::LogRowPainter::s_FontHeight));
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
