// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/text_area_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/text_area_navigation.h>
#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_text_area{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextAreaActions final : public IEditActionSink{
public:
    TextAreaActions(const EditModel& model, const TextAreaState& state, const Context& context)noexcept
        : m_model(model)
        , m_state(state)
        , m_context(context)
        , m_instanceGeneration(model.instanceGeneration())
        , m_revision(state.revision())
    {}
    virtual ~TextAreaActions()noexcept override = default;


public:
    [[nodiscard]] virtual b8 apply(EditModel& model, const EditAction::Enum action, const b8)noexcept override{
        if(m_context.failed() || &model != &m_model || model.instanceGeneration() != m_instanceGeneration || m_state.revision() != m_revision)
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


EditBoxResult TextAreaCoordinator::declareTextArea(const AStringView stableKey, EditModel& model, TextAreaState& state, const TextAreaOptions& options){
    if(m_builder.itemDeclarationBlocked() || !IsFinite(options.wheelLines) || options.wheelLines <= 0.0f || model.textMode() != EditTextMode::Multiline){
        m_builder.m_context.fail();
        return {};
    }
    if(!m_builder.synchronizePopup()){
        EditBoxResult result;
        result.valid = true;
        return result;
    }
    m_builder.m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_builder.m_declaring = false; });
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::TextArea);
    if(!widget)
        return {};
    if(!textAreaAvailable(model, state) || !m_builder.m_context.claimState(**widget, model.instanceGeneration())){
        m_builder.m_context.fail();
        return {};
    }
    if(!options.enabled)
        m_builder.m_context.input().invalidateTarget((*widget)->id);
    const u64 stateRevision = state.revision();
    const u64 modelGeneration = model.instanceGeneration();
    const EditBoxOptions editOptions{ options.width, options.height, options.enabled, options.readOnly };
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_builder.m_context.input().focus() == (*widget)->id;
    if(m_builder.m_editHost){
        TextAreaNavigationResolver navigation(m_builder.m_arena, m_builder.m_text, m_builder.m_context, state, m_builder.m_style.fontSize, m_builder.m_textScriptTag, StringView(m_builder.m_textLanguage.data(), m_builder.m_textLanguage.size()));
        __hidden_builder_text_area::TextAreaActions actions(model, state, m_builder.m_context);
        result = m_builder.m_editHost->editNavigated(**widget, editOptions, m_builder.m_context.popupToken(), model, state.m_navigation, navigation, actions);
    }
    if(!result.valid || m_builder.m_context.failed() || state.revision() != stateRevision || model.instanceGeneration() != modelGeneration){
        result.valid = false;
        m_builder.m_context.fail();
        return result;
    }
    const Point requestedScroll = state.scroll();
    b8 firstBinding = false;
    ReconcileTextAreaVisualScrollState(
        modelGeneration,
        model.revision(),
        model.selectionGeneration(),
        model.compositionGeneration(),
        model.anchor(),
        model.caret(),
        result.focused,
        m_builder.m_context.input().capture(),
        (*widget)->id,
        state.m_compositionGeneration,
        state.m_visual.modelGeneration,
        state.m_visual.revision,
        state.m_visual.selectionGeneration,
        state.m_visual.anchor,
        state.m_visual.caret,
        state.m_visual.focused,
        state.m_visual.scroll,
        state.m_scrollY,
        state.m_revealCaret,
        state.m_visual.caretElapsed,
        firstBinding
    );
    TextAreaFrame frame;
    frame.wheelLines = options.wheelLines;
    frame.model = &model;
    frame.state = &state;
    frame.focused = result.focused;
    snapshotTextArea(frame);
    Item item(m_builder.m_arena);
    item.style = m_builder.m_style;
    item.editStyle = m_builder.m_editStyle;
    item.scrollbarStyle = m_builder.m_scrollbarStyle;
    item.state = **widget;
    item.editOptions = editOptions;
    if(!m_builder.prepareEditBox(result, model, state.m_visual, item) || !m_builder.textAreaMatches(frame)){
        result.valid = false;
        m_builder.m_context.fail();
        return result;
    }
    if(firstBinding && !state.m_revealCaret){
        state.m_visual.scroll = requestedScroll.x;
        state.m_scrollY = requestedScroll.y;
    }
    state.m_compositionGeneration = model.compositionGeneration();
    item.textArea = static_cast<u32>(m_builder.m_scope->m_textAreas.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ResolveEditIntrinsicSize(m_builder.loadEditMinimumSize(), item.editView.visualBounds().height, item.padding);
    if(!m_builder.addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    frame.item = static_cast<u32>(m_builder.m_scope->m_items.size());
    m_builder.m_scope->m_items.push_back(Move(item));
    m_builder.m_scope->m_textAreas.push_back(frame);
    return result;
}

b8 TextAreaCoordinator::textAreaAvailable(const EditModel& model, const TextAreaState& state)const noexcept{
    for(usize index = 0u; index <= m_builder.m_popupFrameCount; ++index){
        const BuilderScopeFrame& scope = index == 0u ? m_builder.m_scopeFrame : *m_builder.m_popupFrames[index - 1u];
        for(const auto& frame : scope.m_textAreas){
            if(frame.model == &model || frame.state == &state)
                return false;
        }
    }
    return true;
}

void TextAreaCoordinator::snapshotTextArea(TextAreaFrame& frame)noexcept{
    frame.stateRevision = frame.state->revision();
    frame.navigation = frame.state->navigation().snapshot();
    frame.modelStamp = frame.model->stamp();
}

EditBoxResult Builder::textArea(const AStringView stableKey, EditModel& model, TextAreaState& state, const TextAreaOptions& options){
    return TextAreaCoordinator(*this).declareTextArea(stableKey, model, state, options);
}

b8 Builder::textAreaMatches(const TextAreaFrame& frame)const noexcept{
    return
        popupAncestorsVisible() && frame.model && frame.state
        && frame.state->revision() == frame.stateRevision && frame.state->navigation().matches(frame.navigation)
        && frame.model->matchesStamp(frame.modelStamp)
        && frame.state->m_visual.matchesStamp(frame.modelStamp, frame.focused)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

