// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_navigation.h"
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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
    const TextAreaModelStamp expected = model.stamp();
    EditBoxView view(m_arena);
    const ShapeRequest request{ {}, StringView(m_language.data(), m_language.size()), m_fontSize, m_scriptTag, TextDirection::LeftToRight };
    if(!view.snapshot(model) || view.shape(m_text, request) != TextLayoutStatus::Success)
        return MakeUnexpected(Failure{});
    if(!current() || !model.matchesStamp(expected) || model.composition().active || !m_state.navigation().matches(preferred))
        return MakeUnexpected(Failure{});
    const EditCaretGeometry& geometry = view.caretGeometry();
    const auto caret = geometry.caretRect(view.displayCaret());
    if(!caret)
        return MakeUnexpected(Failure{});
    const f32 preferredX = preferred.valid ? preferred.preferredX : caret->x;
    if(!IsFinite(preferredX))
        return MakeUnexpected(Failure{});
    const b8 down = direction == EditNavigationDirection::Down || direction == EditNavigationDirection::PageDown;
    const auto resolved = page ? ResolveEditCaretPageTarget(geometry.lines(), geometry.caretStops(), model.caret(), *caret, down, preferredX, viewportHeight) : geometry.verticalTarget(view.displayCaret(), down, preferredX);
    if(!resolved)
        return MakeUnexpected(resolved.error());
    return EditNavigationResult{ *resolved, preferredX };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
