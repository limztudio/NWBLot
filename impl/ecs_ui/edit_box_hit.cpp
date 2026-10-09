// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/math/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::TextInputRect UiEditBoxHost::nativeCaret(const UiEditBoxGeometry& geometry)const noexcept{
    const Ui::Rect& caret = geometry.placement.caret;
    const auto coordinate = [](const f32 value)noexcept{
        return static_cast<i32>(Clamp(static_cast<f64>(value), static_cast<f64>(Limit<i32>::s_Min), static_cast<f64>(Limit<i32>::s_Max)));
    };
    const SIMDVector physical = VectorMultiply(VectorSet(caret.x, caret.y, caret.width, caret.height),
        VectorSet(m_display.pixelScaleX, m_display.pixelScaleY, m_display.pixelScaleX, m_display.pixelScaleY));
    const SIMDVector origin = VectorFloor(VectorSwizzle<0, 1, 0, 1>(physical));
    const SIMDVector size = VectorCeiling(VectorSwizzle<2, 3, 2, 3>(physical));
    return { coordinate(VectorGetX(origin)), coordinate(VectorGetY(origin)),
        Max(1, coordinate(VectorGetX(size))), Max(1, coordinate(VectorGetY(size))) };
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
    const SIMDVector localPosition = VectorSubtract(VectorSet(position.x, position.y, position.x, position.y),
        VectorSet(geometry.placement.textOrigin.x, geometry.placement.textOrigin.y,
            geometry.placement.textOrigin.x, geometry.placement.textOrigin.y));
    const Ui::Point local{ VectorGetX(localPosition), VectorGetY(localPosition) };
    return Ui::HitEditCaretGeometry(geometry.lines, geometry.stops, local);
}

Expected<usize> UiEditBoxHost::hitWord(const Entry& entry, const Ui::Point position)const noexcept{
    const auto byte = hit(entry, position);
    if(!byte)
        return MakeUnexpected(byte.error());
    const UiEditBoxGeometry& geometry = entry.displayed;
    const SIMDVector localPosition = VectorSubtract(VectorSet(position.x, position.y, position.x, position.y),
        VectorSet(geometry.placement.textOrigin.x, geometry.placement.textOrigin.y,
            geometry.placement.textOrigin.x, geometry.placement.textOrigin.y));
    const Ui::Point local{ VectorGetX(localPosition), VectorGetY(localPosition) };
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

