// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxResult Builder::editBox(
    const AStringView stableKey, EditModel& model, EditBoxState& state, const EditBoxOptions& options){
    return declareEditBox(stableKey, model, state, options);
}

EditBoxResult Builder::declareEditBox(const AStringView stableKey, EditModel& model, EditBoxState& state,
    const EditBoxOptions& options, IEditActionSink* actions, IntegerEditFrame* integerFrame, FloatEditFrame* floatFrame){
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
            minimum.x = Max(minimum.x, skinRegion->minimumWidth);
            minimum.y = Max(minimum.y, skinRegion->minimumHeight);
        }
    }
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    description.intrinsicSize = { minimum.x, Max(minimum.y, item.editView.layout().measure().y + item.padding.top + item.padding.bottom) };
    if(!m_scope->m_layout.addNode(m_scope->m_stack.back(), description, item.node)){
        m_context.fail();
        result.valid = false;
        return result;
    }
    m_scope->m_items.push_back(Move(item));
    return result;
}

bool Builder::prepareEditBox(Item& item, EditModel& model, EditBoxState& state, const EditBoxResult& result){
    if(
        state.modelGeneration != model.instanceGeneration() || state.revision != model.revision()
        || state.selectionGeneration != model.selectionGeneration()
        || state.anchor != model.anchor() || state.caret != model.caret() || state.focused != result.focused
    )
        state.caretElapsed = 0.0f;
    else if(IsFinite(m_deltaSeconds) && m_deltaSeconds > 0.0f)
        state.caretElapsed = FMod(state.caretElapsed + Min(m_deltaSeconds, 1.0f), 1.0f);
    if(state.modelGeneration != model.instanceGeneration())
        state.scroll = 0.0f;
    state.modelGeneration = model.instanceGeneration();
    state.revision = model.revision();
    state.selectionGeneration = model.selectionGeneration();
    state.anchor = model.anchor();
    state.caret = model.caret();
    state.focused = result.focused;

    item.editFlags = { item.editOptions.enabled, m_context.input().hover() == item.state.id, result.focused, item.editOptions.readOnly,
        state.caretElapsed < 0.5f };
    item.editFlags.preeditCaretVisible = result.preeditCaretVisible;
    if(!item.editView.snapshot(model) || item.editView.shape(m_text, { {}, m_style.fontSize }) != TextLayoutStatus::Success){
        m_context.fail();
        return false;
    }
    item.editState = &state;
    item.padding = m_editStyle.padding;

    const Name names[]{ m_editStyle.normal, m_editStyle.hover, m_editStyle.focused, m_editStyle.disabled };
    for(const auto& name : names){
        const UiSkinRegion* skinRegion = region(name, m_editStyle.normal);
        if(!skinRegion)
            skinRegion = region(m_editStyle.fallback, m_editStyle.fallback);
        if(!skinRegion)
            continue;
        item.padding.left = Max(item.padding.left, skinRegion->padding.left);
        item.padding.top = Max(item.padding.top, skinRegion->padding.top);
        item.padding.right = Max(item.padding.right, skinRegion->padding.right);
        item.padding.bottom = Max(item.padding.bottom, skinRegion->padding.bottom);
    }
    return true;
}

bool Builder::paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation){
    if(!numericEditMatches(item))
        return false;
    EditBoxPlacement placement;
    const f32 caretWidth = 1.0f / m_paint.displayMetrics().pixelScaleX;
    if(!item.editView.arrange(box.rectangle, item.padding, visibleClip(box.clip), item.editState->scroll, placement, caretWidth))
        return false;
    if(!item.editView.paint(m_text, m_paint, *m_skin, placement, m_editStyle, item.editFlags))
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

