// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "search_combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiSearchComboTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 FilterView::rowCount()const noexcept{
    return mode == FilterMode::Empty ? 0u : mode == FilterMode::Second ? full->count / 2u : full->count;
}

u64 FilterView::key(const u64 index)const noexcept{
    return mode == FilterMode::Second ? index * 2u + 2u : index + 1u;
}

Expected<u64> FilterView::indexOf(const u64 value)const noexcept{
    u64 index = 0u;
    if(value == 0u || value > full->count || mode == FilterMode::Empty || (mode == FilterMode::Second && value % 2u != 0u))
        return MakeUnexpected(Failure{});
    index = mode == FilterMode::Second ? value / 2u - 1u : value - 1u;
    return index;
}

Expected<u64> FilterView::findEnabled(const u64 start, const b8 reverse)const{
    return UiWidgetTests::FindSingleDisabledFixtureRow(rowCount(), start, reverse, full->disabled, [this](const u64 index)noexcept{ return key(index); });
}

StringView FilterView::text(const u64 index)const{
    if(textMutation){
        EditModel& query = *MakeNotNull(Exchange(textMutation, nullptr));
        if(!query.setText(s_CallbackQuery))
            ADD_FAILURE() << "callback query change failed";
    }
    return full->text(key(index) - 1u);
}

b8 FilterView::enabled(const u64 index)const{
    if(enabledMutation){
        EditModel& query = *MakeNotNull(Exchange(enabledMutation, nullptr));
        if(!query.setText(s_EmptyResultQuery))
            ADD_FAILURE() << "enabled callback query change failed";
    }
    return index < rowCount() && full->enabled(key(index) - 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SearchSource::SearchSource(Core::Alloc::GlobalArena& arena)noexcept
    : m_query(arena)
{
    view.full = &full;
    alternate.full = &full;
}

b8 SearchSource::enabled(const u64 index)const{
    if(fullMutation){
        EditModel& query = *MakeNotNull(Exchange(fullMutation, nullptr));
        if(!query.setText(s_EmptyResultQuery))
            ADD_FAILURE() << "full source callback query change failed";
    }
    return full.enabled(index);
}

b8 SearchSource::filter(const AStringView query){
    ++filterCalls;
    if(filterMutation){
        EditModel& model = *MakeNotNull(Exchange(filterMutation, nullptr));
        if(!model.setText(s_CallbackQuery))
            return false;
    }
    const AStringView previous{ m_query.data(), m_query.size() };
    if(previous != query){
        m_query.assign(query.data(), query.size());
        ++view.contentRevision;
    }
    view.mode = query == s_SecondHalfQuery ? FilterMode::Second : query == s_EmptyResultQuery ? FilterMode::Empty : FilterMode::All;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxResult SearchEditHost::edit([[maybe_unused]] const WidgetState& widget, [[maybe_unused]] const EditBoxOptions& options, [[maybe_unused]] EditModel& model)noexcept{
    ++contextScopedLoans;
    return {};
}

EditBoxResult SearchEditHost::editInPopup(const WidgetState& widget, const EditBoxOptions& options, const PopupToken& popup, EditModel& model){
    ++loans;
    lastPopup = popup;
    loanContextPopup = m_context.popupToken();
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_context.input().focus() == widget.id;
    result.submitted = submitted;
    result.cancelled = cancelled;
    submitted = false;
    cancelled = false;
    if(m_changeText){
        result.valid = model.setText(m_nextText);
        result.textChanged = result.valid;
        m_changeText = false;
    }
    if(preedit){
        result.valid = result.valid && model.beginComposition() && model.updateComposition(s_PreeditText, 0u, 7u);
        preedit = false;
    }
    return result;
}

b8 SearchEditHost::publish([[maybe_unused]] const WidgetState& widget, const EditBoxView& view, [[maybe_unused]] const EditBoxPlacement& placement, [[maybe_unused]] const EditBoxOptions& options){
    ++publications;
    publishContextPopup = m_context.popupToken();
    publishedComposing = view.composing();
    if(publishMutation){
        EditModel& query = *MakeNotNull(Exchange(publishMutation, nullptr));
        return query.setText("published");
    }
    return true;
}

void SearchEditHost::text(const AStringView value)noexcept{
    m_nextText = value;
    m_changeText = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SearchComboOptions SearchFixture::Options()noexcept{
    SearchComboOptions value;
    value.combo = NWB::UiComboTests::Options();
    value.combo.popupHeight = 220.0f;
    return value;
}

SearchFixture::SearchFixture()
    : m_searchSource(m_arena)
    , m_search(m_arena)
    , m_host(m_context)
{}

void SearchFixture::SetUp(){
    WidgetFixture::SetUp();
    m_builder.setEditHost(&m_host);
}

b8 SearchFixture::declareSearch(const u64 generation){
    if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 280.0f }))
        return false;
    m_searchResult = m_builder.searchComboBox(s_ComboKey, m_searchSource, m_search, Options());
    return m_searchResult.combo.valid;
}

b8 SearchFixture::openSearch(const u64 generation){
    const HitTarget* field = target(id(s_ComboKey, s_ComboPanelKey));
    if(!field)
        return false;
    click(Center(field->rectangle));
    return acceptSearch(generation) && m_search.combo().isOpen();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

