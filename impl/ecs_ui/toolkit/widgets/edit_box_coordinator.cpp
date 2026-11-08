// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/edit_box_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/control_style_metrics.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>
#include <impl/ecs_ui/toolkit/edit/types.adhd.h>

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxResult EditBoxCoordinator::declareEditBox(const AStringView stableKey, const EditBoxOptions& options, EditModel& model, EditBoxState& state, IEditActionSink* actions, IntegerEditFrame* integerFrame, FloatEditFrame* floatFrame){
    if(m_builder.itemDeclarationBlocked() || model.textMode() != EditTextMode::SingleLine){
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
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::EditBox);
    if(!widget)
        return {};
    if((integerFrame || floatFrame) && (!m_builder.numericStateAvailable(state) || !m_builder.m_context.claimState(**widget, model.instanceGeneration()))){
        m_builder.m_context.fail();
        return {};
    }
    if(!options.enabled)
        m_builder.m_context.input().invalidateTarget((*widget)->id);
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_builder.m_context.input().focus() == (*widget)->id;
    if(m_builder.m_editHost){
        result = actions ? m_builder.m_editHost->editActions(**widget, options, m_builder.m_context.popupToken(), model, *actions) : m_builder.m_editHost->edit(**widget, options, model);
    }
    if(!result.valid || m_builder.m_context.failed()){
        result.valid = false;
        m_builder.m_context.fail();
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
        m_builder.m_context.fail();
        result.valid = false;
        return result;
    }
    Item item(m_builder.m_arena);
    item.style = m_builder.m_style;
    item.editStyle = m_builder.m_editStyle;
    item.state = **widget;
    item.editOptions = options;
    if(!prepareEditBox(result, model, state, item)){
        result.valid = false;
        return result;
    }
    if((integerFrame && !m_builder.numericEditFrameMatches(*integerFrame)) || (floatFrame && !m_builder.numericEditFrameMatches(*floatFrame))){
        m_builder.m_context.fail();
        result.valid = false;
        return result;
    }
    if(integerFrame)
        item.integerEdit = static_cast<u32>(m_builder.m_scope->m_integerEdits.size());
    if(floatFrame)
        item.floatEdit = static_cast<u32>(m_builder.m_scope->m_floatEdits.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = ResolveEditIntrinsicSize(m_builder.loadEditMinimumSize(), item.editView.visualBounds().height, item.padding);
    if(!m_builder.addItemNode(description, item)){
        result.valid = false;
        return result;
    }
    m_builder.m_scope->m_items.push_back(Move(item));
    return result;
}

b8 EditBoxCoordinator::prepareEditBox(const EditBoxResult& result, EditModel& model, EditBoxState& state, Item& item){
    state.updateFromModel(model.stamp(), result.focused, m_builder.m_deltaSeconds);

    item.editFlags = { item.editOptions.enabled, m_builder.m_context.input().hover() == item.state.id, result.focused, item.editOptions.readOnly, state.caretElapsed < s_CaretVisiblePhaseEnd };
    item.editFlags.preeditCaretVisible = result.preeditCaretVisible;
    if(!item.editView.snapshot(model) || item.editView.shape(m_builder.m_text, m_builder.textShapeRequest({}, m_builder.m_style.fontSize)) != TextLayoutStatus::Success || !item.editView.measure(m_builder.m_text, m_builder.m_paint.displayMetrics())){
        m_builder.m_context.fail();
        return false;
    }
    item.editState = &state;
    item.padding = m_builder.m_skin ? ControlStyleMetrics().editPadding(*m_builder.m_skin, item.editStyle) : item.editStyle.padding;
    return true;
}

b8 EditBoxCoordinator::paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation){
    if(!m_builder.numericEditMatches(item))
        return false;
    const f32 caretWidth = 1.0f / m_builder.m_paint.displayMetrics().pixelScaleX;
    const auto arranged = item.editView.arrange(box.rectangle, item.padding, m_builder.visibleClip(box.clip), { item.editState->scroll, 0.0f }, caretWidth);
    if(!arranged)
        return false;
    const EditBoxPlacement& placement = *arranged;
    if(!item.editView.paint(m_builder.m_text, m_builder.m_paint, *m_builder.m_skin, placement, item.editStyle, item.editFlags))
        return false;
    item.editState->placement = placement;
    item.editState->scroll = placement.scroll;
    const HitTarget target = HitTargetFactory().makeEditBoxHitTarget(
        navigation ? *navigation : HitTarget{},
        box.rectangle,
        m_builder.visibleClip(box.clip),
        item.editOptions.enabled,
        item.contextMenu
    );
    if(!m_builder.m_context.addTarget(item.state, target))
        return false;
    const b8 published = !m_builder.m_editHost || m_builder.m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    return published && m_builder.numericEditMatches(item);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

