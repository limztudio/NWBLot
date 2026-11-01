// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiComboTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 ComboSource::key(const u64 index)const noexcept{
    observeCallback();
    ++keyCalls;
    return rawKey(index);
}

Expected<u64> ComboSource::indexOf(const u64 value)const noexcept{
    observeCallback();
    ++lookupCalls;
    return UiWidgetTests::FindArithmeticFixtureRowIndex(count, removed, reverse, value);
}

Expected<u64> ComboSource::findEnabled(const u64 start, const b8 backwards)const noexcept{
    observeCallback();
    ++searchCalls;
    return UiWidgetTests::FindSingleDisabledFixtureRow(count, start, backwards, disabled, [this](const u64 index)noexcept{ return rawKey(index); });
}

StringView ComboSource::text(const u64 index)const{
    observeCallback();
    ++textCalls;
    return index % 2u == 0u ? "First" : "Second";
}

b8 ComboSource::enabled(const u64 index)const noexcept{
    observeCallback();
    ++enabledCalls;
    return index < count && rawKey(index) != disabled;
}

void ComboSource::resetCounters()const noexcept{
    keyCalls = 0u;
    lookupCalls = 0u;
    searchCalls = 0u;
    textCalls = 0u;
    enabledCalls = 0u;
}

ComboOptions Options()noexcept{
    ComboOptions options;
    options.width = { LayoutSizePolicy::Fixed, 220.0f };
    options.height = { LayoutSizePolicy::Fixed, 32.0f };
    options.popupHeight = 144.0f;
    options.placeholder = "Choose";
    return options;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ComboFixture::declareSource(const u64 generation, const IListDataSource& source, ComboState& state, const ComboOptions& options, const Rect& bounds){
    if(!begin(generation) || !m_builder.beginPanel(s_ComboPanelKey, bounds))
        return false;
    m_result = m_builder.comboBox(s_ComboKey, source, state, options);
    return m_result.valid;
}

b8 ComboFixture::openByPointer(const u64 generation){
    const HitTarget* field = target(id(s_ComboKey, s_ComboPanelKey));
    if(!field)
        return false;
    click(Center(field->rectangle));
    return accept(generation) && m_state.isOpen();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

