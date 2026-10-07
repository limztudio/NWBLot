// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_builder_radio_group{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReconcileGuard final : public IRadioGroupReconcileGuard{
private:
    struct PopupLoan{
        const PopupState& state;
        PopupToken token;
    };


public:
    explicit ReconcileGuard(const Context& context)
        : m_context(context)
    {}
    virtual ~ReconcileGuard()override = default;


public:
    [[nodiscard]] bool add(const PopupState& state, const PopupToken& token){
        if(m_count == s_InputMaxPopups)
            return false;
        m_popups[m_count].emplace(PopupLoan{ state, token });
        ++m_count;
        return true;
    }
    [[nodiscard]] virtual bool current()const noexcept override{
        if(m_context.failed())
            return false;
        for(usize index = 0u; index < m_count; ++index){
            const PopupLoan& loan = *m_popups[index];
            if(
                !loan.state.isOpen() || loan.state.instanceGeneration() != loan.token.instanceGeneration
                || loan.state.openGeneration() != loan.token.openGeneration
            )
                return false;
        }
        return true;
    }


private:
    const Context& m_context;
    Array<Optional<PopupLoan>, s_InputMaxPopups> m_popups{};
    usize m_count = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RadioGroupResult Builder::radioGroup(const AStringView stableKey, const IListDataSource& source,
    RadioGroupState& state, const RadioGroupOptions& options
){
    RadioGroupResult result;
    if(
        declarationBlocked() || !m_scope->m_panelActive || !m_skin || m_context.failed()
        || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_scope->m_items.size() >= s_LayoutMaxNodes
    ){
        m_context.fail();
        return result;
    }
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::RadioGroup);
    if(!widget || !m_context.claimState(*widget, state.instanceGeneration()))
        return result;
    auto frame = Core::MakeGlobalUnique<RadioGroupFrame>(m_arena, m_arena, source, state);
    frame->m_options = options;
    frame->m_style = m_radioGroupStyle;
    frame->m_widgetStyle = m_style;
    // Reject options and style before invoking application callbacks or changing reconciliation state.
    if(!RadioGroupLayout::Measure(0u, {}, options, frame->m_style, frame->m_metrics)){
        m_context.fail();
        return result;
    }
    const bool previouslyFocused = m_context.input().focus() == widget->id;
    __hidden_builder_radio_group::ReconcileGuard guard(m_context);
    for(const BuilderScopeFrame* scope = m_scope.get(); scope; scope = scope->m_parent){
        if(scope->m_popupState && !guard.add(*scope->m_popupState, scope->m_popupToken)){
            m_context.fail();
            return result;
        }
    }
    const bool reconciled = RadioGroupBehavior::Reconcile(state, source, frame->m_choices, result, &guard);
    if(m_context.failed()){
        result.valid = false;
        return result;
    }
    // Source callbacks can close or reopen an ancestor before reconciliation or copied input commits.
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    if(!reconciled){
        result.valid = false;
        m_context.fail();
        return result;
    }
    frame->m_snapshot = state.snapshot();
    frame->m_token = { state.inputGeneration(), frame->m_choices.sourceGeneration, frame->m_choices.sourceRevision };
    m_context.input().fenceControl(widget->id, widget->declarationGeneration, frame->m_token);
    bool interactive = false;
    for(u32 index = 0u; index < frame->m_choices.count; ++index)
        interactive = interactive || frame->m_choices.rows[index].enabled;
    interactive = interactive && options.enabled;
    if(!interactive)
        m_context.input().invalidateTarget(widget->id);
    ControlAction action;
    while(m_context.takeControlAction(*widget, interactive, frame->m_token, action)){
        if(!RadioGroupBehavior::Apply(state, frame->m_choices, options, action, result)){
            result.valid = false;
            m_context.fail();
            return result;
        }
    }
    frame->m_snapshot = state.snapshot();
    result.focused = interactive && m_context.input().focus() == widget->id;
    frame->m_focusOnCommit = interactive && previouslyFocused && !result.focused;
    if(!prepareRadioGroup(*frame)){
        result.valid = false;
        m_context.fail();
        return result;
    }
    Item item(m_arena);
    item.state = *widget;
    item.enabled = interactive;
    item.radioGroup = static_cast<u32>(m_scope->m_radioGroups.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.intrinsicSize = frame->m_metrics.contentSize;
    if(!m_scope->m_layout.addNode(m_scope->m_stack.back(), description, item.node)){
        result.valid = false;
        m_context.fail();
        return result;
    }
    m_scope->m_radioGroups.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    result.valid = true;
    return result;
}

bool Builder::prepareRadioGroup(RadioGroupFrame& frame){
    if(!radioGroupMatches(frame))
        return false;
    Point maximumLabel;
    frame.m_labels.reserve(frame.m_choices.count);
    for(u32 index = 0u; index < frame.m_choices.count; ++index){
        if(!radioGroupStateMatches(frame))
            return false;
        TextLayout label(m_arena);
        const StringView text = frame.m_source.text(index);
        // Do not call any further source method until the temporary label has been shaped and copied.
        if(!radioGroupStateMatches(frame)
            || m_text.layout(textShapeRequest(text, frame.m_widgetStyle.fontSize), label) != TextLayoutStatus::Success)
            return false;
        if(!radioGroupMatches(frame))
            return false;
        maximumLabel.x = Max(maximumLabel.x, label.measure().x);
        maximumLabel.y = Max(maximumLabel.y, label.measure().y);
        frame.m_labels.push_back(Move(label));
    }
    const UiSkinRegion* normal = region(frame.m_style.normal, frame.m_style.fallback);
    const UiSkinRegion* checked = region(frame.m_style.checked, frame.m_style.checkedFallback);
    const UiSkinRegion* mark = region(frame.m_style.mark, frame.m_style.markFallback);
    if(!normal || !checked || !mark || !IsFinite(m_skin->referenceDensity()) || m_skin->referenceDensity() <= 0.0f)
        return false;
    const UiSkinRegion* states[] = { normal, checked, m_skin->findRegion(frame.m_style.hover),
        m_skin->findRegion(frame.m_style.pressed), m_skin->findRegion(frame.m_style.disabled) };
    for(const UiSkinRegion* skinRegion : states){
        if(!skinRegion)
            continue;
        const f32 horizontal = static_cast<f32>(static_cast<u64>(skinRegion->sliceInsets.left) + skinRegion->sliceInsets.right)
            / m_skin->referenceDensity();
        const f32 vertical = static_cast<f32>(static_cast<u64>(skinRegion->sliceInsets.top) + skinRegion->sliceInsets.bottom)
            / m_skin->referenceDensity();
        frame.m_style.indicatorExtent = Max(frame.m_style.indicatorExtent,
            Max(Max(skinRegion->minimumWidth, skinRegion->minimumHeight), Max(horizontal, vertical))
        );
    }
    return
        RadioGroupLayout::Measure(frame.m_choices.count, maximumLabel, frame.m_options, frame.m_style, frame.m_metrics)
        && radioGroupMatches(frame)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

