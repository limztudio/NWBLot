// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_fixture.h"
#include "search_combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiPopupToolsTests;
using namespace NWB::UiSearchComboTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupBuilderTests : public SearchFixture{
public:
    UiNestedPopupBuilderTests()
        : m_edit(m_arena)
    {}


protected:
    virtual void SetUp()override{
        SearchFixture::SetUp();
        m_source.count = 5u;
        m_searchSource.full.count = 5u;
        m_parent.open();
        m_child.open();
    }

    [[nodiscard]] static PopupOptions parentOptions(){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 280.0f, 360.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions childOptions(){
        PopupOptions options;
        options.anchor = { 460.0f, 80.0f, 80.0f, 24.0f };
        options.size = { 300.0f, 300.0f };
        return options;
    }

    [[nodiscard]] static WidgetOptions control(const f32 width = 120.0f){
        WidgetOptions options;
        options.width = { LayoutSizePolicy::Fixed, width };
        options.height = { LayoutSizePolicy::Fixed, 30.0f };
        return options;
    }

    [[nodiscard]] static ListOptions listOptions(){
        ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, 160.0f };
        options.rowHeight = 24.0f;
        return options;
    }

    [[nodiscard]] WidgetId parentId()const{ return MakeWidgetId(MakeRootId(m_root), "parent"); }
    [[nodiscard]] WidgetId childId()const{ return MakeWidgetId(parentId(), "child"); }

    [[nodiscard]] bool beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup("parent", m_parent, parentOptions());
    }

    [[nodiscard]] bool finishRoot(){ return m_context.endRoot() && m_context.finishFrame(); }

    [[nodiscard]] bool prepareButtons(const u64 generation){
        if(!beginParent(generation))
            return false;
        m_beforeActivated = m_builder.button("before", "Before", control());
        if(m_builder.beginPopup("child", m_child, childOptions())){
            m_childActivated = m_builder.button("apply", "Child", control());
            if(!m_builder.endPopup())
                return false;
        }
        else if(m_context.failed())
            return false;
        m_afterActivated = m_builder.button("after", "After", control());
        return m_builder.endPopup() && finishRoot();
    }

    [[nodiscard]] bool acceptButtons(const u64 generation){
        return prepareButtons(generation) && m_context.commitFrame(generation);
    }


protected:
    PopupState m_parent;
    PopupState m_child;
    ListState m_list;
    EditModel m_edit;
    EditBoxState m_editor;
    ContextMenuState m_menu;
    PopupToolsSource m_menuSource;
    bool m_beforeActivated = false;
    bool m_afterActivated = false;
    bool m_childActivated = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNestedPopupBuilderTests, ChildLayoutDoesNotConsumeParentSpaceAndRestoresItsPopupToken){
    ASSERT_TRUE(beginParent(1u));
    const PopupToken parent = m_context.popupToken();
    EXPECT_FALSE(m_builder.button("before", "Before", control()));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    const PopupToken child = m_context.popupToken();
    EXPECT_NE(child, parent);
    EXPECT_FALSE(m_builder.button("apply", "Child", control()));
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_context.popupToken(), parent);
    EXPECT_FALSE(m_builder.balanced());
    EXPECT_FALSE(m_builder.button("after", "After", control()));
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_TRUE(m_builder.balanced());
    EXPECT_FALSE(m_context.popupToken().valid());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));

    const HitTarget* before = target(MakeWidgetId(parentId(), "before"));
    const HitTarget* after = target(MakeWidgetId(parentId(), "after"));
    const HitTarget* apply = target(MakeWidgetId(childId(), "apply"));
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_EQ(before->popup, parent);
    EXPECT_EQ(after->popup, parent);
    EXPECT_EQ(apply->popup, child);
    EXPECT_FLOAT_EQ(after->rectangle.x, before->rectangle.x);
    EXPECT_FLOAT_EQ(after->rectangle.y, before->rectangle.y + before->rectangle.height + m_builder.style().gap);
    EXPECT_GT(apply->rectangle.x, m_parent.placement().bounds.x + m_parent.placement().bounds.width);
    EXPECT_EQ(m_context.input().focus(), apply->id);
}

TEST_F(UiNestedPopupBuilderTests, ChildOpenedInsideARowRestoresTheSameParentContainer){
    ASSERT_TRUE(beginParent(1u));
    ContainerOptions rowOptions;
    rowOptions.width = { LayoutSizePolicy::Fixed, 240.0f };
    rowOptions.height = { LayoutSizePolicy::Fixed, 30.0f };
    rowOptions.gap = 5.0f;
    ASSERT_TRUE(m_builder.beginRow("row", rowOptions));
    const WidgetId row = MakeWidgetId(parentId(), "row");
    EXPECT_FALSE(m_builder.button("before", "Before", control(100.0f)));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    EXPECT_FALSE(m_builder.button("apply", "Child", control()));
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_context.scopeId(), row);
    EXPECT_FALSE(m_builder.button("after", "After", control(100.0f)));
    ASSERT_TRUE(m_builder.endContainer());
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));

    const HitTarget* before = target(MakeWidgetId(row, "before"));
    const HitTarget* after = target(MakeWidgetId(row, "after"));
    const HitTarget* apply = target(MakeWidgetId(MakeWidgetId(row, "child"), "apply"));
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_FLOAT_EQ(after->rectangle.x, before->rectangle.x + before->rectangle.width + rowOptions.gap);
    EXPECT_FLOAT_EQ(after->rectangle.y, before->rectangle.y);
    EXPECT_EQ(before->popup, after->popup);
    EXPECT_NE(apply->popup, before->popup);
}

TEST_F(UiNestedPopupBuilderTests, ClosedChildBeginLeavesParentScopeAndFollowingControlsIntact){
    m_child.close();
    ASSERT_TRUE(beginParent(1u));
    const PopupToken parent = m_context.popupToken();
    EXPECT_FALSE(m_builder.button("before", "Before", control()));
    EXPECT_FALSE(m_builder.beginPopup("child", m_child, childOptions()));
    EXPECT_FALSE(m_context.failed());
    EXPECT_EQ(m_context.popupToken(), parent);
    EXPECT_EQ(m_context.scopeId(), parentId());
    EXPECT_FALSE(m_builder.button("after", "After", control()));
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(MakeWidgetId(parentId(), "after")), nullptr);
    EXPECT_EQ(target(childId()), nullptr);
    EXPECT_EQ(target(MakeWidgetId(parentId(), "after"))->popup, parent);
}

TEST_F(UiNestedPopupBuilderTests, ChildPaintEscapesParentClipAndLayersRemainOrdered){
    ASSERT_TRUE(prepareButtons(1u));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_FALSE(snapshot.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* apply = target(MakeWidgetId(childId(), "apply"));
    ASSERT_NE(apply, nullptr);
    EXPECT_GT(apply->clip.x, m_parent.placement().bounds.x + m_parent.placement().bounds.width);
    EXPECT_LE(apply->clip.x, apply->rectangle.x);
    EXPECT_LE(apply->clip.y, apply->rectangle.y);
    EXPECT_GE(apply->clip.x + apply->clip.width, apply->rectangle.x + apply->rectangle.width);
    EXPECT_GE(apply->clip.y + apply->clip.height, apply->rectangle.y + apply->rectangle.height);
    ASSERT_NE(target(MakeWidgetId(parentId(), "after")), nullptr);
    EXPECT_GT(apply->layer, target(MakeWidgetId(parentId(), "after"))->layer);
    EXPECT_EQ(m_context.input().hitTest(Center(apply->rectangle)), apply->id);
    u32 previousLayer = 0u;
    bool childPainted = false;
    for(const DrawCommand& command : snapshot.commands()){
        EXPECT_GE(command.layer, previousLayer);
        previousLayer = command.layer;
        childPainted |= command.layer == apply->layer;
    }
    EXPECT_TRUE(childPainted);
}

TEST_F(UiNestedPopupBuilderTests, ChildActionLeavesBothParentControlsAvailableOnLaterDeclarations){
    ASSERT_TRUE(acceptButtons(1u));
    const HitTarget* apply = target(MakeWidgetId(childId(), "apply"));
    ASSERT_NE(apply, nullptr);
    click(Center(apply->rectangle));
    ASSERT_TRUE(acceptButtons(2u));
    EXPECT_TRUE(m_childActivated);
    EXPECT_FALSE(m_beforeActivated);
    EXPECT_FALSE(m_afterActivated);
    m_child.close();
    ASSERT_TRUE(acceptButtons(3u));
    const HitTarget* after = target(MakeWidgetId(parentId(), "after"));
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(target(MakeWidgetId(childId(), "apply")), nullptr);
    click(Center(after->rectangle));
    ASSERT_TRUE(acceptButtons(4u));
    EXPECT_TRUE(m_afterActivated);
    EXPECT_FALSE(m_beforeActivated);
    EXPECT_TRUE(m_parent.isOpen());
}

TEST_F(UiNestedPopupBuilderTests, NestedScopeCapacityRejectsTheNextChildWithoutPublishingTheCandidate){
    Array<PopupState, s_InputMaxPopups + 1u> states;
    ASSERT_TRUE(begin(1u));
    for(usize index = 0u; index < s_InputMaxPopups; ++index){
        states[index].open();
        ASSERT_TRUE(m_builder.beginPopup("level", states[index], parentOptions()));
    }
    states[s_InputMaxPopups].open();
    EXPECT_FALSE(m_builder.beginPopup("level", states[s_InputMaxPopups], childOptions()));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiNestedPopupBuilderTests, WrongPanelEndCannotCloseTheChildPopupOrPublishIt){
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    EXPECT_FALSE(m_builder.button("apply", "Child", control()));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiNestedPopupBuilderTests, UnbalancedChildContainerPreventsTheOuterFrameFromPublishing){
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    ASSERT_TRUE(m_builder.beginColumn("unbalanced"));
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.endRoot());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiNestedPopupBuilderTests, NestedEditAndListPublishTheirChildGeometryAndExactPopupToken){
    m_builder.setEditHost(nullptr);
    ASSERT_TRUE(m_edit.setText("Nested editor"));
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    const PopupToken child = m_context.popupToken();
    EditBoxOptions editOptions;
    editOptions.width = { LayoutSizePolicy::Fixed, 220.0f };
    editOptions.height = { LayoutSizePolicy::Fixed, 32.0f };
    ASSERT_TRUE(m_builder.editBox("edit", m_edit, m_editor, editOptions).valid);
    ASSERT_TRUE(m_builder.virtualList("list", m_source, m_list, listOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_FALSE(m_builder.button("after", "Parent after", control()));
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));

    const HitTarget* editor = target(MakeWidgetId(childId(), "edit"));
    const HitTarget* list = target(MakeWidgetId(childId(), "list"));
    ASSERT_NE(editor, nullptr);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(editor->popup, child);
    EXPECT_EQ(list->popup, child);
    EXPECT_EQ(editor->layer, list->layer);
    EXPECT_GT(editor->layer, target(MakeWidgetId(parentId(), "after"))->layer);
    ExpectRect(m_editor.placement.bounds, editor->rectangle);
    EXPECT_GT(m_list.placement().viewport.width, 0.0f);
    EXPECT_GE(list->rectangle.y, editor->rectangle.y + editor->rectangle.height);
}

TEST_F(UiNestedPopupBuilderTests, ComboInsideTheChildAnchorsItsOwnPopupToTheArrangedField){
    m_state.open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    const PopupToken child = m_context.popupToken();
    const ComboResult result = m_builder.comboBox("combo", m_source, m_state, Options());
    ASSERT_TRUE(result.valid);
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_FALSE(m_builder.button("after", "Parent after", control()));
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));

    const WidgetId fieldId = MakeWidgetId(childId(), "combo");
    const HitTarget* field = target(fieldId);
    const HitTarget* rows = target(MakeWidgetId(fieldId, "rows"));
    ASSERT_NE(field, nullptr);
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(field->popup, child);
    EXPECT_EQ(rows->popup.widget, MakeWidgetId(fieldId, "popup"));
    EXPECT_NE(rows->popup, child);
    EXPECT_GT(rows->layer, field->layer);
    ExpectRect(m_state.bounds(), field->rectangle);
    EXPECT_FLOAT_EQ(m_state.placement().bounds.x, field->rectangle.x);
    EXPECT_GE(m_state.placement().bounds.y, field->rectangle.y + field->rectangle.height);
    EXPECT_EQ(m_context.input().focus(), rows->id);
}

TEST_F(UiNestedPopupBuilderTests, SearchInsideTheChildLendsAnExplicitQueryPopupAndPublishesItsPlacement){
    m_search.combo().open();
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    const PopupToken child = m_context.popupToken();
    const SearchComboResult result = m_builder.searchComboBox("search", m_searchSource, m_search, options());
    ASSERT_TRUE(result.combo.valid);
    EXPECT_EQ(m_host.loanContextPopup, child);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));

    const WidgetId fieldId = MakeWidgetId(childId(), "search");
    const HitTarget* field = target(fieldId);
    const HitTarget* query = target(MakeWidgetId(fieldId, "query"));
    const HitTarget* rows = target(MakeWidgetId(fieldId, "rows"));
    ASSERT_NE(field, nullptr);
    ASSERT_NE(query, nullptr);
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(field->popup, child);
    EXPECT_EQ(query->popup, rows->popup);
    EXPECT_EQ(m_host.lastPopup, query->popup);
    EXPECT_EQ(m_host.publishContextPopup, query->popup);
    EXPECT_EQ(query->keyboardOwner, rows->id);
    EXPECT_EQ(query->layer, rows->layer);
    EXPECT_GT(query->layer, field->layer);
    ExpectRect(m_search.editorState().placement.bounds, query->rectangle);
    EXPECT_EQ(m_host.legacyLoans, 0u);
    EXPECT_EQ(m_context.input().focus(), query->id);
}

TEST_F(UiNestedPopupBuilderTests, ContextMenuInsideTheChildUsesItsOwnHigherPopupAndStableCommandKeys){
    ASSERT_TRUE(m_menu.open({ 510.0f, 170.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, childOptions()));
    const PopupToken child = m_context.popupToken();
    EXPECT_FALSE(m_builder.button("anchor", "Commands", control()));
    const ContextMenuResult result = m_builder.contextMenu("menu", "anchor", m_menuSource, m_menu);
    ASSERT_TRUE(result.valid);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));

    const WidgetId menu = MakeWidgetId(childId(), "menu");
    const HitTarget* anchor = target(MakeWidgetId(childId(), "anchor"));
    const HitTarget* rows = target(MakeWidgetId(menu, "rows"));
    ASSERT_NE(anchor, nullptr);
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(anchor->popup, child);
    EXPECT_TRUE(anchor->contextMenu);
    EXPECT_EQ(rows->popup.widget, MakeWidgetId(menu, "popup"));
    EXPECT_GT(rows->layer, anchor->layer);
    EXPECT_EQ(m_context.input().focus(), rows->id);
    EXPECT_EQ(m_menu.cursorKey(), 1u);
    EXPECT_FALSE(result.activated);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

