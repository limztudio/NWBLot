// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rectangle.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVectorDouble InsetUiRectValue(const Rect& bounds, const Insets& padding)noexcept{
    const SIMDVectorDouble boundsSize = VectorDoubleSet(static_cast<f64>(bounds.width), static_cast<f64>(bounds.height), 0.0, 0.0);
    const SIMDVectorDouble paddingStart = VectorDoubleSet(static_cast<f64>(padding.left), static_cast<f64>(padding.top), 0.0, 0.0);
    const SIMDVectorDouble firstInset = VectorDoubleSelect(boundsSize, paddingStart, VectorDoubleLess(paddingStart, boundsSize));
    const SIMDVectorDouble remaining = VectorDoubleSubtract(boundsSize, firstInset);
    const SIMDVectorDouble paddingEnd = VectorDoubleSet(static_cast<f64>(padding.right), static_cast<f64>(padding.bottom), 0.0, 0.0);
    const SIMDVectorDouble lastInset = VectorDoubleSelect(remaining, paddingEnd, VectorDoubleLess(paddingEnd, remaining));
    const SIMDVectorDouble origin = VectorDoubleAdd(VectorDoubleSet(static_cast<f64>(bounds.x), static_cast<f64>(bounds.y), 0.0, 0.0), firstInset);
    const SIMDVectorDouble size = VectorDoubleSubtract(remaining, lastInset);
    return VectorDoublePermute<0, 1, 4, 5>(origin, size);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

