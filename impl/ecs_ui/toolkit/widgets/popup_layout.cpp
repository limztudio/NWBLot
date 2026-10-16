// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup.h"

#include <global/simd/vector_arithmetic.h>
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PopupPlacement> PopupLayout::Place(const PopupOptions& options, const DisplayMetrics& display)noexcept{
    using namespace __hidden_ui_popup_layout;
    if(!ValidOptions(options, display))
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle requestedSize = VectorSet(options.size.x, options.size.y, options.size.x, options.size.y);
    const SIMDVectorSingle displaySize = VectorSet(display.logicalWidth, display.logicalHeight, display.logicalWidth, display.logicalHeight);
    const SIMDVectorSingle boundedSize = VectorSelect(displaySize, requestedSize, VectorLess(requestedSize, displaySize));
    const f32 width = VectorGetX(boundedSize);
    const f32 height = VectorGetY(boundedSize);
    PopupPlacementSide::Enum side = options.side;
    if(side != PopupPlacementSide::Center){
        const bool vertical = side == PopupPlacementSide::Below || side == PopupPlacementSide::Above;
        const f64 required = vertical ? height : width;
        const f64 anchorOrigin = vertical ? options.anchor.y : options.anchor.x;
        const f64 anchorExtent = vertical ? options.anchor.height : options.anchor.width;
        const f64 displayExtent = vertical ? display.logicalHeight : display.logicalWidth;
        // Validated f32 coordinates make both f64 room expressions finite even when only one side is needed.
        const SIMDVectorDouble distance = VectorDoubleSubtract(VectorDoubleSubtract(VectorDoubleSubtract(VectorDoubleSet(displayExtent, anchorOrigin, 0.0, 0.0), VectorDoubleSet(anchorOrigin, 0.0, 0.0, 0.0)), VectorDoubleSet(anchorExtent, 0.0, 0.0, 0.0)), VectorDoubleSet(options.gap, options.gap, 0.0, 0.0));
        const SIMDVectorDouble maximum = VectorDoubleReplicate(displayExtent);
        const SIMDVectorDouble room = VectorDoubleSelect(VectorDoubleZero(), VectorDoubleSelect(distance, maximum, VectorDoubleGreater(distance, maximum)), VectorDoubleGreater(distance, VectorDoubleZero()));
        const bool forward = side == PopupPlacementSide::Below || side == PopupPlacementSide::Right;
        const f64 preferredRoom = forward ? VectorDoubleGetX(room) : VectorDoubleGetY(room);
        if(preferredRoom < required){
            const PopupPlacementSide::Enum opposite = Opposite(side);
            const f64 oppositeRoom = forward ? VectorDoubleGetY(room) : VectorDoubleGetX(room);
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
    case PopupPlacementSide::Center:{
        const SIMDVectorDouble centered = VectorDoubleMultiply(VectorDoubleSubtract(VectorDoubleSet(display.logicalWidth, display.logicalHeight, 0.0, 0.0), VectorDoubleSet(width, height, 0.0, 0.0)), VectorDoubleSet(0.5, 0.5, 0.0, 0.0));
        x = VectorDoubleGetX(centered);
        y = VectorDoubleGetY(centered);
        break;
    }
    }
    const SIMDVectorDouble origin = VectorDoubleSet(x, y, 0.0, 0.0);
    const SIMDVectorDouble maximum = VectorDoubleSubtract(VectorDoubleSet(display.logicalWidth, display.logicalHeight, 0.0, 0.0), VectorDoubleSet(width, height, 0.0, 0.0));
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble constrained = VectorDoubleSelect(zero, VectorDoubleSelect(origin, maximum, VectorDoubleGreater(origin, maximum)), VectorDoubleGreater(origin, zero));
    x = VectorDoubleGetX(constrained);
    y = VectorDoubleGetY(constrained);
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

