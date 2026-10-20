// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"
#include "widget_id_assertion.h"

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

constexpr AStringView s_BeforeKey = "before";
constexpr AStringView s_ApplyKey = "apply";
constexpr AStringView s_AfterKey = "after";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] static PopupOptions ParentOptions()noexcept{
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 280.0f, 360.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions ChildOptions()noexcept{
        PopupOptions options;
        options.anchor = { 460.0f, 80.0f, 80.0f, 24.0f };
        options.size = { 300.0f, 300.0f };
        return options;
    }

    [[nodiscard]] static WidgetOptions Control(const f32 width = 120.0f)noexcept{
        WidgetOptions options;
        options.width = { LayoutSizePolicy::Fixed, width };
        options.height = { LayoutSizePolicy::Fixed, 30.0f };
        return options;
    }


protected:
    virtual void SetUp()override{
        WidgetFixture::SetUp();
        m_parent.open();
        m_child.open();
    }

    [[nodiscard]] WidgetId parentId()const{ return UiTestSupport::RequireWidgetId(MakeWidgetId(UiTestSupport::RequireWidgetId(MakeRootId(m_root)), s_ParentPopupKey)); }

    [[nodiscard]] WidgetId childId()const{ return UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_ChildPopupKey)); }

    [[nodiscard]] bool beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup(s_ParentPopupKey, m_parent, ParentOptions());
    }

    [[nodiscard]] bool finishRoot(){ return m_context.endRoot() && m_context.finishFrame(); }

    [[nodiscard]] bool prepareButtons(const u64 generation){
        if(!beginParent(generation))
            return false;
        m_beforeActivated = m_builder.button(s_BeforeKey, "Before", Control());
        if(m_builder.beginPopup(s_ChildPopupKey, m_child, ChildOptions())){
            m_childActivated = m_builder.button(s_ApplyKey, "Child", Control());
            if(!m_builder.endPopup())
                return false;
        }
        else if(m_context.failed())
            return false;
        m_afterActivated = m_builder.button(s_AfterKey, "After", Control());
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
    EXPECT_FALSE(m_builder.button(s_BeforeKey, "Before", Control()));
    ASSERT_TRUE(m_builder.beginPopup(s_ChildPopupKey, m_child, ChildOptions()));
    const PopupToken child = m_context.popupToken();
    EXPECT_NE(child, parent);
    EXPECT_FALSE(m_builder.button(s_ApplyKey, "Child", Control()));
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_context.popupToken(), parent);
    EXPECT_FALSE(m_builder.balanced());
    EXPECT_FALSE(m_builder.button(s_AfterKey, "After", Control()));
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_TRUE(m_builder.balanced());
    EXPECT_FALSE(m_context.popupToken().valid());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));

    const HitTarget* before = target(UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_BeforeKey)));
    const HitTarget* after = target(UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_AfterKey)));
    const HitTarget* apply = target(UiTestSupport::RequireWidgetId(MakeWidgetId(childId(), s_ApplyKey)));
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_EQ(before->popup, parent);
    EXPECT_EQ(after->popup, parent);
    EXPECT_EQ(apply->popup, child);
    EXPECT_FLOAT_EQ(after->rectangle.x, before->rectangle.x);
    EXPECT_FLOAT_EQ(after->rectangle.y, VectorGetX(VectorAdd(VectorAdd(VectorReplicate(before->rectangle.y), VectorReplicate(before->rectangle.height)), VectorReplicate(m_builder.style().gap))));
    EXPECT_GT(apply->rectangle.x, VectorGetX(VectorAdd(VectorReplicate(m_parent.placement().bounds.x), VectorReplicate(m_parent.placement().bounds.width))));
    EXPECT_EQ(m_context.input().focus(), apply->id);
}

TEST_F(UiNestedPopupBuilderTests, ChildOpenedInsideARowRestoresTheSameParentContainer){
    ASSERT_TRUE(beginParent(1u));
    ContainerOptions rowOptions;
    rowOptions.width = { LayoutSizePolicy::Fixed, 240.0f };
    rowOptions.height = { LayoutSizePolicy::Fixed, 30.0f };
    rowOptions.gap = 5.0f;
    ASSERT_TRUE(m_builder.beginRow("row", rowOptions));
    const WidgetId row = UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), "row"));
    EXPECT_FALSE(m_builder.button(s_BeforeKey, "Before", Control(100.0f)));
    ASSERT_TRUE(m_builder.beginPopup(s_ChildPopupKey, m_child, ChildOptions()));
    EXPECT_FALSE(m_builder.button(s_ApplyKey, "Child", Control()));
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_context.scopeId(), row);
    EXPECT_FALSE(m_builder.button(s_AfterKey, "After", Control(100.0f)));
    ASSERT_TRUE(m_builder.endContainer());
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));

    const HitTarget* before = target(UiTestSupport::RequireWidgetId(MakeWidgetId(row, s_BeforeKey)));
    const HitTarget* after = target(UiTestSupport::RequireWidgetId(MakeWidgetId(row, s_AfterKey)));
    const HitTarget* apply = target(UiTestSupport::RequireWidgetId(MakeWidgetId(UiTestSupport::RequireWidgetId(MakeWidgetId(row, s_ChildPopupKey)), s_ApplyKey)));
    ASSERT_NE(before, nullptr);
    ASSERT_NE(after, nullptr);
    ASSERT_NE(apply, nullptr);
    EXPECT_FLOAT_EQ(after->rectangle.x, VectorGetX(VectorAdd(VectorAdd(VectorReplicate(before->rectangle.x), VectorReplicate(before->rectangle.width)), VectorReplicate(rowOptions.gap))));
    EXPECT_FLOAT_EQ(after->rectangle.y, before->rectangle.y);
    EXPECT_EQ(before->popup, after->popup);
    EXPECT_NE(apply->popup, before->popup);
}

TEST_F(UiNestedPopupBuilderTests, ClosedChildBeginLeavesParentScopeAndFollowingControlsIntact){
    m_child.close();
    ASSERT_TRUE(beginParent(1u));
    const PopupToken parent = m_context.popupToken();
    EXPECT_FALSE(m_builder.button(s_BeforeKey, "Before", Control()));
    EXPECT_FALSE(m_builder.beginPopup(s_ChildPopupKey, m_child, ChildOptions()));
    EXPECT_FALSE(m_context.failed());
    EXPECT_EQ(m_context.popupToken(), parent);
    EXPECT_EQ(m_context.scopeId(), parentId());
    EXPECT_FALSE(m_builder.button(s_AfterKey, "After", Control()));
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_AfterKey))), nullptr);
    EXPECT_EQ(target(childId()), nullptr);
    EXPECT_EQ(target(UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_AfterKey)))->popup, parent);
}

TEST_F(UiNestedPopupBuilderTests, ChildPaintEscapesParentClipAndLayersRemainOrdered){
    ASSERT_TRUE(prepareButtons(1u));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_FALSE(snapshot.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* apply = target(UiTestSupport::RequireWidgetId(MakeWidgetId(childId(), s_ApplyKey)));
    ASSERT_NE(apply, nullptr);
    EXPECT_GT(apply->clip.x, VectorGetX(VectorAdd(VectorReplicate(m_parent.placement().bounds.x), VectorReplicate(m_parent.placement().bounds.width))));
    EXPECT_LE(apply->clip.x, apply->rectangle.x);
    EXPECT_LE(apply->clip.y, apply->rectangle.y);
    // Lanes are clip right, clip bottom, rectangle right, rectangle bottom.
    const SIMDVectorSingle edges = VectorAdd(VectorSet(apply->clip.x, apply->clip.y, apply->rectangle.x, apply->rectangle.y), VectorSet(apply->clip.width, apply->clip.height, apply->rectangle.width, apply->rectangle.height));
    EXPECT_GE(VectorGetX(edges), VectorGetZ(edges));
    EXPECT_GE(VectorGetY(edges), VectorGetW(edges));
    ASSERT_NE(target(UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_AfterKey))), nullptr);
    EXPECT_GT(apply->layer, target(UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_AfterKey)))->layer);
    const Point applyCenter = Center(apply->rectangle);
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
    const HitTarget* apply = target(UiTestSupport::RequireWidgetId(MakeWidgetId(childId(), s_ApplyKey)));
    ASSERT_NE(apply, nullptr);
    const Point applyCenter = Center(apply->rectangle);
    click(applyCenter);
    ASSERT_TRUE(acceptButtons(2u));
    EXPECT_TRUE(m_childActivated);
    EXPECT_FALSE(m_beforeActivated);
    EXPECT_FALSE(m_afterActivated);
    m_child.close();
    ASSERT_TRUE(acceptButtons(3u));
    const HitTarget* after = target(UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_AfterKey)));
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(target(UiTestSupport::RequireWidgetId(MakeWidgetId(childId(), s_ApplyKey))), nullptr);
    const Point afterCenter = Center(after->rectangle);
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
        ASSERT_TRUE(m_builder.beginPopup("level", states[index], ParentOptions()));
    }
    states[s_InputMaxPopups].open();
    EXPECT_FALSE(m_builder.beginPopup("level", states[s_InputMaxPopups], ChildOptions()));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiNestedPopupBuilderTests, WrongPanelEndCannotCloseTheChildPopupOrPublishIt){
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup(s_ChildPopupKey, m_child, ChildOptions()));
    EXPECT_FALSE(m_builder.button(s_ApplyKey, "Child", Control()));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiNestedPopupBuilderTests, UnbalancedChildContainerPreventsTheOuterFrameFromPublishing){
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup(s_ChildPopupKey, m_child, ChildOptions()));
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

