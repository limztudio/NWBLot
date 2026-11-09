// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>
#include <global/simd/vector_arithmetic.h>
#include <impl/ecs_ui/toolkit/widgets/control_style_metrics.adhd.h>

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
    if(!m_skin || !ControlStyleMetrics().radioGroupMetrics(*m_skin, frame.m_choices.count, maximumLabel, frame.m_options, frame.m_style, frame.m_metrics))
        return false;
    return radioGroupMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintRadioGroup(const Item& item, const LayoutBox& box){
    if(item.radioGroup >= m_scope->m_radioGroups.size())
        return false;
    RadioGroupFrame& frame = m_scope->m_radioGroups[item.radioGroup];
    if(!radioGroupMatches(frame) || frame.m_labels.size() != frame.m_choices.count)
        return false;
    const auto placement = PlaceRadioGroup(box.rectangle, visibleClip(box.clip), frame.m_choices, frame.m_metrics);
    if(!placement)
        return false;
    const HitTarget host = HitTargetFactory().makeRadioGroupHostHitTarget(*placement, item.enabled, item.contextMenu, frame.m_token, frame.m_focusOnCommit);
    if(!m_context.addTarget(item.state, host))
        return false;
    const auto choices = TryMakeWidgetId(item.state.id, s_WidgetPartChoices);
    if(!choices)
        return false;
    const UiSkinRegion* mark = placement->count != 0u ? region(frame.m_style.mark, frame.m_style.markFallback) : nullptr;
    for(u32 index = 0u; index < placement->count; ++index){
        const RadioGroupChoicePlacement& row = placement->rows[index];
        const auto part = TryMakeWidgetPartId(*choices, row.key);
        if(!part)
            return false;
        const InputRouter& input = m_context.input();
        const b8 enabled = frame.m_options.enabled && row.enabled;
        const b8 checked = frame.m_snapshot.selectedKey == row.key;
        const b8 hover = input.hover() == *part;
        const b8 pressed = input.pressed(*part) || (frame.m_snapshot.cursorKey == row.key && input.pressed(item.state.id));
        const b8 focused = enabled && input.focus() == item.state.id && frame.m_snapshot.cursorKey == row.key;
        if(!mark || !ControlVisualPainter().paintRadioGroupChoiceRow(
            m_paint,
            m_text,
            *m_skin,
            frame.m_style,
            frame.m_widgetStyle,
            frame.m_metrics,
            row,
            frame.m_labels[index].text,
            frame.m_labels[index].bounds,
            mark->name,
            enabled,
            checked,
            pressed,
            hover,
            focused
        ))
            return false;
        if(item.enabled){
            const HitTarget target = HitTargetFactory().makeRadioGroupChoiceHitTarget(row, enabled, frame.m_token);
            if(!m_context.addPartTarget(item.state, *part, target))
                return false;
        }
    }
    if(!radioGroupMatches(frame))
        return false;
    // Diagnostics describe prepared placement; input still uses the last published owned hit targets.
    frame.m_state.m_placement = *placement;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

