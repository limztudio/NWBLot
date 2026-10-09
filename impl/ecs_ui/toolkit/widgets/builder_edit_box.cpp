// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "../edit/caret_clock.h"

#include <global/math/vector_arithmetic.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxResult Builder::editBox(
    const AStringView stableKey, EditModel& model, EditBoxState& state, const EditBoxOptions& options
){
    return declareEditBox(stableKey, model, state, options);
}

EditBoxResult Builder::declareEditBox(const AStringView stableKey, EditModel& model, EditBoxState& state,
    const EditBoxOptions& options, IEditActionSink* actions, IntegerEditFrame* integerFrame, FloatEditFrame* floatFrame
){
    if(
        declarationBlocked() || !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed)
        || model.textMode() != EditTextMode::SingleLine || m_context.failed() || m_scope->m_items.size() >= s_LayoutMaxNodes
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
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::EditBox);
    if(!widget)
        return {};
    if((integerFrame || floatFrame) && (!numericStateAvailable(state) || !m_context.claimState(*widget, model.instanceGeneration()))){
        m_context.fail();
        return {};
    }
    if(!options.enabled)
        m_context.input().invalidateTarget(widget->id);
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_context.input().focus() == widget->id;
    if(m_editHost){
        result = actions ? m_editHost->editActions(*widget, model, options, m_context.popupToken(), *actions)
            : m_editHost->edit(*widget, model, options);
    }
    if(!result.valid || m_context.failed()){
        result.valid = false;
        m_context.fail();
        return result;
    }
    if(integerFrame){
        if(!integerFrame->model || integerFrame->model->revision() != integerFrame->revision){
            m_context.fail();
            result.valid = false;
            return result;
        }
        snapshotNumericEdit(*integerFrame, model);
        integerFrame->focused = result.focused;
    }
    if(floatFrame){
        if(!floatFrame->model || floatFrame->model->revision() != floatFrame->revision){
            m_context.fail();
            result.valid = false;
            return result;
        }
        snapshotNumericEdit(*floatFrame, model);
        floatFrame->focused = result.focused;
    }
    Item item(m_arena);
    item.style = m_style;
    item.editStyle = m_editStyle;
    item.state = *widget;
    item.editOptions = options;
    if(!prepareEditBox(item, model, state, result)){
        result.valid = false;
        return result;
    }
    if((integerFrame && !integerEditMatches(*integerFrame)) || (floatFrame && !floatEditMatches(*floatFrame))){
        m_context.fail();
        result.valid = false;
        return result;
    }
    if(integerFrame)
        item.integerEdit = static_cast<u32>(m_scope->m_integerEdits.size());
    if(floatFrame)
        item.floatEdit = static_cast<u32>(m_scope->m_floatEdits.size());
    Point minimum{ 120.0f, 0.0f };
    const Name names[]{ m_editStyle.normal, m_editStyle.hover, m_editStyle.focused, m_editStyle.disabled };
    for(const auto& name : names){
        const UiSkinRegion* skinRegion = region(name, m_editStyle.fallback);
        if(skinRegion){
            const SIMDVector minimumCurrent = VectorSet(minimum.x, minimum.y, minimum.x, minimum.y);
            const SIMDVector minimumCandidate = VectorSet(skinRegion->minimumWidth, skinRegion->minimumHeight,
                skinRegion->minimumWidth, skinRegion->minimumHeight);
            const SIMDVector minimumMaximum = VectorSelect(minimumCandidate, minimumCurrent,
                VectorGreater(minimumCurrent, minimumCandidate));
            minimum = { VectorGetX(minimumMaximum), VectorGetY(minimumMaximum) };
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
    m_scope->m_items.push_back(Move(item));
    return result;
}

bool Builder::prepareEditBox(Item& item, EditModel& model, EditBoxState& state, const EditBoxResult& result){
    if(
        state.modelGeneration != model.instanceGeneration() || state.revision != model.revision()
        || state.selectionGeneration != model.selectionGeneration()
        || state.anchor != model.anchor() || state.caret != model.caret() || state.focused != result.focused
    )
        state.caretElapsed = 0.0;
    else
        state.caretElapsed = AdvanceCaretPhase(state.caretElapsed, static_cast<f64>(m_deltaSeconds));
    if(state.modelGeneration != model.instanceGeneration())
        state.scroll = 0.0f;
    state.modelGeneration = model.instanceGeneration();
    state.revision = model.revision();
    state.selectionGeneration = model.selectionGeneration();
    state.anchor = model.anchor();
    state.caret = model.caret();
    state.focused = result.focused;

    item.editFlags = { item.editOptions.enabled, m_context.input().hover() == item.state.id, result.focused, item.editOptions.readOnly,
        state.caretElapsed < 0.5 };
    item.editFlags.preeditCaretVisible = result.preeditCaretVisible;
    if(!item.editView.snapshot(model)
        || item.editView.shape(m_text, textShapeRequest({}, m_style.fontSize)) != TextLayoutStatus::Success){
        m_context.fail();
        return false;
    }
    item.editState = &state;
    item.padding = item.editStyle.padding;

    const Name names[]{ m_editStyle.normal, m_editStyle.hover, m_editStyle.focused, m_editStyle.disabled };
    for(const auto& name : names){
        const UiSkinRegion* skinRegion = region(name, m_editStyle.normal);
        if(!skinRegion)
            skinRegion = region(m_editStyle.fallback, m_editStyle.fallback);
        if(!skinRegion)
            continue;
        const SIMDVector itempaddingCurrent = VectorSet(item.padding.left, item.padding.top, item.padding.right, item.padding.bottom);
        const SIMDVector itempaddingCandidate = VectorSet(skinRegion->padding.left, skinRegion->padding.top, skinRegion->padding.right, skinRegion->padding.bottom);
        const SIMDVector itempaddingMaximum = VectorSelect(itempaddingCandidate, itempaddingCurrent,
            VectorGreater(itempaddingCurrent, itempaddingCandidate));
        item.padding = { VectorGetX(itempaddingMaximum), VectorGetY(itempaddingMaximum), VectorGetZ(itempaddingMaximum), VectorGetW(itempaddingMaximum) };
    }
    return true;
}

bool Builder::paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation){
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
    HitTarget target = navigation ? *navigation : HitTarget{};
    target.rectangle = box.rectangle;
    target.clip = visibleClip(box.clip);
    target.enabled = item.editOptions.enabled;
    target.focusable = item.editOptions.enabled;
    target.textEditable = item.editOptions.enabled;
    target.contextMenu = item.contextMenu;
    if(!m_context.addTarget(item.state, target))
        return false;
    const bool published = !m_editHost || m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    return published && numericEditMatches(item);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

