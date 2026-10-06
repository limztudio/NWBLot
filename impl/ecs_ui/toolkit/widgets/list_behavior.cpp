// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ResolveKey(const IListDataSource& source, const u64 count, const u64 key,
    u64& index, bool& present){
    present = false;
    index = 0u;
    if(key == 0u || count == 0u || !source.indexOf(key, index))
        return true;
    if(index >= count || source.key(index) != key)
        return false;
    present = source.enabled(index);
    return true;
}

[[nodiscard]] static bool SourceMatches(const IListDataSource& source, const u64 generation, const u64 revision){
    return generation != 0u && revision != 0u && source.instanceGeneration() == generation && source.revision() == revision;
}

[[nodiscard]] static bool ValidOptions(const ListOptions& options)noexcept{
    return
        options.enabled && IsFinite(options.rowHeight) && options.rowHeight > 0.0f
        && IsFinite(options.wheelRows) && options.wheelRows > 0.0f
    ;
}

[[nodiscard]] static bool IsReverse(const ControlActionKind::Enum kind)noexcept{
    return kind == ControlActionKind::Up || kind == ControlActionKind::PageUp || kind == ControlActionKind::End;
}

[[nodiscard]] static bool FindCursor(const IListDataSource& source, const u64 count, const u64 cursor,
    const ControlAction& action, u64& candidate){
    candidate = 0u;
    if(count == 0u)
        return true;
    u64 current = 0u;
    bool hasCurrent = false;
    if(!ResolveKey(source, count, cursor, current, hasCurrent))
        return false;
    const bool reverse = IsReverse(action.kind);
    u64 start = reverse ? count - 1u : 0u;
    if(action.kind == ControlActionKind::Home)
        start = 0u;
    else if(action.kind == ControlActionKind::End)
        start = count - 1u;
    else if(hasCurrent){
        const u64 distance = action.kind == ControlActionKind::PageUp || action.kind == ControlActionKind::PageDown
            ? action.pageRows : 1u;
        start = reverse ? current - Min(distance, current) : current + Min(distance, count - 1u - current);
    }
    u64 index = 0u;
    if(!source.findEnabled(start, reverse, index))
        return true;
    if(index >= count || (reverse ? index > start : index < start))
        return false;
    const u64 key = source.key(index);
    u64 resolved = 0u;
    bool present = false;
    if(key == 0u || !ResolveKey(source, count, key, resolved, present) || !present || resolved != index)
        return false;
    candidate = key;
    return true;
}

[[nodiscard]] static bool WheelOffset(const f64 current, const ControlAction& action, f64& candidate)noexcept{
    if(
        !IsFinite(action.delta) || !IsFinite(action.step) || action.step <= 0.0
        || !IsFinite(action.maximum) || action.maximum < 0.0
    )
        return false;
    candidate = Min(current, action.maximum);
    if(action.delta > 0.0)
        candidate = action.delta >= candidate / action.step ? 0.0 : candidate - action.delta * action.step;
    else if(action.delta < 0.0){
        const f64 remaining = action.maximum - candidate;
        candidate = -action.delta >= remaining / action.step ? action.maximum : candidate + -action.delta * action.step;
    }
    candidate = Clamp(candidate, 0.0, action.maximum);
    return IsFinite(candidate);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ListBehavior::Reconcile(ListState& state, const IListDataSource& source){
    using namespace __hidden_ui_list_behavior;
    const u64 inputGeneration = state.m_inputGeneration;
    const u64 generation = source.instanceGeneration();
    const u64 revision = source.revision();
    if(generation == 0u || revision == 0u)
        return false;
    const bool replacement = state.m_sourceGeneration != 0u && state.m_sourceGeneration != generation;
    u64 selected = replacement ? 0u : state.m_selected;
    u64 cursor = replacement ? 0u : state.m_cursor;
    const u64 count = source.rowCount();
    u64 index = 0u;
    bool selectedPresent = false;
    if(!ResolveKey(source, count, selected, index, selectedPresent))
        return false;
    bool cursorPresent = selectedPresent;
    if(cursor != selected && !ResolveKey(source, count, cursor, index, cursorPresent))
        return false;
    selected = selectedPresent ? selected : 0u;
    cursor = cursorPresent ? cursor : 0u;
    if(!SourceMatches(source, generation, revision) || state.m_inputGeneration != inputGeneration)
        return false;
    if(replacement && !state.m_scroll.setOffset(0.0))
        return false;
    const bool changed = state.m_sourceGeneration != generation || state.m_sourceRevision != revision;
    state.m_selected = selected;
    state.m_cursor = cursor;
    state.m_ensureCursor = cursor != 0u && (state.m_ensureCursor || changed);
    state.m_sourceGeneration = generation;
    state.m_sourceRevision = revision;
    return true;
}

bool ListBehavior::Apply(ListState& state, const IListDataSource& source, const ListOptions& options,
    const ControlAction& action, ListResult& result){
    using namespace __hidden_ui_list_behavior;
    const u64 inputGeneration = state.m_inputGeneration;
    const ControlToken token{ state.m_inputGeneration, state.m_sourceGeneration, state.m_sourceRevision };
    if(
        !ValidOptions(options) || !token.valid() || action.control != token
        || !SourceMatches(source, token.contentGeneration, token.contentRevision) || action.kind > ControlActionKind::Activate
        || ((action.kind == ControlActionKind::PageUp || action.kind == ControlActionKind::PageDown) && action.pageRows == 0u)
        || state.m_inputGeneration != inputGeneration
    )
        return false;
    if(action.kind == ControlActionKind::Wheel){
        f64 offset = 0.0;
        if(
            !WheelOffset(state.m_scroll.offset(), action, offset) || state.m_inputGeneration != inputGeneration
            || !state.m_scroll.setOffset(offset)
        )
            return false;
        state.m_ensureCursor = false;
        result.valid = true;
        return true;
    }
    const u64 count = source.rowCount();
    u64 candidate = 0u;
    const bool activate = action.kind == ControlActionKind::Activate || action.kind == ControlActionKind::Submit;
    if(activate){
        candidate = action.kind == ControlActionKind::Activate ? action.value : state.m_cursor;
        u64 index = 0u;
        bool present = false;
        if(!ResolveKey(source, count, candidate, index, present))
            return false;
        if(!present){
            if(action.kind == ControlActionKind::Activate)
                return false;
            candidate = 0u;
        }
    }
    else if(!FindCursor(source, count, state.m_cursor, action, candidate))
        return false;
    if(!SourceMatches(source, token.contentGeneration, token.contentRevision) || state.m_inputGeneration != inputGeneration)
        return false;
    if(candidate != 0u){
        state.m_cursor = candidate;
        state.m_ensureCursor = true;
        if(activate || options.selectOnNavigate){
            result.selectionChanged = result.selectionChanged || state.m_selected != candidate;
            state.m_selected = candidate;
        }
        result.activated = result.activated || activate;
    }
    result.valid = true;
    return true;
}

bool ListBehavior::EnsureCursor(ListState& state, const IListDataSource& source, const f32 rowHeight,
    const f64 viewportHeight){
    using namespace __hidden_ui_list_behavior;
    const u64 inputGeneration = state.m_inputGeneration;
    if(
        !IsFinite(rowHeight) || rowHeight <= 0.0f || !IsFinite(viewportHeight) || viewportHeight < 0.0
        || !SourceMatches(source, state.m_sourceGeneration, state.m_sourceRevision)
        || state.m_inputGeneration != inputGeneration
    )
        return false;
    if(!state.m_ensureCursor)
        return true;
    if(state.m_cursor == 0u){
        state.m_ensureCursor = false;
        return true;
    }
    u64 index = 0u;
    bool present = false;
    if(!ResolveKey(source, source.rowCount(), state.m_cursor, index, present) || !present)
        return false;
    const f64 start = static_cast<f64>(index) * rowHeight;
    const f64 end = start + rowHeight;
    if(
        !IsFinite(start) || !IsFinite(end) || end <= start
        || !SourceMatches(source, state.m_sourceGeneration, state.m_sourceRevision)
        || state.m_inputGeneration != inputGeneration
        || !state.m_scroll.ensureVisible(start, end, viewportHeight)
    )
        return false;
    state.m_ensureCursor = false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

