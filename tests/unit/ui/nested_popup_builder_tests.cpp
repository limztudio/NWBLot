// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupBuilderTests : public WidgetFixture{
protected:
    virtual void SetUp()override{
        WidgetFixture::SetUp();
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
    const Point applyCenter{ apply->rectangle.x + apply->rectangle.width * 0.5f, apply->rectangle.y + apply->rectangle.height * 0.5f };
    EXPECT_EQ(m_context.input().hitTest(applyCenter), apply->id);
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
    const Point applyCenter{ apply->rectangle.x + apply->rectangle.width * 0.5f, apply->rectangle.y + apply->rectangle.height * 0.5f };
    click(applyCenter);
    ASSERT_TRUE(acceptButtons(2u));
    EXPECT_TRUE(m_childActivated);
    EXPECT_FALSE(m_beforeActivated);
    EXPECT_FALSE(m_afterActivated);
    m_child.close();
    ASSERT_TRUE(acceptButtons(3u));
    const HitTarget* after = target(MakeWidgetId(parentId(), "after"));
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(target(MakeWidgetId(childId(), "apply")), nullptr);
    const Point afterCenter{ after->rectangle.x + after->rectangle.width * 0.5f, after->rectangle.y + after->rectangle.height * 0.5f };
    click(afterCenter);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

