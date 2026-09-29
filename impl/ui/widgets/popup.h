// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PopupPlacementSide{
    enum Enum : u8{ Below, Above, Right, Left, Center };
};

struct PopupOptions{
    Rect anchor;
    Point size = { 220.0f, 160.0f };
    PopupPlacementSide::Enum side = PopupPlacementSide::Below;
    f32 gap = 4.0f;
    bool modal = false;
    bool dismissOutside = true;
    bool dismissEscape = true;
    bool autofocus = true;
};

struct PopupPlacement{
    Rect bounds;
    Rect viewport;
    PopupPlacementSide::Enum side = PopupPlacementSide::Below;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The host owns each state; declarations lend it only through their matching endPopup.
class PopupState final : NoCopy{
    friend class Builder;


public:
    PopupState();


public:
    PopupState(PopupState&&) = delete;
    PopupState& operator=(PopupState&&) = delete;


public:
    void open();
    void close();
    [[nodiscard]] bool isOpen()const{ return m_open; }
    [[nodiscard]] u64 instanceGeneration()const{ return m_instanceGeneration; }
    [[nodiscard]] u64 openGeneration()const{ return m_openGeneration; }
    [[nodiscard]] const PopupPlacement& placement()const{ return m_placement; }


private:
    const u64 m_instanceGeneration;
    u64 m_openGeneration = 0u;
    PopupPlacement m_placement;
    bool m_open = false;
};

class PopupLayout final{
public:
    // Anchored placement flips on its requested axis; oversized content shrinks to the logical viewport.
    [[nodiscard]] static bool Place(const PopupOptions& options, const DisplayMetrics& display, PopupPlacement& placement);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

