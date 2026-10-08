// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::TextInputRect UiEditBoxHost::nativeCaret(const UiEditBoxGeometry& geometry)const noexcept{
    const Ui::Rect& caret = geometry.placement.caret;
    const auto coordinate = [](const f32 value)noexcept{
        return static_cast<i32>(Clamp(static_cast<f64>(value), static_cast<f64>(Limit<i32>::s_Min), static_cast<f64>(Limit<i32>::s_Max)));
    };
    return { coordinate(Floor(caret.x * m_display.pixelScaleX)), coordinate(Floor(caret.y * m_display.pixelScaleY)),
        Max(1, coordinate(Ceil(caret.width * m_display.pixelScaleX))), Max(1, coordinate(Ceil(caret.height * m_display.pixelScaleY))) };
}

Expected<usize> UiEditBoxHost::hit(const Entry& entry, const Ui::Point position)const noexcept{
    const UiEditBoxGeometry& geometry = entry.displayed;
    if(
        geometry.generation == 0u || geometry.stops.empty() || geometry.lines.empty()
        || geometry.modelGeneration != entry.owner.modelGeneration || geometry.popup != entry.popup
        || (geometry.textMode != Ui::EditTextMode::SingleLine && geometry.textMode != Ui::EditTextMode::Multiline)
        || (geometry.textMode == Ui::EditTextMode::SingleLine && geometry.lines.size() != 1u)
    )
        return MakeUnexpected(Failure{});
    const Ui::Point local{ position.x - geometry.placement.textOrigin.x, position.y - geometry.placement.textOrigin.y };
    return Ui::HitEditCaretGeometry(geometry.lines, geometry.stops, local);
}

Expected<usize> UiEditBoxHost::hitWord(const Entry& entry, const Ui::Point position)const noexcept{
    const auto byte = hit(entry, position);
    if(!byte)
        return MakeUnexpected(byte.error());
    const UiEditBoxGeometry& geometry = entry.displayed;
    const Ui::Point local{ position.x - geometry.placement.textOrigin.x, position.y - geometry.placement.textOrigin.y };
    u32 lineIndex = static_cast<u32>(geometry.lines.size() - 1u);
    for(u32 index = 0u; index < geometry.lines.size(); ++index){
        if(local.y < geometry.lines[index].top + geometry.lines[index].height){
            lineIndex = index;
            break;
        }
    }
    const Ui::EditCaretLine& line = geometry.lines[lineIndex];
    if(line.stopCount < 2u)
        return *byte;
    const usize first = line.firstStop;
    const usize end = first + line.stopCount;
    usize selected = first;
    for(usize index = first + 1u; index < end; ++index){
        if(geometry.stops[index].lineIndex != lineIndex || geometry.stops[index].x < geometry.stops[index - 1u].x)
            return MakeUnexpected(Failure{});
        if(local.x < geometry.stops[index].x)
            break;
        selected = index;
    }
    if(selected + 1u == end)
        --selected;
    return geometry.stops[selected].committedByte;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

