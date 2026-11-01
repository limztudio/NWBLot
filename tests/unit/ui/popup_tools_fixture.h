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
    [[nodiscard]] virtual u64 revision()const noexcept override;
    [[nodiscard]] virtual u64 rowCount()const noexcept override{ return count; }
    [[nodiscard]] virtual u64 key(const u64 index)const noexcept override{ return index < count ? index + 1u : 0u; }
    [[nodiscard]] virtual Expected<u64> indexOf(u64 value)const noexcept override;
    [[nodiscard]] virtual Expected<u64> findEnabled(u64 start, b8 reverse)const noexcept override;
    [[nodiscard]] virtual StringView text(u64 index)const noexcept override;
    [[nodiscard]] virtual b8 enabled(const u64 index)const noexcept override{ return index < count && index + 1u != 3u; }


public:
    u64 generation = 1401u;
    u64 count = 5u;
    u64 selectListKey = 4u;
    mutable u64 contentRevision = 1u;
    mutable ContextMenuState* closeOnText = nullptr;
    mutable TooltipState* resetTooltipOnText = nullptr;
    mutable ListState* selectListOnText = nullptr;
    mutable ComboState* closeComboOnRevision = nullptr;
    mutable b8 changeRevisionOnText = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PopupToolsFixture : public WidgetFixture{
protected:
    virtual void SetUp()override;
    [[nodiscard]] b8 declareTools(u64 generation, b8 tooltip = true, b8 menu = true, const Rect& bounds = { 10.0f, 10.0f, 360.0f, 280.0f });
    [[nodiscard]] b8 prepareTools(const u64 generation, const b8 tooltip = true, const b8 menu = true, const Rect& bounds = { 10.0f, 10.0f, 360.0f, 280.0f }){
        return declareTools(generation, tooltip, menu, bounds) && finishPanel();
    }
    [[nodiscard]] b8 acceptTools(const u64 generation, const b8 tooltip = true, const b8 menu = true, const Rect& bounds = { 10.0f, 10.0f, 360.0f, 280.0f }){
        return prepareTools(generation, tooltip, menu, bounds) && m_context.commitFrame(generation);
    }
    [[nodiscard]] b8 acceptPopupTooltip(u64 generation);
    [[nodiscard]] const HitTarget* menuHost()const noexcept;
    [[nodiscard]] const HitTarget* menuRow(u64 key)const noexcept;
    [[nodiscard]] b8 moveToAnchor(AStringView parent = s_PopupToolsPanelKey);
    void secondaryClick(const Point& point);
    [[nodiscard]] b8 openBySecondary(u64 generation);


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
    b8 m_anchorActivated = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

