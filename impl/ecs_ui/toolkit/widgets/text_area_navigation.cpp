// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_navigation.h"
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_navigation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static const EditBoxCaretStop* FindStop(const EditCaretGeometry& geometry, const usize committedByte)noexcept{
    for(const EditBoxCaretStop& stop : geometry.caretStops()){
        if(stop.committedByte == committedByte)
            return &stop;
    }
    return nullptr;
}

[[nodiscard]] static Expected<usize> PageTarget(const EditCaretGeometry& geometry, const usize activeCaret, const Rect& caret, const b8 down, const f32 preferredX, const f32 viewportHeight)noexcept{
    const auto& lines = geometry.lines();
    const EditBoxCaretStop* current = FindStop(geometry, activeCaret);
    if(lines.empty() || !current || current->lineIndex >= lines.size())
        return MakeUnexpected(Failure{});
    const f64 center = static_cast<f64>(caret.y) + (static_cast<f64>(caret.height) * s_RectCenterFractionF64);
    const f64 pageStep = static_cast<f64>(viewportHeight);
    const f64 target = center + (down ? pageStep : -pageStep);
    const EditCaretLine& first = lines.front();
    const EditCaretLine& last = lines.back();
    const f64 minimum = static_cast<f64>(first.top) + (static_cast<f64>(first.height) * s_RectCenterFractionF64);
    const f64 maximum = static_cast<f64>(last.top) + (static_cast<f64>(last.height) * s_RectCenterFractionF64);
    if(!IsFinite(target) || !IsFinite(minimum) || !IsFinite(maximum) || minimum > maximum)
        return MakeUnexpected(Failure{});
    const Point point{ preferredX, static_cast<f32>(Clamp(target, minimum, maximum)) };
    if(!IsFinite(point.y))
        return MakeUnexpected(Failure{});
    const auto result = geometry.hitTest(point);
    if(!result)
        return MakeUnexpected(Failure{});
    const EditBoxCaretStop* resolved = FindStop(geometry, *result);
    if(!resolved || resolved->lineIndex >= lines.size())
        return MakeUnexpected(Failure{});
    // A page that cannot reach another row preserves the active caret, including a short document boundary.
    return resolved->lineIndex == current->lineIndex ? activeCaret : *result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<EditNavigationResult> TextAreaNavigationResolver::resolve(const EditModel& model, const EditNavigationDirection::Enum direction, const EditNavigationSnapshot& preferred, const f32 viewportHeight){
    const b8 page = direction == EditNavigationDirection::PageUp || direction == EditNavigationDirection::PageDown;
    if(
        !current() || model.textMode() != EditTextMode::Multiline || model.composition().active
        || direction > EditNavigationDirection::PageDown || !IsFinite(m_fontSize) || m_fontSize <= 0.0f
        || !IsFinite(viewportHeight) || viewportHeight < 0.0f || (page && viewportHeight == 0.0f)
        || !IsFinite(preferred.preferredX) || !m_state.navigation().matches(preferred)
    )
        return MakeUnexpected(Failure{});
    const TextAreaModelStamp expected(
        model.instanceGeneration(),
        model.revision(),
        model.externalRevision(),
        model.selectionGeneration(),
        model.compositionGeneration(),
        model.anchor(),
        model.caret(),
        model.textMode()
    );
    EditBoxView view(m_arena);
    const ShapeRequest request{ {}, StringView(m_language.data(), m_language.size()), m_fontSize, m_scriptTag, TextDirection::LeftToRight };
    if(!view.snapshot(model) || view.shape(m_text, request) != TextLayoutStatus::Success)
        return MakeUnexpected(Failure{});
    if(
        !current()
        || expected.instanceGeneration != model.instanceGeneration() || expected.revision != model.revision()
        || expected.externalRevision != model.externalRevision() || expected.selectionGeneration != model.selectionGeneration()
        || expected.compositionGeneration != model.compositionGeneration() || expected.anchor != model.anchor() || expected.caret != model.caret()
        || expected.textMode != model.textMode() || model.composition().active
        || !m_state.navigation().matches(preferred)
    )
        return MakeUnexpected(Failure{});
    const EditCaretGeometry& geometry = view.caretGeometry();
    const auto caret = geometry.caretRect(view.displayCaret());
    if(!caret)
        return MakeUnexpected(Failure{});
    const f32 preferredX = preferred.valid ? preferred.preferredX : caret->x;
    if(!IsFinite(preferredX))
        return MakeUnexpected(Failure{});
    const b8 down = direction == EditNavigationDirection::Down || direction == EditNavigationDirection::PageDown;
    const auto resolved = page ? __hidden_ui_text_area_navigation::PageTarget(geometry, model.caret(), *caret, down, preferredX, viewportHeight) : geometry.verticalTarget(view.displayCaret(), down, preferredX);
    if(!resolved)
        return MakeUnexpected(resolved.error());
    return EditNavigationResult{ *resolved, preferredX };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

