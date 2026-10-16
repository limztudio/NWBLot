// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "selectable_paint.h"

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ListResult Builder::virtualList(const AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options){
    ListResult result;
    if(declarationBlocked() || !m_scope->m_panelActive || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_context.failed() || m_scope->m_items.size() >= s_LayoutMaxNodes || !IsFinite(options.wheelRows) || options.wheelRows <= 0.0f){
        m_context.fail();
        return result;
    }
    // A body callback may have closed or reopened its popup; no model loan is allowed past that boundary.
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::VirtualList);
    if(!widget)
        return result;
    const bool previouslyFocused = m_context.input().focus() == widget->id;
    const u64 previousSelection = state.selectedKey();
    if(!ListBehavior::Reconcile(state, source)){
        m_context.fail();
        return result;
    }
    const ControlToken token{ state.inputGeneration(), source.instanceGeneration(), source.revision() };
    m_context.input().fenceControl(widget->id, widget->declarationGeneration, token);
    if(!options.enabled)
        m_context.input().invalidateTarget(widget->id);
    result.valid = true;
    result.selectionChanged = previousSelection != state.selectedKey();
    result.focused = options.enabled && m_context.input().focus() == widget->id;
    const WidgetId thumb = MakeWidgetId(widget->id, "scrollbar");
    auto gesture = m_context.takePartPointerGesture(*widget, thumb, options.enabled);
    bool haveGesture = gesture && gesture->control == token;
    auto action = m_context.takeControlAction(*widget, options.enabled, token);
    bool haveAction = action.has_value();
    while(haveGesture || haveAction){
        if(haveGesture && (!haveAction || gesture->updateSequence < action->id.sequence)){
            if(!applyListGesture(state, *gesture)){
                m_context.fail();
                result.valid = false;
                return result;
            }
            gesture = m_context.takePartPointerGesture(*widget, thumb, options.enabled);
            haveGesture = gesture && gesture->control == token;
        }
        else{
            if(!ListBehavior::Apply(state, source, options, *action, result)){
                m_context.fail();
                result.valid = false;
                return result;
            }
            action = m_context.takeControlAction(*widget, options.enabled, token);
            haveAction = action.has_value();
        }
    }
    ListFrame frame(m_arena);
    frame.source = &source;
    frame.state = &state;
    frame.widgetStyle = m_style;
    frame.style = m_listStyle;
    frame.options = options;
    frame.token = token;
    frame.rowCount = source.rowCount();
    frame.focusOnCommit = options.enabled && previouslyFocused && !result.focused;
    frame.padding = frame.style.padding;
    const UiSkinRegion* background = region(frame.style.background, frame.style.backgroundFallback);
    if(!background){
        m_context.fail();
        result.valid = false;
        return result;
    }
    const SIMDVectorSingle framepaddingCurrent = VectorSet(frame.padding.left, frame.padding.top, frame.padding.right, frame.padding.bottom);
    const SIMDVectorSingle framepaddingCandidate = VectorSet(background->padding.left, background->padding.top, background->padding.right, background->padding.bottom);
    const SIMDVectorSingle framepaddingMaximum = VectorSelect(framepaddingCandidate, framepaddingCurrent, VectorGreater(framepaddingCurrent, framepaddingCandidate));
    frame.padding = { VectorGetX(framepaddingMaximum), VectorGetY(framepaddingMaximum), VectorGetZ(framepaddingMaximum), VectorGetW(framepaddingMaximum) };
    if(!prepareListMetrics(frame)){
        m_context.fail();
        result.valid = false;
        return result;
    }
    // A fractional offset or a skin minimum can expose more than one row in a natural viewport.
    // Finish that bounded measurement before its intrinsic height enters the parent layout.
    if(options.height.policy == LayoutSizePolicy::Content && frame.rowCount != 0u){
        for(;;){
            LayoutBox provisional;
            const f32 probeWidth = Max(Max(120.0f, background->minimumWidth), frame.padding.left + frame.padding.right + frame.style.scrollbarWidth + frame.rowHeight);
            provisional.rectangle = { 0.0f, 0.0f, probeWidth, Max(background->minimumHeight, frame.rowHeight + frame.padding.top + frame.padding.bottom) };
            provisional.clip = provisional.rectangle;
            ScrollPlacement placement;
            if(!measureListPlacement(frame, provisional, provisional.clip, placement)){
                m_context.fail();
                result.valid = false;
                return result;
            }
            const f32 height = Max(background->minimumHeight, frame.rowHeight + frame.padding.top + frame.padding.bottom);
            if(height == provisional.rectangle.height)
                break;
        }
    }
    Item item(m_arena);
    item.state = *widget;
    item.enabled = options.enabled;
    item.list = static_cast<u32>(m_scope->m_lists.size());
    LayoutNodeDesc description;
    description.width = options.width;
    description.height = options.height;
    const SIMDVectorSingle preferredSize = VectorSet(120.0f, ((frame.rowHeight + frame.padding.top) + frame.padding.bottom), 0.0f, 0.0f);
    const SIMDVectorSingle minimumSize = VectorSet(background->minimumWidth, background->minimumHeight, 0.0f, 0.0f);
    const SIMDVectorSingle intrinsic = VectorSelect(minimumSize, preferredSize, VectorGreater(preferredSize, minimumSize));
    description.intrinsicSize = { VectorGetX(intrinsic), VectorGetY(intrinsic) };
    const auto admittedNode = m_scope->m_layout.addNode(m_scope->m_stack.back(), description);
    if(!admittedNode){
        m_context.fail();
        result.valid = false;
        return result;
    }
    item.node = *admittedNode;
    m_scope->m_lists.push_back(Move(frame));
    m_scope->m_items.push_back(Move(item));
    return result;
}

bool Builder::prepareListMetrics(ListFrame& frame){
    if(!listMatches(frame) || !m_skin)
        return false;
    const auto metrics = SelectablePainter::Measure(*m_skin, frame.style.row);
    if(!metrics)
        return false;
    frame.style.row.padding = metrics->padding;
    ListState::MeasureKey key;
    key.sourceGeneration = frame.token.contentGeneration;
    key.sourceRevision = frame.token.contentRevision;
    key.rowCount = frame.rowCount;
    key.textIdentity = m_text.identity();
    key.textGeneration = m_text.generation();
    key.fontSize = frame.widgetStyle.fontSize;
    key.pixelScaleX = m_paint.displayMetrics().pixelScaleX;
    key.pixelScaleY = m_paint.displayMetrics().pixelScaleY;
    key.rowMetrics = { metrics->padding.left, metrics->padding.top, metrics->padding.right, metrics->padding.bottom, metrics->minimum.x, metrics->minimum.y };
    key.scriptTag = m_textScriptTag;
    if(m_textLanguage.size() >= key.language.size())
        return false;
    for(usize index = 0u; index < m_textLanguage.size(); ++index)
        key.language[index] = m_textLanguage[index];
    frame.measureKey = key;
    ListState& state = *frame.state;
    if(key != state.m_measureKey || state.m_rowHeight <= 0.0f){
        auto empty = m_text.layout(textShapeRequest({}, key.fontSize));
        if(!empty)
            return false;
        const auto bounds = m_text.measure(*empty, m_paint.displayMetrics());
        if(!bounds)
            return false;
        const f32 height = Max(metrics->minimum.y, bounds->height + metrics->padding.top + metrics->padding.bottom);
        if(!IsFinite(height) || height <= 0.0f || !listMatches(frame))
            return false;
        state.m_measureKey = key;
        state.m_rowHeight = height;
    }
    // The last painted offset may belong to a different styled declaration than the measurement cache.
    if(state.m_scrollRowHeight > 0.0f && state.m_scrollSourceGeneration == key.sourceGeneration && state.m_scrollRowHeight != state.m_rowHeight){
        const f64 clippedTop = static_cast<f64>(state.m_placement.contentClip.y) - state.m_placement.viewport.y;
        const f64 anchor = Max(0.0, state.scrollOffset() + clippedTop) / state.m_scrollRowHeight;
        if(!state.m_scroll.setOffset(Max(0.0, anchor * state.m_rowHeight - clippedTop)))
            return false;
    }
    state.m_scrollRowHeight = state.m_rowHeight;
    state.m_scrollSourceGeneration = key.sourceGeneration;
    frame.rowHeight = state.m_rowHeight;
    frame.scrollOffset = state.scrollOffset();
    frame.cursorKey = state.cursorKey();
    frame.revealCursor = state.m_ensureCursor;
    return listMatches(frame);
}

bool Builder::applyListGesture(ListState& state, const PointerGesture& gesture)noexcept{
    const f64 travel = (static_cast<f64>(gesture.referenceRectangle.height) - static_cast<f64>(gesture.targetRectangle.height));
    if(!IsFinite(travel) || travel <= 0.0 || !IsFinite(gesture.maximum) || gesture.maximum < 0.0)
        return false;
    const SIMDVectorDouble startDeltaValue = VectorDoubleSubtract(
        VectorDoubleSet(static_cast<f64>(gesture.targetRectangle.y), static_cast<f64>(gesture.position.y), 0.0, 0.0),
        VectorDoubleSet(static_cast<f64>(gesture.referenceRectangle.y), static_cast<f64>(gesture.origin.y), 0.0, 0.0)
    );
    const f64 start = VectorDoubleGetX(startDeltaValue);
    const f64 delta = VectorDoubleGetY(startDeltaValue);
    const f64 fraction = Clamp(((start + delta) / travel), 0.0, 1.0);
    state.m_ensureCursor = false;
    return state.m_scroll.setOffset((fraction * gesture.maximum));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

