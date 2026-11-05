// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/edit/types.adhd.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxResult Builder::declareEditBox(const AStringView stableKey, const EditBoxOptions& options, EditModel& model, EditBoxState& state, IEditActionSink* actions, IntegerEditFrame* integerFrame, FloatEditFrame* floatFrame){
    if(itemDeclarationBlocked() || model.textMode() != EditTextMode::SingleLine){
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
    const auto widget = m_context.declare(stableKey, WidgetKind::EditBox);
    if(!widget)
        return {};
    if((integerFrame || floatFrame) && (!numericStateAvailable(state) || !m_context.claimState(**widget, model.instanceGeneration()))){
        m_context.fail();
        return {};
    }
    if(!options.enabled)
        m_context.input().invalidateTarget((*widget)->id);
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_context.input().focus() == (*widget)->id;
    if(m_editHost){
        result = actions ? m_editHost->editActions(**widget, options, m_context.popupToken(), model, *actions) : m_editHost->edit(**widget, options, model);
    }
    if(!result.valid || m_context.failed()){
        result.valid = false;
        m_context.fail();
        return result;
    }
    const auto snapshotNumericFrame = [&](auto* const frame)noexcept{
        if(!frame)
            return true;
        if(!frame->model || frame->model->revision() != frame->revision)
            return false;
        frame->draftStamp = model.stamp();
        frame->focused = result.focused;
        return true;
    };
    if(!snapshotNumericFrame(integerFrame) || !snapshotNumericFrame(floatFrame)){
        m_context.fail();
        result.valid = false;
        return result;
    }
    Item item(m_arena);
    item.style = m_style;
    item.editStyle = m_editStyle;
    item.state = **widget;
    item.editOptions = options;
    if(!prepareEditBox(result, model, state, item)){
        result.valid = false;
        return result;
    }
    if((integerFrame && !numericEditFrameMatches(*integerFrame)) || (floatFrame && !numericEditFrameMatches(*floatFrame))){
        m_context.fail();
        result.valid = false;
        return result;
    }
    if(integerFrame)
        item.integerEdit = static_cast<u32>(m_scope->m_integerEdits.size());
    if(floatFrame)
        item.floatEdit = static_cast<u32>(m_scope->m_floatEdits.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ResolveEditIntrinsicSize(loadEditMinimumSize(), item.editView.visualBounds().height, item.padding);
    if(!addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    m_scope->m_items.push_back(Move(item));
    return result;
}

Point Builder::loadEditMinimumSize()const noexcept{
    if(!m_skin)
        return Point{ s_StyleMinimumFieldWidth, 0.0f };
    return LoadEditStyleMinimumSize(*m_skin, m_editStyle);
}

b8 Builder::prepareEditBox(const EditBoxResult& result, EditModel& model, EditBoxState& state, Item& item){
    state.updateFromModel(model.stamp(), result.focused, m_deltaSeconds);

    item.editFlags = { item.editOptions.enabled, m_context.input().hover() == item.state.id, result.focused, item.editOptions.readOnly, state.caretElapsed < s_CaretVisiblePhaseEnd };
    item.editFlags.preeditCaretVisible = result.preeditCaretVisible;
    if(!item.editView.snapshot(model) || item.editView.shape(m_text, textShapeRequest({}, m_style.fontSize)) != TextLayoutStatus::Success || !item.editView.measure(m_text, m_paint.displayMetrics())){
        m_context.fail();
        return false;
    }
    item.editState = &state;
    item.padding = m_skin ? ResolveEditStylePadding(*m_skin, item.editStyle) : item.editStyle.padding;
    return true;
}

b8 Builder::paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation){
    if(!numericEditMatches(item))
        return false;
    const f32 caretWidth = 1.0f / m_paint.displayMetrics().pixelScaleX;
    const auto arranged = item.editView.arrange(box.rectangle, item.padding, visibleClip(box.clip), { item.editState->scroll, 0.0f }, caretWidth);
    if(!arranged)
        return false;
    const EditBoxPlacement& placement = *arranged;
    if(!item.editView.paint(m_text, m_paint, *m_skin, placement, item.editStyle, item.editFlags))
        return false;
    item.editState->placement = placement;
    item.editState->scroll = placement.scroll;
    const HitTarget target = MakeEditBoxHitTarget(
        navigation ? *navigation : HitTarget{},
        box.rectangle,
        visibleClip(box.clip),
        item.editOptions.enabled,
        item.contextMenu
    );
    if(!m_context.addTarget(item.state, target))
        return false;
    const b8 published = !m_editHost || m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    return published && numericEditMatches(item);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

