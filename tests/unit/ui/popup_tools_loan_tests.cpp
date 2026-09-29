// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_tools_loan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiPopupToolsTests;

class UiPopupToolsLoanTests : public PopupToolsFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RevisionArmingComboSource final : public IListDataSource{
public:
    RevisionArmingComboSource(PopupToolsSource& menuSource, ComboState& combo)
        : m_menuSource(menuSource)
        , m_combo(combo)
    {
        m_rows.generation = 1501u;
    }


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return m_rows.instanceGeneration(); }
    [[nodiscard]] virtual u64 revision()const override{ return m_rows.revision(); }
    [[nodiscard]] virtual u64 rowCount()const override{ return m_rows.rowCount(); }
    [[nodiscard]] virtual u64 key(const u64 index)const override{ return m_rows.key(index); }
    [[nodiscard]] virtual bool indexOf(const u64 key, u64& index)const override{ return m_rows.indexOf(key, index); }
    [[nodiscard]] virtual bool findEnabled(const u64 start, const bool reverse, u64& index)const override{
        return m_rows.findEnabled(start, reverse, index);
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        ++textCalls;
        if(!armed){
            m_menuSource.closeComboOnRevision = &m_combo;
            armed = true;
        }
        return m_rows.text(index);
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{ return m_rows.enabled(index); }


private:
    PopupToolsSource& m_menuSource;
    ComboState& m_combo;
    PopupToolsSource m_rows;


public:
    mutable u64 textCalls = 0u;
    mutable bool armed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupToolsLoanTests, ClosingAUserPopupAfterTooltipDeclarationHidesHelpWithoutOrphanOverlay){
    m_plainPopup.open();
    ASSERT_TRUE(acceptPopupTooltip(1u));
    ASSERT_TRUE(moveToAnchor("plain"));
    ASSERT_TRUE(acceptPopupTooltip(2u));
    ASSERT_TRUE(m_tooltip.visible());
    ASSERT_TRUE(m_context.input().hasPopup());
    ASSERT_TRUE(begin(3u));
    PopupOptions options;
    options.anchor = { 40.0f, 40.0f, 40.0f, 24.0f };
    options.size = { 320.0f, 220.0f };
    ASSERT_TRUE(m_builder.beginPopup("plain", m_plainPopup, options));
    EXPECT_FALSE(m_builder.button("anchor", "Popup anchor", m_anchorOptions));
    ASSERT_TRUE(m_builder.tooltip("tip", "anchor", "Overlay help", m_tooltip, m_tooltipOptions));
    ASSERT_TRUE(m_tooltip.visible());
    EXPECT_EQ(m_context.input().layoutGeneration(), 2u);
    m_plainPopup.close();
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_TRUE(m_builder.balanced());
    EXPECT_FALSE(m_tooltip.visible());
    EXPECT_EQ(m_context.input().layoutGeneration(), 2u);
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_TRUE(snapshot.commands().empty());
    EXPECT_TRUE(snapshot.vertices().empty());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiPopupToolsLoanTests, MenuRowCallbackCannotReplaceAnAlreadyPaintedOrdinaryListAndPublish){
    ASSERT_TRUE(acceptTools(1u, false, false));
    PopupToolsSource ordinarySource;
    ordinarySource.generation = 1502u;
    ListState ordinary;
    ordinary.select(1u);
    ASSERT_TRUE(m_menu.open({ 40.0f, 60.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 360.0f }));
    ListOptions options;
    options.width = { LayoutSizePolicy::Fixed, 220.0f };
    options.height = { LayoutSizePolicy::Fixed, 168.0f };
    options.rowHeight = 28.0f;
    ASSERT_TRUE(m_builder.virtualList("ordinary", ordinarySource, ordinary, options).valid);
    EXPECT_FALSE(m_builder.button("anchor", "Anchor", m_anchorOptions));
    ASSERT_TRUE(m_builder.contextMenu("menu", "anchor", m_source, m_menu, m_menuOptions).valid);
    const u64 inputGeneration = ordinary.inputGeneration();
    m_source.selectListOnText = &ordinary;
    m_source.selectListKey = 4u;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_source.selectListOnText, nullptr);
    EXPECT_EQ(ordinary.selectedKey(), 4u);
    EXPECT_EQ(ordinary.cursorKey(), 4u);
    EXPECT_NE(ordinary.inputGeneration(), inputGeneration);
    EXPECT_GT(ordinary.placement().bounds.width, 0.0f);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiPopupToolsLoanTests, LateMenuMetadataCallbackCannotCloseAPaintedComboAndPublish){
    ASSERT_TRUE(acceptTools(1u, false, false));
    ComboState combo;
    RevisionArmingComboSource comboSource(m_source, combo);
    combo.open();
    ASSERT_EQ(combo.selectedKey(), 0u);
    ASSERT_TRUE(m_menu.open({ 40.0f, 60.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 360.0f }));
    EXPECT_FALSE(m_builder.button("anchor", "Anchor", m_anchorOptions));
    ASSERT_TRUE(m_builder.contextMenu("menu", "anchor", m_source, m_menu, m_menuOptions).valid);
    ComboOptions options;
    options.width = { LayoutSizePolicy::Fixed, 220.0f };
    options.height = { LayoutSizePolicy::Fixed, 32.0f };
    options.popupHeight = 176.0f;
    options.rowHeight = 28.0f;
    ASSERT_TRUE(m_builder.comboBox("combo", comboSource, combo, options).valid);
    ASSERT_EQ(comboSource.textCalls, 0u);
    ASSERT_EQ(m_source.closeComboOnRevision, nullptr);
    const u64 inputGeneration = combo.inputGeneration();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(comboSource.armed);
    EXPECT_GT(comboSource.textCalls, 0u);
    EXPECT_EQ(m_source.closeComboOnRevision, nullptr);
    EXPECT_FALSE(combo.isOpen());
    EXPECT_NE(combo.inputGeneration(), inputGeneration);
    EXPECT_GT(combo.placement().bounds.width, 0.0f);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

