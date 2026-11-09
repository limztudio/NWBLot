// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/text_area_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_visual_painter.adhd.h>
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


b8 TextAreaCoordinator::paintTextArea(const Item& item, const LayoutBox& box){
    if(item.textArea >= m_builder.m_scope->m_textAreas.size())
        return false;
    const TextAreaFrame& frame = m_builder.m_scope->m_textAreas[item.textArea];
    if(!m_builder.textAreaMatches(frame))
        return false;
    TextAreaState& state = *frame.state;
    const Rect clip = m_builder.visibleClip(box.clip);
    const f32 caretWidth = 1.0f / m_builder.m_paint.displayMetrics().pixelScaleX;
    const Rect& visualBounds = item.editView.visualBounds();
    const auto viewport = CalculateScrollViewportPlacement(box.rectangle, clip, item.padding, { visualBounds.width, visualBounds.height }, caretWidth, state.scroll(), item.scrollbarStyle.thickness, item.scrollbarStyle.minimumThumb);
    if(!viewport)
        return false;
    const f32 lineHeight = item.editView.layout().lines().empty() ? item.editView.layout().fontSize() : item.editView.layout().lines().front().height;
    const auto stepResult = TryResolveTextAreaScrollStep(item.editView.layout().fontSize(), lineHeight, frame.wheelLines);
    if(!stepResult)
        return false;
    const Point step = *stepResult;
    const ControlToken token = state.m_scrollInput.prepare(item.state, m_builder.m_context.popupToken(), state.instanceGeneration(), state.revision(), *frame.model, item.editOptions.enabled, item.editOptions.readOnly, *viewport, step);
    m_builder.m_context.input().fenceControl(item.state.id, item.state.declarationGeneration, token);
    if(!applyTextAreaScrollInput(item, frame, token))
        return false;
    const auto arranged = item.editView.arrangeViewport(box.rectangle, viewport->viewport, clip, state.scroll(), caretWidth, state.m_revealCaret);
    if(!arranged)
        return false;
    const EditBoxPlacement& placement = *arranged;
    if(!TryUpdateScrollViewportOffsets({ placement.scroll, placement.scrollY }, state.m_scrollInput.m_placement))
        return false;
    if(!item.editView.paint(m_builder.m_text, m_builder.m_paint, *m_builder.m_skin, placement, item.editStyle, item.editFlags) || !m_builder.textAreaMatches(frame))
        return false;
    state.m_visual.placement = placement;
    state.m_visual.scroll = placement.scroll;
    state.m_scrollY = placement.scrollY;
    const HitTarget target = HitTargetFactory().makeTextAreaHitTarget(
        box.rectangle,
        m_builder.visibleClip(box.clip),
        item.editOptions.enabled,
        item.contextMenu,
        token,
        step,
        state.scrollbars()
    );
    if(!m_builder.m_context.addTarget(item.state, target) || !paintTextAreaScrollbars(item, state.scrollbars(), clip, token))
        return false;
    const b8 published = !m_builder.m_editHost || m_builder.m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    if(!published || !m_builder.textAreaMatches(frame))
        return false;
    return true;
}

b8 TextAreaCoordinator::applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token){
    TextAreaState& state = *frame.state;
    const b8 enabled = item.editOptions.enabled;
    const auto horizontalId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollHorizontalThumb);
    if(!horizontalId)
        return false;
    const auto verticalId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollVerticalThumb);
    if(!verticalId)
        return false;
    auto horizontal = m_builder.m_context.takePartPointerGesture(item.state, *horizontalId, enabled);
    b8 haveHorizontal = horizontal && horizontal->control == token;
    auto vertical = m_builder.m_context.takePartPointerGesture(item.state, *verticalId, enabled);
    b8 haveVertical = vertical && vertical->control == token;
    auto action = m_builder.m_context.takeControlAction(item.state, enabled, token);
    b8 haveAction = action.has_value();
    Point scroll = state.scroll();
    b8 manual = false;
    while(haveHorizontal || haveVertical || haveAction){
        const u64 horizontalSequence = haveHorizontal ? horizontal->updateSequence : Limit<u64>::s_Max;
        const u64 verticalSequence = haveVertical ? vertical->updateSequence : Limit<u64>::s_Max;
        const u64 actionSequence = haveAction ? action->id.sequence : Limit<u64>::s_Max;
        if(haveHorizontal && horizontalSequence <= verticalSequence && horizontalSequence <= actionSequence){
            const auto dragged = TryDragScrollbarOffset(*horizontal, true);
            if(!dragged)
                return false;
            scroll.x = *dragged;
            manual = true;
            horizontal = m_builder.m_context.takePartPointerGesture(item.state, *horizontalId, enabled);
            haveHorizontal = horizontal && horizontal->control == token;
        }
        else if(haveVertical && verticalSequence <= actionSequence){
            const auto dragged = TryDragScrollbarOffset(*vertical, false);
            if(!dragged)
                return false;
            scroll.y = *dragged;
            manual = true;
            vertical = m_builder.m_context.takePartPointerGesture(item.state, *verticalId, enabled);
            haveVertical = vertical && vertical->control == token;
        }
        else{
            if(!TryApplyTextAreaScrollControlAction(*action, state.scrollbars(), scroll, manual))
                return false;
            action = m_builder.m_context.takeControlAction(item.state, enabled, token);
            haveAction = action.has_value();
        }
    }
    if(manual){
        state.m_visual.scroll = scroll.x;
        state.m_scrollY = scroll.y;
        state.m_revealCaret = false;
    }
    return true;
}

b8 TextAreaCoordinator::paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement, const Rect& clip, const ControlToken& token){
    const b8 enabled = item.editOptions.enabled;
    b8 horizontalHovered = false;
    b8 horizontalPressed = false;
    if(placement.horizontal.visible){
        const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollHorizontalThumb);
        if(!thumbId)
            return false;
        horizontalHovered = m_builder.m_context.input().hover() == *thumbId;
        horizontalPressed = m_builder.m_context.input().capture() == *thumbId;
    }
    b8 verticalHovered = false;
    b8 verticalPressed = false;
    if(placement.vertical.visible){
        const auto thumbId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollVerticalThumb);
        if(!thumbId)
            return false;
        verticalHovered = m_builder.m_context.input().hover() == *thumbId;
        verticalPressed = m_builder.m_context.input().capture() == *thumbId;
    }
    if(!ControlVisualPainter().paintTextAreaScrollbarsVisual(
        m_builder.m_paint,
        *m_builder.m_skin,
        item.scrollbarStyle,
        placement,
        clip,
        enabled,
        horizontalHovered,
        horizontalPressed,
        verticalHovered,
        verticalPressed
    ))
        return false;
    if(!enabled)
        return true;
    for(u32 axis = 0u; axis < s_ScrollAxisCount; ++axis){
        const b8 horizontal = axis == 0u;
        const ScrollbarPlacement& bar = horizontal ? placement.horizontal : placement.vertical;
        if(!bar.visible)
            continue;
        Rect beforeRect;
        Rect afterRect;
        ResolveScrollbarBeforeAfterRects(bar, horizontal, beforeRect, afterRect);
        const HitTarget beforeTarget = HitTargetFactory().makeScrollbarTrackActionHitTarget(
            beforeRect,
            clip,
            token,
            horizontal ? s_TextAreaScrollHorizontalBefore : s_TextAreaScrollVerticalBefore
        );
        const auto beforeId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalBefore : s_WidgetPartScrollVerticalBefore);
        if(!beforeId || !m_builder.m_context.addPartTarget(item.state, *beforeId, beforeTarget))
            return false;
        const HitTarget afterTarget = HitTargetFactory().makeScrollbarTrackActionHitTarget(
            afterRect,
            clip,
            token,
            horizontal ? s_TextAreaScrollHorizontalAfter : s_TextAreaScrollVerticalAfter
        );
        const auto afterId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalAfter : s_WidgetPartScrollVerticalAfter);
        if(!afterId || !m_builder.m_context.addPartTarget(item.state, *afterId, afterTarget))
            return false;
        const HitTarget thumbTarget = HitTargetFactory().makeScrollbarThumbHitTarget(bar, clip, token, horizontal);
        const auto thumbId = TryMakeWidgetId(item.state.id, horizontal ? s_WidgetPartScrollHorizontalThumb : s_WidgetPartScrollVerticalThumb);
        if(!thumbId || !m_builder.m_context.addPartTarget(item.state, *thumbId, thumbTarget))
            return false;
    }
    if(placement.corner.width > 0.0f && placement.corner.height > 0.0f){
        const HitTarget target = HitTargetFactory().makeScrollbarCornerHitTarget(placement.corner, clip, token);
        const auto cornerId = TryMakeWidgetId(item.state.id, s_WidgetPartScrollCorner);
        if(!cornerId || !m_builder.m_context.addPartTarget(item.state, *cornerId, target))
            return false;
    }
    return true;
}

b8 Builder::paintTextArea(const Item& item, const LayoutBox& box){
    return TextAreaCoordinator(*this).paintTextArea(item, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

