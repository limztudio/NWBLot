// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_navigation_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditNavigationTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Ui::EditNavigationResult> RecordingNavigationResolver::resolve(const Ui::EditModel& model, const Ui::EditNavigationDirection::Enum direction, const Ui::EditNavigationSnapshot& navigation, const f32 viewportHeight){
    records.emplace_back(m_arena);
    NavigationCall& record = records.back();
    record.text.assign(model.text().data(), model.text().size());
    record.navigation = navigation;
    record.selectionGeneration = model.selectionGeneration();
    record.caret = model.caret();
    record.viewportHeight = viewportHeight;

    Expected<Ui::EditNavigationResult> result = MakeUnexpected(Failure{});
    Ui::EditBoxView view(m_arena);
    Expected<Ui::TextLayout, Ui::TextLayoutStatus::Enum> layout = MakeUnexpected(Ui::TextLayoutStatus::InvalidParameters);
    if(view.snapshot(model))
        layout = m_layoutBuilder.layout({ view.displayText() });
    if(layout && view.adoptLayout(Move(*layout))){
        const auto caret = view.caretGeometry().caretRect(view.displayCaret());
        if(caret){
            const f32 preferredX = navigation.valid ? navigation.preferredX : caret->x;
            Expected<usize> target = MakeUnexpected(Failure{});
            if(direction == Ui::EditNavigationDirection::Up || direction == Ui::EditNavigationDirection::Down)
                target = view.caretGeometry().verticalTarget(view.displayCaret(), direction == Ui::EditNavigationDirection::Down, preferredX);
            else{
                const f32 distance = direction == Ui::EditNavigationDirection::PageDown ? viewportHeight : -viewportHeight;
                target = view.caretGeometry().hitTest({ preferredX, caret->y + caret->height * 0.5f + distance });
            }
            if(target)
                result = Ui::EditNavigationResult{ *target, preferredX };
        }
    }
    if(forceResult)
        result = forcedResult;
    record.result = result;
    if(hook){
        Function<void()> callback = Move(hook);
        hook = {};
        callback();
    }
    return result;
}

b8 AcceptingNavigationActions::apply(Ui::EditModel& model, const Ui::EditAction::Enum action, const b8){
    records.emplace_back(m_arena);
    records.back().text.assign(model.text().data(), model.text().size());
    records.back().action = action;
    return true;
}

usize AcceptingNavigationActions::count(const Ui::EditAction::Enum action)const noexcept{
    usize result = 0u;
    for(const auto& record : records)
        result += record.action == action ? 1u : 0u;
    return result;
}

UiEditNavigationHostTests::UiEditNavigationHostTests()
    : m_navigationModel(m_arena, {}, Ui::EditTextMode::Multiline)
    , m_resolver(m_arena, m_layoutBuilder)
    , m_actions(m_arena)
{
    m_includeButton = true;
}

b8 UiEditNavigationHostTests::prepareNavigationModel(Ui::EditModel& model, Ui::EditNavigationState& navigation, const Ui::EditBoxOptions& options, const Ui::Rect& bounds, const Ui::Point previousScroll){
    if(!beginEditFrame())
        return false;
    m_result = m_host.editNavigated(m_widget, options, {}, model, navigation, m_resolver, m_actions);
    if(!m_result.valid)
        return false;
    if(!publishEdit(model, options, bounds, previousScroll))
        return false;
    const auto button = m_context.declare("other", Ui::WidgetKind::Button);
    if(!button)
        return false;
    m_otherWidget = **button;
    Ui::HitTarget other;
    other.rectangle = { 230.0f, 20.0f, 100.0f, 30.0f };
    other.clip = { 0.0f, 0.0f, 500.0f, 200.0f };
    other.focusable = true;
    other.activatable = true;
    if(!m_context.addTarget(m_otherWidget, other) || !m_context.endRoot() || !m_context.finishFrame())
        return false;
    m_host.finishFrame();
    return true;
}

b8 UiEditNavigationHostTests::commitNative(const AStringView text){
    if(m_textInput.commit(text) != TextInputAdmission::Accepted)
        return false;
    m_host.collectNative();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

