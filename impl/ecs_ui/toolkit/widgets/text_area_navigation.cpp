// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_navigation.h"

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_navigation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ModelStamp final{
public:
    explicit ModelStamp(const EditModel& model)noexcept
        : m_instanceGeneration(model.instanceGeneration())
        , m_revision(model.revision())
        , m_externalRevision(model.externalRevision())
        , m_selectionGeneration(model.selectionGeneration())
        , m_compositionGeneration(model.compositionGeneration())
        , m_anchor(model.anchor())
        , m_caret(model.caret())
        , m_textMode(model.textMode())
    {}


public:
    [[nodiscard]] bool matches(const EditModel& model)const noexcept{
        return
            m_instanceGeneration == model.instanceGeneration() && m_revision == model.revision()
            && m_externalRevision == model.externalRevision() && m_selectionGeneration == model.selectionGeneration()
            && m_compositionGeneration == model.compositionGeneration() && m_anchor == model.anchor() && m_caret == model.caret()
            && m_textMode == model.textMode() && !model.composition().active
        ;
    }


private:
    const u64 m_instanceGeneration;
    const u64 m_revision;
    const u64 m_externalRevision;
    const u64 m_selectionGeneration;
    const u64 m_compositionGeneration;
    const usize m_anchor;
    const usize m_caret;
    const EditTextMode::Enum m_textMode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static const EditBoxCaretStop* FindStop(const EditCaretGeometry& geometry, const usize committedByte)noexcept{
    for(const EditBoxCaretStop& stop : geometry.caretStops()){
        if(stop.committedByte == committedByte)
            return &stop;
    }
    return nullptr;
}

[[nodiscard]] static Expected<usize> PageTarget(const EditCaretGeometry& geometry, const usize activeCaret, const Rect& caret, const bool down, const f32 preferredX, const f32 viewportHeight)noexcept{
    const auto& lines = geometry.lines();
    const EditBoxCaretStop* current = FindStop(geometry, activeCaret);
    if(lines.empty() || !current || current->lineIndex >= lines.size())
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble center = VectorDoubleAdd(VectorDoubleReplicate(caret.y), VectorDoubleMultiply(VectorDoubleReplicate(caret.height), VectorDoubleReplicate(0.5)));
    const f64 target = VectorDoubleGetX(VectorDoubleAdd(center, VectorDoubleReplicate(down ? static_cast<f64>(viewportHeight) : -static_cast<f64>(viewportHeight))));
    const EditCaretLine& first = lines.front();
    const EditCaretLine& last = lines.back();
    const SIMDVectorDouble minimumMaximumValue = VectorDoubleMultiplyAddExpression(
        VectorDoubleSet(static_cast<f64>(first.height), static_cast<f64>(last.height), 0.0, 0.0),
        VectorDoubleSet(0.5, 0.5, 0.0, 0.0),
        VectorDoubleSet(static_cast<f64>(first.top), static_cast<f64>(last.top), 0.0, 0.0)
    );
    const f64 minimum = VectorDoubleGetX(minimumMaximumValue);
    const f64 maximum = VectorDoubleGetY(minimumMaximumValue);
    if(!IsFinite(target) || !IsFinite(minimum) || !IsFinite(maximum) || minimum > maximum)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble targetValue = VectorDoubleReplicate(target);
    const SIMDVectorDouble minimumValue = VectorDoubleReplicate(minimum);
    const Point point{ preferredX, static_cast<f32>(VectorDoubleGetX(VectorDoubleSelect(minimumValue, VectorDoubleMin(VectorDoubleReplicate(maximum), targetValue), VectorDoubleGreater(targetValue, minimumValue)))) };
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


TextAreaNavigationResolver::TextAreaNavigationResolver(Core::Alloc::GlobalArena& arena, TextService& text, const Context& context, const TextAreaState& state, const f32 fontSize, const u32 scriptTag, const StringView language)
    : m_arena(arena)
    , m_text(text)
    , m_context(context)
    , m_state(state)
    , m_instanceGeneration(state.instanceGeneration())
    , m_revision(state.revision())
    , m_fontSize(fontSize)
    , m_scriptTag(scriptTag)
    , m_language(language.data(), language.size(), arena)
{}

Expected<EditNavigationResult> TextAreaNavigationResolver::resolve(const EditModel& model, const EditNavigationDirection::Enum direction, const EditNavigationSnapshot& preferred, const f32 viewportHeight){
    const bool page = direction == EditNavigationDirection::PageUp || direction == EditNavigationDirection::PageDown;
    if(
        !current() || model.textMode() != EditTextMode::Multiline || model.composition().active
        || direction > EditNavigationDirection::PageDown || !IsFinite(m_fontSize) || m_fontSize <= 0.0f
        || !IsFinite(viewportHeight) || viewportHeight < 0.0f || (page && viewportHeight == 0.0f)
        || !IsFinite(preferred.preferredX) || !m_state.navigation().matches(preferred)
    )
        return MakeUnexpected(Failure{});
    const __hidden_ui_text_area_navigation::ModelStamp expected(model);
    EditBoxView view(m_arena);
    const ShapeRequest request{ {}, m_fontSize, TextDirection::LeftToRight, m_scriptTag, StringView(m_language.data(), m_language.size()) };
    if(!view.snapshot(model) || view.shape(m_text, request) != TextLayoutStatus::Success)
        return MakeUnexpected(Failure{});
    if(!current() || !expected.matches(model) || !m_state.navigation().matches(preferred))
        return MakeUnexpected(Failure{});
    const EditCaretGeometry& geometry = view.caretGeometry();
    const auto caret = geometry.caretRect(view.displayCaret());
    if(!caret)
        return MakeUnexpected(Failure{});
    const f32 preferredX = preferred.valid ? preferred.preferredX : caret->x;
    if(!IsFinite(preferredX))
        return MakeUnexpected(Failure{});
    const bool down = direction == EditNavigationDirection::Down || direction == EditNavigationDirection::PageDown;
    const auto resolved = page ? __hidden_ui_text_area_navigation::PageTarget(geometry, model.caret(), *caret, down, preferredX, viewportHeight) : geometry.verticalTarget(view.displayCaret(), down, preferredX);
    if(!resolved)
        return MakeUnexpected(resolved.error());
    return EditNavigationResult{ *resolved, preferredX };
}

bool TextAreaNavigationResolver::current()const noexcept{
    return !m_context.failed() && m_state.instanceGeneration() == m_instanceGeneration && m_state.revision() == m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

