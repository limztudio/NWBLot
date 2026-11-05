// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

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
    explicit ReconcileGuard(const Context& context)noexcept
        : m_context(context)
    {}
    virtual ~ReconcileGuard()noexcept override = default;


public:
    [[nodiscard]] b8 add(const PopupState& state, const PopupToken& token)noexcept{
        if(m_count == s_InputMaxPopups)
            return false;
        m_popups[m_count].emplace(PopupLoan{ state, token });
        ++m_count;
        return true;
    }

    [[nodiscard]] virtual b8 current()const noexcept override{
        if(m_context.failed())
            return false;
        for(usize index = 0u; index < m_count; ++index){
            const PopupLoan& loan = *m_popups[index];
            if(!loan.state.isOpen() || loan.state.instanceGeneration() != loan.token.instanceGeneration || loan.state.openGeneration() != loan.token.openGeneration)
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


RadioGroupResult Builder::radioGroup(const AStringView stableKey, const IListDataSource& source, RadioGroupState& state, const RadioGroupOptions& options){
    RadioGroupResult result;
    if(itemDeclarationBlocked() || !m_skin){
        m_context.fail();
        return result;
    }
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::RadioGroup);
    if(!widget || !m_context.claimState(**widget, state.instanceGeneration()))
        return result;
    RadioGroupFrame frame(m_arena, source, state);
    frame.m_options = options;
    frame.m_style = m_radioGroupStyle;
    frame.m_widgetStyle = m_style;
    // Reject options and style before invoking application callbacks or changing reconciliation state.
    const auto metrics = MeasureRadioGroup(0u, {}, options, frame.m_style);
    if(!metrics){
        m_context.fail();
        return result;
    }
    frame.m_metrics = *metrics;
    const b8 previouslyFocused = m_context.input().focus() == (*widget)->id;
    __hidden_builder_radio_group::ReconcileGuard guard(m_context);
    for(const BuilderScopeFrame* scope = m_scope.get(); scope; scope = scope->m_parent){
        if(scope->m_popupState && !guard.add(*scope->m_popupState, scope->m_popupToken)){
            m_context.fail();
            return result;
        }
    }
    const auto choices = RadioGroupBehavior::Reconcile(source, &guard, state, result);
    const b8 reconciled = choices.has_value();
    if(choices)
        frame.m_choices = *choices;
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
    frame.m_snapshot = state.snapshot();
    frame.m_token = { state.inputGeneration(), frame.m_choices.sourceGeneration, frame.m_choices.sourceRevision };
    m_context.input().fenceControl((*widget)->id, (*widget)->declarationGeneration, frame.m_token);
    const b8 interactive = IsRadioGroupInteractive(frame.m_choices, options);
    if(!interactive)
        m_context.input().invalidateTarget((*widget)->id);
    while(auto action = m_context.takeControlAction(**widget, interactive, frame.m_token)){
        if(!RadioGroupBehavior::Apply(frame.m_choices, options, *action, state, result)){
            result.valid = false;
            m_context.fail();
            return result;
        }
    }
    frame.m_snapshot = state.snapshot();
    result.focused = interactive && m_context.input().focus() == (*widget)->id;
    frame.m_focusOnCommit = interactive && previouslyFocused && !result.focused;
    if(!prepareRadioGroup(frame)){
        result.valid = false;
        m_context.fail();
        return result;
    }
    Item item(m_arena);
    item.state = **widget;
    item.enabled = interactive;
    item.radioGroup = static_cast<u32>(m_scope->m_radioGroups.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.intrinsicSize = frame.m_metrics.contentSize;
    if(!addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    m_scope->m_radioGroups.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    result.valid = true;
    return result;
}

b8 Builder::radioGroupMatches(const RadioGroupFrame& frame)const{
    // Repeat the stamp fence to catch a one-shot metadata mutation from the final count callback.
    for(u32 pass = 0u; pass < s_RadioGroupStampFencePassCount; ++pass){
        if(
            !radioGroupStateMatches(frame)
            || frame.m_source.instanceGeneration() != frame.m_choices.sourceGeneration || !radioGroupStateMatches(frame)
            || frame.m_source.revision() != frame.m_choices.sourceRevision || !radioGroupStateMatches(frame)
            || frame.m_source.rowCount() != frame.m_choices.count || !radioGroupStateMatches(frame)
        )
            return false;
    }
    return true;
}

b8 Builder::prepareRadioGroup(RadioGroupFrame& frame){
    if(!radioGroupMatches(frame))
        return false;
    Point maximumLabel{};
    frame.m_labels.reserve(frame.m_choices.count);
    for(u32 index = 0u; index < frame.m_choices.count; ++index){
        if(!radioGroupStateMatches(frame))
            return false;
        const StringView text = frame.m_source.text(index);
        // Do not call any further source method until the temporary label has been shaped and copied.
        if(!radioGroupStateMatches(frame))
            return false;
        auto label = m_text.layout(textShapeRequest(text, frame.m_widgetStyle.fontSize));
        if(!label || !radioGroupMatches(frame))
            return false;
        const auto textBounds = m_text.measure(*label, m_paint.displayMetrics());
        if(!textBounds)
            return false;
        maximumLabel.x = Max(maximumLabel.x, textBounds->width);
        maximumLabel.y = Max(maximumLabel.y, textBounds->height);
        frame.m_labels.push_back({ Move(*label), *textBounds });
    }
    const UiSkinRegion* normal = region(frame.m_style.normal, frame.m_style.fallback);
    const UiSkinRegion* checked = region(frame.m_style.checked, frame.m_style.checkedFallback);
    const UiSkinRegion* mark = region(frame.m_style.mark, frame.m_style.markFallback);
    if(!normal || !checked || !mark || !IsFinite(m_skin->referenceDensity()) || m_skin->referenceDensity() <= 0.0f)
        return false;
    const Array<const UiSkinRegion*, 5u> states = { normal, checked, m_skin->findRegion(frame.m_style.hover), m_skin->findRegion(frame.m_style.pressed), m_skin->findRegion(frame.m_style.disabled) };
    const f32 referenceDensity = m_skin->referenceDensity();
    f32 indicatorExtent = frame.m_style.indicatorExtent;
    for(const UiSkinRegion* skinRegion : states){
        if(!skinRegion)
            continue;
        const auto minimum = TryResolveSkinRegionMinimumSize(*skinRegion, referenceDensity);
        if(!minimum)
            return false;
        indicatorExtent = Max(indicatorExtent, Max(minimum->x, minimum->y));
    }
    frame.m_style.indicatorExtent = indicatorExtent;
    const auto metrics = MeasureRadioGroup(frame.m_choices.count, maximumLabel, frame.m_options, frame.m_style);
    if(!metrics)
        return false;
    frame.m_metrics = *metrics;
    return radioGroupMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

