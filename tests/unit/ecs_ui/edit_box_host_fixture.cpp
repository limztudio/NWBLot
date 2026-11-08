// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"

#include <tests/unit/ui/name_cases.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditBoxTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiEditBoxHostTests::UiEditBoxHostTests()
    : m_arena(Name(NWB::Tests::UiCases::s_EcsUiEditBoxHost))
    , m_context(m_arena)
    , m_textInput(m_arena)
    , m_clipboard(m_arena)
    , m_host(m_arena, m_context, m_textInput, m_clipboard)
    , m_model(m_arena)
    , m_shaper(m_arena)
    , m_layoutBuilder(m_arena, m_shaper)
{}

b8 UiEditBoxHostTests::beginEditFrame(){
    ++m_generation;
    if(!m_context.beginFrame(m_generation))
        return false;
    m_host.beginFrame(m_generation, { 500.0f, 200.0f, 1.0f, 1.0f });
    if(!m_context.beginRoot({ 17u, 1u }))
        return false;
    const auto widget = m_context.declare("edit", Ui::WidgetKind::EditBox);
    if(!widget)
        return false;
    m_widget = **widget;
    return true;
}

b8 UiEditBoxHostTests::prepare(Ui::EditModel& model, const Ui::EditBoxOptions& options, const Ui::Rect& bounds, Ui::IEditActionSink* actions){
    if(!beginEditFrame())
        return false;
    m_result = actions ? m_host.editActions(m_widget, options, {}, model, *actions) : m_host.edit(m_widget, options, model);
    if(!m_result.valid)
        return false;
    if(!publishEdit(model, options, bounds))
        return false;
    if(m_includeButton){
        const auto button = m_context.declare("other", Ui::WidgetKind::Button);
        if(!button)
            return false;
        m_otherWidget = **button;
        Ui::HitTarget other;
        other.rectangle = { 230.0f, 20.0f, 100.0f, 30.0f };
        other.clip = { 0.0f, 0.0f, 500.0f, 200.0f };
        other.focusable = true;
        other.activatable = true;
        if(!m_context.addTarget(m_otherWidget, other))
            return false;
    }
    if(!m_context.endRoot() || !m_context.finishFrame())
        return false;
    m_host.finishFrame();
    return true;
}

b8 UiEditBoxHostTests::publishEdit(Ui::EditModel& model, const Ui::EditBoxOptions& options, const Ui::Rect& bounds, const Ui::Point previousScroll){
    Ui::EditBoxView view(m_arena);
    if(!UiEditHostFixtureSupport::PrepareEditView(model, m_layoutBuilder, view))
        return false;
    const auto placement = view.arrange(bounds, {}, { 0.0f, 0.0f, 500.0f, 200.0f }, previousScroll);
    if(!placement)
        return false;
    m_placement = *placement;
    if(!m_host.publish(m_widget, view, m_placement, options))
        return false;
    Ui::HitTarget target;
    target.rectangle = bounds;
    target.clip = { 0.0f, 0.0f, 500.0f, 200.0f };
    target.enabled = options.enabled;
    target.focusable = options.enabled;
    target.pointerGesture = options.enabled;
    target.textEditable = true;
    return m_context.addTarget(m_widget, target);
}

b8 UiEditBoxHostTests::commit(){
    if(!m_context.commitFrame(m_generation))
        return false;
    m_host.commitFrame(m_generation);
    return true;
}

b8 UiEditBoxHostTests::dispatch(const Ui::InputEvent& event){
    m_host.collectNative();
    const Ui::WidgetId previousCapture = m_context.input().capture();
    const auto normalized = m_context.input().queue(UiTestSupport::NativeInput(event));
    if(!normalized)
        return false;
    const b8 submit = m_host.prepareSubmit(*normalized);
    const Ui::InputRoutingResult result = m_context.input().process();
    m_host.input(*normalized, previousCapture, submit);
    return !result.activationOverflow && !result.gestureOverflow;
}

b8 UiEditBoxHostTests::dispatchPointer(const Ui::PointerInput& input){
    m_host.collectNative();
    const Ui::WidgetId previousCapture = m_context.input().capture();
    const auto normalized = m_context.input().queuePointer(input);
    if(!normalized)
        return false;
    const Ui::InputRoutingResult result = m_context.input().process();
    if(result.pointerRouted)
        m_host.input(*normalized, previousCapture, false);
    return !result.activationOverflow && !result.gestureOverflow;
}

b8 UiEditBoxHostTests::emptyFrame(){
    ++m_generation;
    if(!m_context.beginFrame(m_generation))
        return false;
    m_host.beginFrame(m_generation, { 500.0f, 200.0f, 1.0f, 1.0f });
    if(!m_context.beginRoot({ 17u, 1u }) || !m_context.endRoot() || !m_context.finishFrame())
        return false;
    m_host.finishFrame();
    return commit();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

