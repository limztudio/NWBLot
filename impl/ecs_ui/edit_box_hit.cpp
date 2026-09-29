// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::TextInputRect UiEditBoxHost::nativeCaret(const UiEditBoxGeometry& geometry)const{
    const Ui::Rect& caret = geometry.placement.caret;
    const auto coordinate = [](const f32 value){
        return static_cast<i32>(Clamp(static_cast<f64>(value), static_cast<f64>(Limit<i32>::s_Min), static_cast<f64>(Limit<i32>::s_Max)));
    };
    return { coordinate(Floor(caret.x * m_display.pixelScaleX)), coordinate(Floor(caret.y * m_display.pixelScaleY)),
        Max(1, coordinate(Ceil(caret.width * m_display.pixelScaleX))), Max(1, coordinate(Ceil(caret.height * m_display.pixelScaleY))) };
}

bool UiEditBoxHost::hit(const Entry& entry, const Ui::Point position, usize& byte)const{
    const UiEditBoxGeometry& geometry = entry.displayed;
    if(
        geometry.generation == 0u || geometry.stops.empty() || geometry.lines.empty()
        || geometry.modelGeneration != entry.owner.modelGeneration || geometry.popup != entry.popup
        || (geometry.textMode != Ui::EditTextMode::SingleLine && geometry.textMode != Ui::EditTextMode::Multiline)
        || (geometry.textMode == Ui::EditTextMode::SingleLine && geometry.lines.size() != 1u)
    )
        return false;
    const Ui::Point local{ position.x - geometry.placement.textOrigin.x, position.y - geometry.placement.textOrigin.y };
    return Ui::HitEditCaretGeometry(geometry.lines, geometry.stops, local, byte);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

