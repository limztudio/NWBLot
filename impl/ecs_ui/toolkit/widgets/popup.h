// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"
#include "../input/popup.h"


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


// The host lends state through the outermost popup end; each opening binds to its exact parent lifetime.
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
    void bindParent(const PopupToken& parent);


private:
    const u64 m_instanceGeneration;
    u64 m_openGeneration = 0u;
    PopupPlacement m_placement;
    PopupToken m_parent;
    u64 m_parentOpenGeneration = 0u;
    bool m_parentBound = false;
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

