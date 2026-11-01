// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup.h"
#include <impl/ecs_ui/toolkit/rect_math.h>


#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PopupPlacement> PopupLayout::Place(const PopupOptions& options, const DisplayMetrics& display)noexcept{
    if(!IsValidPopupOptions(options, display))
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle requestedSize = VectorSet(options.size.x, options.size.y, options.size.x, options.size.y);
    const SIMDVectorSingle displaySize = VectorSet(display.logicalWidth, display.logicalHeight, display.logicalWidth, display.logicalHeight);
    const SIMDVectorSingle boundedSize = VectorSelect(displaySize, requestedSize, VectorLess(requestedSize, displaySize));
    const f32 width = VectorGetX(boundedSize);
    const f32 height = VectorGetY(boundedSize);
    PopupPlacementSide::Enum side = options.side;
    if(side != PopupPlacementSide::Center){
        const b8 vertical = side == PopupPlacementSide::Below || side == PopupPlacementSide::Above;
        const f64 required = vertical ? height : width;
        const f64 anchorOrigin = vertical ? options.anchor.y : options.anchor.x;
        const f64 anchorExtent = vertical ? options.anchor.height : options.anchor.width;
        const f64 displayExtent = vertical ? display.logicalHeight : display.logicalWidth;
        // Validated f32 coordinates make both f64 room expressions finite even when only one side is needed.
        const SIMDVectorDouble distance = VectorDoubleSubtract(
            VectorDoubleSubtract(VectorDoubleSubtract(VectorDoubleSet(displayExtent, anchorOrigin, 0.0, 0.0), VectorDoubleSet(anchorOrigin, 0.0, 0.0, 0.0)), VectorDoubleSet(anchorExtent, 0.0, 0.0, 0.0)),
            VectorDoubleSet(options.gap, options.gap, 0.0, 0.0)
        );
        const SIMDVectorDouble maximum = VectorDoubleReplicate(displayExtent);
        const SIMDVectorDouble room = VectorDoubleSelect(VectorDoubleZero(), VectorDoubleSelect(distance, maximum, VectorDoubleGreater(distance, maximum)), VectorDoubleGreater(distance, VectorDoubleZero()));
        const b8 forward = side == PopupPlacementSide::Below || side == PopupPlacementSide::Right;
        const f64 preferredRoom = forward ? VectorDoubleGetX(room) : VectorDoubleGetY(room);
        if(preferredRoom < required){
            const PopupPlacementSide::Enum opposite = OppositePopupPlacementSide(side);
            const f64 oppositeRoom = forward ? VectorDoubleGetY(room) : VectorDoubleGetX(room);
            if(oppositeRoom >= required || oppositeRoom > preferredRoom)
                side = opposite;
        }
    }
    // Each side moves only its own lane; the other lane keeps the anchor value.
    SIMDVectorDouble origin = VectorDoubleSet(options.anchor.x, options.anchor.y, 0.0, 0.0);
    switch(side){
    case PopupPlacementSide::Below:
        origin = VectorDoubleSelect(origin, VectorDoubleAdd(VectorDoubleSplatY(origin), VectorDoubleAdd(VectorDoubleReplicate(static_cast<f64>(options.anchor.height)), VectorDoubleReplicate(static_cast<f64>(options.gap)))), s_DoubleSIMDMaskY);
        break;
    case PopupPlacementSide::Above:
        origin = VectorDoubleSelect(origin, VectorDoubleSubtract(VectorDoubleSplatY(origin), VectorDoubleAdd(VectorDoubleReplicate(static_cast<f64>(options.gap)), VectorDoubleReplicate(static_cast<f64>(height)))), s_DoubleSIMDMaskY);
        break;
    case PopupPlacementSide::Right:
        origin = VectorDoubleSelect(origin, VectorDoubleAdd(VectorDoubleSplatX(origin), VectorDoubleAdd(VectorDoubleReplicate(static_cast<f64>(options.anchor.width)), VectorDoubleReplicate(static_cast<f64>(options.gap)))), s_DoubleSIMDMaskX);
        break;
    case PopupPlacementSide::Left:
        origin = VectorDoubleSelect(origin, VectorDoubleSubtract(VectorDoubleSplatX(origin), VectorDoubleAdd(VectorDoubleReplicate(static_cast<f64>(options.gap)), VectorDoubleReplicate(static_cast<f64>(width)))), s_DoubleSIMDMaskX);
        break;
    case PopupPlacementSide::Center:
        origin = VectorDoubleSubtract(VectorDoubleSet(display.logicalWidth, display.logicalHeight, 0.0, 0.0), VectorDoubleSet(width, height, 0.0, 0.0));
        origin = VectorDoubleMultiply(origin, VectorDoubleSet(s_RectCenterFractionF64, s_RectCenterFractionF64, 0.0, 0.0));
        break;
    }
    const SIMDVectorDouble maximum = VectorDoubleSubtract(VectorDoubleSet(display.logicalWidth, display.logicalHeight, 0.0, 0.0), VectorDoubleSet(width, height, 0.0, 0.0));
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble constrained = VectorDoubleSelect(zero, VectorDoubleSelect(origin, maximum, VectorDoubleGreater(origin, maximum)), VectorDoubleGreater(origin, zero));
    PopupPlacement candidate;
    candidate.bounds = { static_cast<f32>(VectorDoubleGetX(constrained)), static_cast<f32>(VectorDoubleGetY(constrained)), width, height };
    candidate.viewport = { 0.0f, 0.0f, display.logicalWidth, display.logicalHeight };
    candidate.side = side;
    if(!VectorIsFinite(VectorAdd(VectorSet(candidate.bounds.x, candidate.bounds.y, 0.0f, 0.0f), boundedSize), VectorComponentMask::s_XY))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

