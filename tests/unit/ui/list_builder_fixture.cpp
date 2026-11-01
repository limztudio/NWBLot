// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list_builder_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiListTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 ArithmeticSource::instanceGeneration()const noexcept{
    invalidateText();
    return generation;
}

u64 ArithmeticSource::revision()const noexcept{
    invalidateText();
    return contentRevision;
}

u64 ArithmeticSource::rowCount()const noexcept{
    invalidateText();
    return count;
}

u64 ArithmeticSource::key(const u64 index)const noexcept{
    invalidateText();
    ++keyCalls;
    if(duplicate)
        return 1u;
    return rawKey(index);
}

Expected<u64> ArithmeticSource::indexOf(const u64 value)const noexcept{
    invalidateText();
    ++lookupCalls;
    if(malformedLookup)
        return count;
    return UiWidgetTests::FindArithmeticFixtureRowIndex(count, removed, reverse, value);
}

Expected<u64> ArithmeticSource::findEnabled(const u64 start, const b8 backwards)const noexcept{
    invalidateText();
    return UiWidgetTests::FindSingleDisabledFixtureRow(count, start, backwards, disabled, [this](const u64 index)noexcept{ return rawKey(index); });
}

StringView ArithmeticSource::text(const u64 index)const noexcept{
    ++textCalls;
    if(index < textCallsByIndex.size())
        ++textCallsByIndex[index];
    if(index == specialIndex){
        for(usize byte = 0u; byte < specialText.size(); ++byte)
            temporary[byte] = specialText[byte];
        return { temporary.data(), specialText.size() };
    }
    temporary[0u] = static_cast<char>('A' + index % 26u);
    temporary[1u] = 'x';
    return { temporary.data(), 2u };
}

b8 ArithmeticSource::enabled(const u64 index)const noexcept{
    invalidateText();
    return rawKey(index) != disabled;
}

ListOptions Options()noexcept{
    ListOptions options;
    options.width = { LayoutSizePolicy::Fixed, 280.0f };
    options.height = { LayoutSizePolicy::Fixed, 120.0f };
    return options;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiListBuilderTests::prepareSource(const u64 generation, const IListDataSource& source, ListState& state, const ListOptions& options, const DisplayMetrics& display){
    if(!begin(generation, display) || !m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }))
        return false;
    m_result = m_builder.virtualList(s_ListKey, source, state, options);
    return m_result.valid && finishPanel();
}

b8 UiListBuilderTests::preparePopups(const u64 generation, PopupState& lowerPopup, ListState& lowerState, PopupState* upperPopup, ListState* upperState){
    if(!begin(generation) || !m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }))
        return false;
    if(!m_builder.virtualList(s_ListKey, m_source, m_state, Options()).valid || !m_builder.endPanel())
        return false;
    PopupOptions popup;
    popup.anchor = { 350.0f, 10.0f, 10.0f, 10.0f };
    popup.size = { 320.0f, 180.0f };
    if(m_builder.beginPopup(s_PopupKey, lowerPopup, popup)){
        if(!m_builder.virtualList(s_ListKey, m_source, lowerState, Options()).valid || !m_builder.endPopup())
            return false;
    }
    if(upperPopup && upperState){
        popup.anchor = { 400.0f, 220.0f, 10.0f, 10.0f };
        if(m_builder.beginPopup(s_UpperPopupKey, *upperPopup, popup)){
            if(!m_builder.virtualList(s_ListKey, m_source, *upperState, Options()).valid || !m_builder.endPopup())
                return false;
        }
    }
    return m_context.endRoot() && m_context.finishFrame();
}

b8 UiListBuilderTests::wheel(const f64 delta){
    const HitTarget* accepted = target(id(s_ListKey, s_PanelKey));
    if(!accepted)
        return false;
    InputEvent event;
    event.type = InputEventType::PointerWheel;
    event.position = Center(accepted->rectangle);
    event.scrollY = delta;
    return send(event).pointerConsumed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

