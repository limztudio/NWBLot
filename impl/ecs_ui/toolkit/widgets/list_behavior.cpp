// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct KeyResolution{
    u64 index = 0u;
    bool present = false;
};

[[nodiscard]] static Expected<KeyResolution> ResolveKey(const IListDataSource& source, const u64 count, const u64 key){
    if(key == 0u || count == 0u)
        return KeyResolution{};
    const auto index = source.indexOf(key);
    if(!index)
        return KeyResolution{};
    if(*index >= count || source.key(*index) != key)
        return MakeUnexpected(Failure{});
    return KeyResolution{ *index, source.enabled(*index) };
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

[[nodiscard]] static Expected<u64> FindCursor(const IListDataSource& source, const u64 count, const u64 cursor,
    const ControlAction& action
){
    if(count == 0u)
        return 0u;
    const auto current = ResolveKey(source, count, cursor);
    if(!current)
        return MakeUnexpected(current.error());
    const bool reverse = IsReverse(action.kind);
    u64 start = reverse ? count - 1u : 0u;
    if(action.kind == ControlActionKind::Home)
        start = 0u;
    else if(action.kind == ControlActionKind::End)
        start = count - 1u;
    else if(current->present){
        const u64 distance = action.kind == ControlActionKind::PageUp || action.kind == ControlActionKind::PageDown
            ? action.pageRows : 1u;
        start = reverse ? current->index - Min(distance, current->index)
            : current->index + Min(distance, count - 1u - current->index);
    }
    const auto index = source.findEnabled(start, reverse);
    if(!index)
        return 0u;
    if(*index >= count || (reverse ? *index > start : *index < start))
        return MakeUnexpected(Failure{});
    const u64 key = source.key(*index);
    if(key == 0u)
        return MakeUnexpected(Failure{});
    const auto resolved = ResolveKey(source, count, key);
    if(!resolved || !resolved->present || resolved->index != *index)
        return MakeUnexpected(Failure{});
    return key;
}

[[nodiscard]] static Expected<f64> WheelOffset(const f64 current, const ControlAction& action)noexcept{
    if(
        !IsFinite(action.delta) || !IsFinite(action.step) || action.step <= 0.0
        || !IsFinite(action.maximum) || action.maximum < 0.0
    )
        return MakeUnexpected(Failure{});
    f64 candidate = Min(current, action.maximum);
    if(action.delta > 0.0)
        candidate = action.delta >= candidate / action.step ? 0.0 : candidate - action.delta * action.step;
    else if(action.delta < 0.0){
        const f64 remaining = action.maximum - candidate;
        candidate = -action.delta >= remaining / action.step ? action.maximum : candidate + -action.delta * action.step;
    }
    candidate = Clamp(candidate, 0.0, action.maximum);
    if(!IsFinite(candidate))
        return MakeUnexpected(Failure{});
    return candidate;
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
    const auto selectedResolution = ResolveKey(source, count, selected);
    if(!selectedResolution)
        return false;
    bool cursorPresent = selectedResolution->present;
    if(cursor != selected){
        const auto cursorResolution = ResolveKey(source, count, cursor);
        if(!cursorResolution)
            return false;
        cursorPresent = cursorResolution->present;
    }
    selected = selectedResolution->present ? selected : 0u;
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
    const ControlAction& action, ListResult& result
){
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
        const auto offset = WheelOffset(state.m_scroll.offset(), action);
        if(
            !offset || state.m_inputGeneration != inputGeneration || !state.m_scroll.setOffset(*offset)
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
        const auto resolved = ResolveKey(source, count, candidate);
        if(!resolved)
            return false;
        if(!resolved->present){
            if(action.kind == ControlActionKind::Activate)
                return false;
            candidate = 0u;
        }
    }
    else{
        const auto cursor = FindCursor(source, count, state.m_cursor, action);
        if(!cursor)
            return false;
        candidate = *cursor;
    }
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
    const f64 viewportHeight
){
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
    const auto resolved = ResolveKey(source, source.rowCount(), state.m_cursor);
    if(!resolved || !resolved->present)
        return false;
    const f64 start = static_cast<f64>(resolved->index) * rowHeight;
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

