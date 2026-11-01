// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPopupToolsTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 PopupToolsSource::revision()const noexcept{
    if(closeComboOnRevision){
        ComboState& state = *MakeNotNull(Exchange(closeComboOnRevision, nullptr));
        state.close();
    }
    return contentRevision;
}

Expected<u64> PopupToolsSource::indexOf(const u64 value)const noexcept{
    u64 index = 0u;
    if(value == 0u || value > count)
        return MakeUnexpected(Failure{});
    index = value - 1u;
    return index;
}

Expected<u64> PopupToolsSource::findEnabled(const u64 start, const b8 reverse)const noexcept{
    u64 index = 0u;
    if(start >= count)
        return MakeUnexpected(Failure{});
    index = start;
    if(index + 1u != 3u)
        return index;
    if(reverse){
        if(index == 0u)
            return MakeUnexpected(Failure{});
        --index;
    }
    else{
        ++index;
        if(index >= count)
            return MakeUnexpected(Failure{});
    }
    return index;
}

StringView PopupToolsSource::text(const u64 index)const noexcept{
    if(changeRevisionOnText){
        changeRevisionOnText = false;
        ++contentRevision;
    }
    if(closeOnText){
        ContextMenuState& state = *MakeNotNull(Exchange(closeOnText, nullptr));
        state.close();
    }
    if(resetTooltipOnText){
        TooltipState& state = *MakeNotNull(Exchange(resetTooltipOnText, nullptr));
        state.reset();
    }
    if(selectListOnText){
        ListState& state = *MakeNotNull(Exchange(selectListOnText, nullptr));
        state.select(selectListKey);
    }
    constexpr Array<StringView, 5u> s_Labels = { "One", "Two", "Three", "Four", "Five" };
    return index < count && index < s_Labels.size() ? s_Labels[index] : StringView{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PopupToolsFixture::SetUp(){
    WidgetFixture::SetUp();
    m_anchorOptions.width = { LayoutSizePolicy::Fixed, 220.0f };
    m_anchorOptions.height = { LayoutSizePolicy::Fixed, 32.0f };
    m_tooltipOptions.delaySeconds = 0.0f;
    m_menuOptions.size = { 240.0f, 220.0f };
}

b8 PopupToolsFixture::declareTools(const u64 generation, const b8 tooltip, const b8 menu, const Rect& bounds){
    if(!begin(generation) || !m_builder.beginPanel(s_PopupToolsPanelKey, bounds))
        return false;
    m_builder.setDeltaSeconds(m_deltaSeconds);
    m_anchorActivated = m_builder.button(s_AnchorKey, "Anchor", m_anchorOptions);
    m_menuResult = {};
    if(tooltip && !m_builder.tooltip(s_TooltipKey, s_AnchorKey, "Helpful text", m_tooltip, m_tooltipOptions))
        return false;
    if(menu){
        m_menuResult = m_builder.contextMenu(s_MenuKey, s_AnchorKey, m_source, m_menu, m_menuOptions);
        if(!m_menuResult.valid)
            return false;
    }
    return true;
}

b8 PopupToolsFixture::acceptPopupTooltip(const u64 generation){
    if(!begin(generation))
        return false;
    PopupOptions options;
    options.anchor = { 40.0f, 40.0f, 40.0f, 24.0f };
    options.size = { 320.0f, 220.0f };
    m_builder.setDeltaSeconds(m_deltaSeconds);
    if(!m_builder.beginPopup("plain", m_plainPopup, options))
        return false;
    m_anchorActivated = m_builder.button(s_AnchorKey, "Popup anchor", m_anchorOptions);
    return m_builder.tooltip(s_TooltipKey, s_AnchorKey, "Overlay help", m_tooltip, m_tooltipOptions) && m_builder.endPopup() && m_context.endRoot() && m_context.finishFrame() && m_context.commitFrame(generation);
}

const HitTarget* PopupToolsFixture::menuHost()const noexcept{
    for(const HitTarget& entry : m_context.input().targets()){
        if(entry.navigable && entry.popup.instanceGeneration == m_menu.instanceGeneration())
            return &entry;
    }
    return nullptr;
}

const HitTarget* PopupToolsFixture::menuRow(const u64 key)const noexcept{
    for(const HitTarget& entry : m_context.input().targets()){
        if(entry.value == key && entry.popup.instanceGeneration == m_menu.instanceGeneration())
            return &entry;
    }
    return nullptr;
}

b8 PopupToolsFixture::moveToAnchor(const AStringView parent){
    const HitTarget* accepted = target(id(s_AnchorKey, parent));
    if(!accepted)
        return false;
    const Point point = Center(accepted->rectangle);
    return send({ .position = point , .type = InputEventType::PointerMove }).hover == id(s_AnchorKey, parent);
}

void PopupToolsFixture::secondaryClick(const Point& point){
    EXPECT_TRUE(send({ .position = point , .type = InputEventType::SecondaryDown }).pointerConsumed);
    EXPECT_TRUE(send({ .position = point , .type = InputEventType::SecondaryUp }).pointerConsumed);
}

b8 PopupToolsFixture::openBySecondary(const u64 generation){
    const HitTarget* accepted = target(id(s_AnchorKey, s_PopupToolsPanelKey));
    if(!accepted)
        return false;
    const Point point = Center(accepted->rectangle);
    secondaryClick(point);
    return acceptTools(generation, false) && m_menu.isOpen();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

