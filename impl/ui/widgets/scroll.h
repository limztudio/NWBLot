// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ScrollPlacement{
    Rect bounds;
    Rect viewport;
    Rect contentClip;
    Rect track;
    Rect thumb;
    f64 contentHeight = 0.0;
    f64 maxOffset = 0.0;
    f64 offset = 0.0;
    u64 firstRow = 0u;
    u64 endRow = 0u;
    u64 rowCount = 0u;
    bool scrollbarVisible = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hosts own scroll state; its offset and identity contain no retained application or OS references.
class ScrollState final : NoCopy{
public:
    ScrollState();


public:
    ScrollState(ScrollState&&) = delete;
    ScrollState& operator=(ScrollState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const{ return m_instanceGeneration; }
    [[nodiscard]] f64 offset()const{ return m_offset; }
    [[nodiscard]] bool setOffset(f64 offset);
    [[nodiscard]] bool ensureVisible(f64 start, f64 end, f64 viewportHeight);
    [[nodiscard]] bool scrollBy(f64 delta, f64 contentHeight, f64 viewportHeight);
    [[nodiscard]] bool clamp(f64 contentHeight, f64 viewportHeight);


private:
    const u64 m_instanceGeneration;
    f64 m_offset = 0.0;
};

class ScrollLayout final{
public:
    // Extents and offsets are logical doubles; only the clipped visible row interval reaches float paint geometry.
    // Inputs that cannot represent the final viewport within a double content extent fail without replacing placement.
    [[nodiscard]] static bool Calculate(
        const Rect& bounds,
        const Rect& inheritedClip,
        const Insets& padding,
        f32 scrollbarWidth,
        f32 minimumThumb,
        u64 rowCount,
        f32 rowHeight,
        f64 offset,
        ScrollPlacement& placement
    );
    [[nodiscard]] static bool RowBounds(u64 index, const ScrollPlacement& placement, f32 rowHeight, Rect& rectangle);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

