// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidOptions(const PopupOptions& options, const DisplayMetrics& display)noexcept{
    const Rect& anchor = options.anchor;
    return
        IsFinite(anchor.x) && IsFinite(anchor.y) && IsFinite(anchor.width) && IsFinite(anchor.height)
        && anchor.width >= 0.0f && anchor.height >= 0.0f
        && IsFinite(anchor.x + anchor.width) && IsFinite(anchor.y + anchor.height)
        && IsFinite(options.size.x) && options.size.x > 0.0f && IsFinite(options.size.y) && options.size.y > 0.0f
        && options.side <= PopupPlacementSide::Center && IsFinite(options.gap) && options.gap >= 0.0f
        && IsFinite(display.logicalWidth) && display.logicalWidth > 0.0f
        && IsFinite(display.logicalHeight) && display.logicalHeight > 0.0f
        && IsFinite(display.pixelScaleX) && display.pixelScaleX > 0.0f
        && IsFinite(display.pixelScaleY) && display.pixelScaleY > 0.0f
    ;
}

[[nodiscard]] static PopupPlacementSide::Enum Opposite(const PopupPlacementSide::Enum side)noexcept{
    switch(side){
    case PopupPlacementSide::Below: return PopupPlacementSide::Above;
    case PopupPlacementSide::Above: return PopupPlacementSide::Below;
    case PopupPlacementSide::Right: return PopupPlacementSide::Left;
    case PopupPlacementSide::Left: return PopupPlacementSide::Right;
    default: return PopupPlacementSide::Center;
    }
}

[[nodiscard]] static f64 Available(const PopupOptions& options, const DisplayMetrics& display,
    const PopupPlacementSide::Enum side
)noexcept{
    const f64 gap = options.gap;
    switch(side){
    case PopupPlacementSide::Below:
        return Clamp(static_cast<f64>(display.logicalHeight) - options.anchor.y - options.anchor.height - gap,
            0.0, static_cast<f64>(display.logicalHeight));
    case PopupPlacementSide::Above:
        return Clamp(static_cast<f64>(options.anchor.y) - gap, 0.0, static_cast<f64>(display.logicalHeight));
    case PopupPlacementSide::Right:
        return Clamp(static_cast<f64>(display.logicalWidth) - options.anchor.x - options.anchor.width - gap,
            0.0, static_cast<f64>(display.logicalWidth));
    case PopupPlacementSide::Left:
        return Clamp(static_cast<f64>(options.anchor.x) - gap, 0.0, static_cast<f64>(display.logicalWidth));
    default:
        return 0.0;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PopupPlacement> PopupLayout::Place(const PopupOptions& options, const DisplayMetrics& display)noexcept{
    using namespace __hidden_ui_popup_layout;
    if(!ValidOptions(options, display))
        return MakeUnexpected(Failure{});
    const f32 width = Min(options.size.x, display.logicalWidth);
    const f32 height = Min(options.size.y, display.logicalHeight);
    PopupPlacementSide::Enum side = options.side;
    if(side != PopupPlacementSide::Center){
        const f64 required = side == PopupPlacementSide::Below || side == PopupPlacementSide::Above ? height : width;
        const f64 preferredRoom = Available(options, display, side);
        if(preferredRoom < required){
            const PopupPlacementSide::Enum opposite = Opposite(side);
            const f64 oppositeRoom = Available(options, display, opposite);
            if(oppositeRoom >= required || oppositeRoom > preferredRoom)
                side = opposite;
        }
    }
    f64 x = options.anchor.x;
    f64 y = options.anchor.y;
    switch(side){
    case PopupPlacementSide::Below:
        y += static_cast<f64>(options.anchor.height) + options.gap;
        break;
    case PopupPlacementSide::Above:
        y -= static_cast<f64>(options.gap) + height;
        break;
    case PopupPlacementSide::Right:
        x += static_cast<f64>(options.anchor.width) + options.gap;
        break;
    case PopupPlacementSide::Left:
        x -= static_cast<f64>(options.gap) + width;
        break;
    case PopupPlacementSide::Center:
        x = (static_cast<f64>(display.logicalWidth) - width) * 0.5;
        y = (static_cast<f64>(display.logicalHeight) - height) * 0.5;
        break;
    }
    x = Clamp(x, 0.0, static_cast<f64>(display.logicalWidth) - width);
    y = Clamp(y, 0.0, static_cast<f64>(display.logicalHeight) - height);
    PopupPlacement candidate;
    candidate.bounds = { static_cast<f32>(x), static_cast<f32>(y), width, height };
    candidate.viewport = { 0.0f, 0.0f, display.logicalWidth, display.logicalHeight };
    candidate.side = side;
    if(!IsFinite(candidate.bounds.x + width) || !IsFinite(candidate.bounds.y + height))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

