// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPopupToolsTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;
using namespace UiWidgetTests;

inline constexpr AStringView s_PopupToolsPanelKey = "panel";
inline constexpr AStringView s_AnchorKey = "anchor";
inline constexpr AStringView s_TooltipKey = "tip";
inline constexpr AStringView s_MenuKey = "menu";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PopupToolsSource final : public IListDataSource{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override{ return generation; }
    [[nodiscard]] virtual u64 revision()const noexcept override{
        if(closeComboOnRevision){
            ComboState* state = closeComboOnRevision;
            closeComboOnRevision = nullptr;
            state->close();
        }
        return contentRevision;
    }
    [[nodiscard]] virtual u64 rowCount()const noexcept override{ return count; }
    [[nodiscard]] virtual u64 key(const u64 index)const noexcept override{ return index < count ? index + 1u : 0u; }

    [[nodiscard]] virtual Expected<u64> indexOf(const u64 value)const noexcept override{
        u64 index = 0u;
        if(value == 0u || value > count)
            return MakeUnexpected(Failure{});
        index = value - 1u;
        return index;
    }

    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const bool reverse)const noexcept override{
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

    [[nodiscard]] virtual StringView text(const u64 index)const noexcept override{
        if(changeRevisionOnText){
            changeRevisionOnText = false;
            ++contentRevision;
        }
        if(closeOnText){
            ContextMenuState* state = closeOnText;
            closeOnText = nullptr;
            state->close();
        }
        if(resetTooltipOnText){
            TooltipState* state = resetTooltipOnText;
            resetTooltipOnText = nullptr;
            state->reset();
        }
        if(selectListOnText){
            ListState* state = selectListOnText;
            selectListOnText = nullptr;
            state->select(selectListKey);
        }
        constexpr StringView s_Labels[]{ "One", "Two", "Three", "Four", "Five" };
        return index < count && index < 5u ? s_Labels[index] : StringView{};
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const noexcept override{ return index < count && index + 1u != 3u; }


public:
    u64 generation = 1401u;
    u64 count = 5u;
    u64 selectListKey = 4u;
    mutable u64 contentRevision = 1u;
    mutable bool changeRevisionOnText = false;
    mutable ContextMenuState* closeOnText = nullptr;
    mutable TooltipState* resetTooltipOnText = nullptr;
    mutable ListState* selectListOnText = nullptr;
    mutable ComboState* closeComboOnRevision = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PopupToolsFixture : public WidgetFixture{
protected:
    virtual void SetUp()override{
        WidgetFixture::SetUp();
        m_anchorOptions.width = { LayoutSizePolicy::Fixed, 220.0f };
        m_anchorOptions.height = { LayoutSizePolicy::Fixed, 32.0f };
        m_tooltipOptions.delaySeconds = 0.0f;
        m_menuOptions.size = { 240.0f, 220.0f };
    }

    [[nodiscard]] bool declareTools(const u64 generation, const bool tooltip = true, const bool menu = true, const Rect& bounds = { 10.0f, 10.0f, 360.0f, 280.0f }){
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

    [[nodiscard]] bool prepareTools(const u64 generation, const bool tooltip = true, const bool menu = true, const Rect& bounds = { 10.0f, 10.0f, 360.0f, 280.0f }){
        return declareTools(generation, tooltip, menu, bounds) && finishPanel();
    }

    [[nodiscard]] bool acceptTools(const u64 generation, const bool tooltip = true, const bool menu = true, const Rect& bounds = { 10.0f, 10.0f, 360.0f, 280.0f }){
        return prepareTools(generation, tooltip, menu, bounds) && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool acceptPopupTooltip(const u64 generation){
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

    [[nodiscard]] WidgetId anchor(const AStringView parent = s_PopupToolsPanelKey)const{ return id(s_AnchorKey, parent); }

    [[nodiscard]] const HitTarget* menuHost()const noexcept{
        for(const HitTarget& entry : m_context.input().targets()){
            if(entry.navigable && entry.popup.instanceGeneration == m_menu.instanceGeneration())
                return &entry;
        }
        return nullptr;
    }

    [[nodiscard]] const HitTarget* menuRow(const u64 key)const noexcept{
        for(const HitTarget& entry : m_context.input().targets()){
            if(entry.value == key && entry.popup.instanceGeneration == m_menu.instanceGeneration())
                return &entry;
        }
        return nullptr;
    }

    [[nodiscard]] bool moveToAnchor(const AStringView parent = s_PopupToolsPanelKey){
        const HitTarget* accepted = target(anchor(parent));
        if(!accepted)
            return false;
        const Point point = Center(accepted->rectangle);
        return send({ InputEventType::PointerMove, point }).hover == anchor(parent);
    }

    void secondaryClick(const Point& point){
        EXPECT_TRUE(send({ InputEventType::SecondaryDown, point }).pointerConsumed);
        EXPECT_TRUE(send({ InputEventType::SecondaryUp, point }).pointerConsumed);
    }

    [[nodiscard]] bool openBySecondary(const u64 generation){
        const HitTarget* accepted = target(anchor());
        if(!accepted)
            return false;
        const Point point = Center(accepted->rectangle);
        secondaryClick(point);
        return acceptTools(generation, false) && m_menu.isOpen();
    }


protected:
    PopupToolsSource m_source;
    TooltipState m_tooltip;
    ContextMenuState m_menu;
    PopupState m_plainPopup;
    WidgetOptions m_anchorOptions;
    TooltipOptions m_tooltipOptions;
    ContextMenuOptions m_menuOptions;
    ContextMenuResult m_menuResult;
    f32 m_deltaSeconds = 0.0f;
    bool m_anchorActivated = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

