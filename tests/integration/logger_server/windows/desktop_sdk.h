// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/basic_string.h>
#include <global/termination.h>
#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::WindowsDesktop{

class DeviceContext final : NoCopy{
public:
    DeviceContext()noexcept : m_dc(CreateCompatibleDC(nullptr)){}
    ~DeviceContext()noexcept{ if(m_dc) DeleteDC(m_dc); }
    [[nodiscard]] b8 valid()const noexcept{ return m_dc != nullptr; }
    [[nodiscard]] HDC native()const noexcept{ return m_dc; }

private:
    HDC m_dc;
};

class DibSection final : NoCopy{
public:
    DibSection(const DeviceContext& dc, const BITMAPINFO& info)noexcept : m_bitmap(CreateDIBSection(dc.native(), &info, DIB_RGB_COLORS, &m_pixels, nullptr, 0u)){}
    ~DibSection()noexcept{ if(m_bitmap) DeleteObject(m_bitmap); }
    [[nodiscard]] b8 valid()const noexcept{ return m_bitmap != nullptr; }
    [[nodiscard]] b8 hasPixels()const noexcept{ return m_pixels != nullptr; }
    [[nodiscard]] HBITMAP native()const noexcept{ return m_bitmap; }

private:
    void* m_pixels = nullptr;
    HBITMAP m_bitmap;
};

class SelectedBitmap final : NoCopy{
public:
    SelectedBitmap(const DeviceContext& dc, const DibSection& bitmap)noexcept : m_dc(dc.native()), m_previous(SelectObject(m_dc, bitmap.native())){}
    ~SelectedBitmap()noexcept{ if(valid()) SelectObject(m_dc, m_previous); }
    [[nodiscard]] b8 valid()const noexcept{ return m_previous && m_previous != HGDI_ERROR; }

private:
    HDC m_dc;
    HGDIOBJ m_previous;
};

class StaticWindow final : NoCopy{
public:
    StaticWindow(const u32 style, const i32 width, const i32 height)noexcept : StaticWindow(nullptr, style, width, height){}
    StaticWindow(const StaticWindow& parent, const u32 style, const i32 width, const i32 height)noexcept : StaticWindow(parent.native(), style, width, height){}
    ~StaticWindow()noexcept{ if(m_hwnd) DestroyWindow(m_hwnd); }
    [[nodiscard]] b8 valid()const noexcept{ return m_hwnd != nullptr; }
    [[nodiscard]] HWND native()const noexcept{ return m_hwnd; }
    void useDefaultGuiFont()const noexcept{ SendMessage(m_hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE); }

private:
    StaticWindow(const HWND parent, const DWORD style, const i32 width, const i32 height)noexcept
        : m_hwnd(CreateWindowEx(0u, FOUNDATION_TEXT("STATIC"), FOUNDATION_TEXT(""), style, 0, 0, width, height, parent, nullptr, GetModuleHandle(nullptr), nullptr)){}
    HWND m_hwnd;
};

class GlobalTextLock final : NoCopy{
public:
    explicit GlobalTextLock(const HGLOBAL memory)noexcept
        : m_memory(memory), m_text(static_cast<const tchar*>(GlobalLock(memory))), m_capacity(GlobalSize(memory) / sizeof(tchar)){}
    ~GlobalTextLock()noexcept{ if(m_text) (void)unlock(); }
    [[nodiscard]] b8 valid()const noexcept{ return m_text != nullptr; }
    [[nodiscard]] TStringView view(const usize count)const noexcept{ return m_text && count <= m_capacity ? TStringView(m_text, count) : TStringView(); }
    [[nodiscard]] u32 character(const usize index)const noexcept{ return m_text && index < m_capacity ? static_cast<u32>(m_text[index]) : Limit<u32>::s_Max; }
    [[nodiscard]] b8 unlock()noexcept{
        if(!m_text)
            return false;
        SetLastError(NO_ERROR);
        const b8 released = GlobalUnlock(m_memory) || GetLastError() == NO_ERROR;
        if(released)
            m_text = nullptr;
        return released;
    }

private:
    HGLOBAL m_memory;
    const tchar* m_text;
    usize m_capacity;
};

class OwnedGlobalMemory final : NoCopy{
public:
    explicit OwnedGlobalMemory(const HGLOBAL memory)noexcept : m_memory(memory){}
    ~OwnedGlobalMemory()noexcept{ if(m_memory && GlobalFree(m_memory)) TerminateInvariant(); }

private:
    HGLOBAL m_memory;
};

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

