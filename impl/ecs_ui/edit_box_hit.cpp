// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/simd/vector_arithmetic.h>
#include <global/simd/vector_rounding.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_box_hit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Ui::Point> ResolveLocalEditTextPosition(
    const UiEditBoxGeometry& geometry,
    const UiTextEditOwner& owner,
    const Ui::PopupToken& popup,
    const Ui::Point position
)noexcept{
    if(
        geometry.generation == 0u || geometry.stops.empty() || geometry.lines.empty()
        || geometry.modelGeneration != owner.modelGeneration || geometry.popup != popup
        || (geometry.textMode != Ui::EditTextMode::SingleLine && geometry.textMode != Ui::EditTextMode::Multiline)
        || (geometry.textMode == Ui::EditTextMode::SingleLine && geometry.lines.size() != 1u)
    )
        return MakeUnexpected(Failure{});
    return Ui::Point{ position.x - geometry.placement.textOrigin.x, position.y - geometry.placement.textOrigin.y };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::TextInputRect UiEditBoxHost::nativeCaret(const UiEditBoxGeometry& geometry)const noexcept{
    const Ui::Rect& caret = geometry.placement.caret;
    const SIMDVectorSingle physical = VectorMultiply(VectorSet(caret.x, caret.y, caret.width, caret.height), VectorSet(m_display.pixelScaleX, m_display.pixelScaleY, m_display.pixelScaleX, m_display.pixelScaleY));
    const SIMDVectorSingle origin = VectorFloor(VectorSwizzle<0, 1, 0, 1>(physical));
    const SIMDVectorSingle size = VectorCeiling(VectorSwizzle<2, 3, 2, 3>(physical));
    const SIMDVectorDouble coordinate = VectorDoubleMin(
        VectorDoubleReplicate(static_cast<f64>(Limit<i32>::s_Max)),
        VectorDoubleMax(ConvertVectorSingleToDouble(VectorPermute<0, 1, 4, 5>(origin, size)), VectorDoubleReplicate(static_cast<f64>(Limit<i32>::s_Min)))
    );
    return { static_cast<i32>(VectorDoubleGetX(coordinate)), static_cast<i32>(VectorDoubleGetY(coordinate)), Max(1, static_cast<i32>(VectorDoubleGetZ(coordinate))), Max(1, static_cast<i32>(VectorDoubleGetW(coordinate))) };
}

Expected<usize> UiEditBoxHost::hit(const Entry& entry, const Ui::Point position)const noexcept{
    const auto local = __hidden_ui_edit_box_hit::ResolveLocalEditTextPosition(entry.displayed, entry.owner, entry.popup, position);
    if(!local)
        return MakeUnexpected(Failure{});
    return Ui::HitEditCaretGeometry(entry.displayed.lines, entry.displayed.stops, *local);
}

Expected<usize> UiEditBoxHost::hitWord(const Entry& entry, const Ui::Point position)const noexcept{
    const auto local = __hidden_ui_edit_box_hit::ResolveLocalEditTextPosition(entry.displayed, entry.owner, entry.popup, position);
    if(!local)
        return MakeUnexpected(Failure{});
    return Ui::HitEditWordCaretGeometry(entry.displayed.lines, entry.displayed.stops, *local);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

