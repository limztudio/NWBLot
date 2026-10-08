// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "text_area_navigation.h"

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_text_area{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextAreaActions final : public IEditActionSink{
public:
    TextAreaActions(const EditModel& model, const TextAreaState& state, const Context& context)
        : m_model(model)
        , m_state(state)
        , m_context(context)
        , m_instanceGeneration(model.instanceGeneration())
        , m_revision(state.revision())
    {}
    virtual ~TextAreaActions()override = default;


public:
    [[nodiscard]] virtual bool apply(EditModel& model, const EditAction::Enum action, const bool)noexcept override{
        if(
            m_context.failed() || &model != &m_model
            || model.instanceGeneration() != m_instanceGeneration || m_state.revision() != m_revision
        )
            return false;
        return action <= EditAction::Abandon;
    }


private:
    const EditModel& m_model;
    const TextAreaState& m_state;
    const Context& m_context;
    const u64 m_instanceGeneration;
    const u64 m_revision;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxResult Builder::textArea(const AStringView stableKey, EditModel& model, TextAreaState& state, const TextAreaOptions& options){
    if(
        declarationBlocked() || !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed)
        || !IsFinite(options.wheelLines) || options.wheelLines <= 0.0f
        || model.textMode() != EditTextMode::Multiline || m_context.failed() || m_scope->m_items.size() >= s_LayoutMaxNodes
    ){
        m_context.fail();
        return {};
    }
    if(!synchronizePopup()){
        EditBoxResult result;
        result.valid = true;
        return result;
    }
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::TextArea);
    if(!widget)
        return {};
    if(!textAreaAvailable(model, state) || !m_context.claimState(*widget, model.instanceGeneration())){
        m_context.fail();
        return {};
    }
    if(!options.enabled)
        m_context.input().invalidateTarget(widget->id);
    const u64 stateRevision = state.revision();
    const u64 modelGeneration = model.instanceGeneration();
    const EditBoxOptions editOptions{ options.width, options.height, options.enabled, options.readOnly };
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_context.input().focus() == widget->id;
    if(m_editHost){
        TextAreaNavigationResolver navigation(m_arena, m_text, m_context, state, m_style.fontSize,
            m_textScriptTag, StringView(m_textLanguage.data(), m_textLanguage.size()));
        __hidden_builder_text_area::TextAreaActions actions(model, state, m_context);
        result = m_editHost->editNavigated(*widget, model, editOptions, m_context.popupToken(), state.m_navigation, navigation, actions);
    }
    if(!result.valid || m_context.failed() || state.revision() != stateRevision || model.instanceGeneration() != modelGeneration){
        result.valid = false;
        m_context.fail();
        return result;
    }
    const bool firstBinding = state.m_visual.modelGeneration == 0u;
    const bool rebound = !firstBinding && state.m_visual.modelGeneration != modelGeneration;
    const Point requestedScroll = state.scroll();
    if(rebound){
        state.m_visual.scroll = 0.0f;
        state.m_scrollY = 0.0f;
        state.m_revealCaret = true;
    }
    else if(
        !firstBinding && (state.m_visual.revision != model.revision()
        || state.m_visual.selectionGeneration != model.selectionGeneration()
        || state.m_compositionGeneration != model.compositionGeneration()
        || state.m_visual.anchor != model.anchor() || state.m_visual.caret != model.caret()
        || (!state.m_visual.focused && result.focused
            && (!m_context.input().capture().valid() || m_context.input().capture() == widget->id)))
    )
        state.m_revealCaret = true;
    if(!firstBinding && state.m_compositionGeneration != model.compositionGeneration())
        state.m_visual.caretElapsed = 0.0f;
    TextAreaFrame frame;
    frame.wheelLines = options.wheelLines;
    frame.model = &model;
    frame.state = &state;
    frame.focused = result.focused;
    snapshotTextArea(frame);
    Item item(m_arena);
    item.style = m_style;
    item.editStyle = m_editStyle;
    item.scrollbarStyle = m_scrollbarStyle;
    item.state = *widget;
    item.editOptions = editOptions;
    if(!prepareEditBox(item, model, state.m_visual, result) || !textAreaMatches(frame)){
        result.valid = false;
        m_context.fail();
        return result;
    }
    if(firstBinding && !state.m_revealCaret){
        state.m_visual.scroll = requestedScroll.x;
        state.m_scrollY = requestedScroll.y;
    }
    state.m_compositionGeneration = model.compositionGeneration();
    item.textArea = static_cast<u32>(m_scope->m_textAreas.size());
    Point minimum{ 120.0f, 0.0f };
    const Name names[]{ m_editStyle.normal, m_editStyle.hover, m_editStyle.focused, m_editStyle.disabled };
    for(const auto& name : names){
        const UiSkinRegion* skinRegion = region(name, m_editStyle.fallback);
        if(skinRegion){
            minimum.x = Max(minimum.x, skinRegion->minimumWidth);
            minimum.y = Max(minimum.y, skinRegion->minimumHeight);
        }
    }
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = { minimum.x, Max(minimum.y, item.editView.layout().measure().y + item.padding.top + item.padding.bottom) };
    const auto admittedNode = m_scope->m_layout.addNode(m_scope->m_stack.back(), description);
    if(!admittedNode){
        m_context.fail();
        result.valid = false;
        return result;
    }
    item.node = *admittedNode;
    frame.item = static_cast<u32>(m_scope->m_items.size());
    m_scope->m_items.push_back(Move(item));
    m_scope->m_textAreas.push_back(frame);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

