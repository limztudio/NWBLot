// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_context_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

struct PopupDeclaration{
    WidgetState owner;
    WidgetState action;
    PopupScope scope;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupContextTests : public testing::Test{
public:
    UiNestedPopupContextTests()
        : m_arena(Name("tests/ui/nested_popup_context"))
        , m_context(m_arena)
    {}


protected:
    [[nodiscard]] bool begin(const u64 generation){
        return m_context.beginFrame(generation) && m_context.beginRoot({ 1401u, 1u });
    }

    [[nodiscard]] PopupDeclaration reserve(const AStringView key,
        const Rect& bounds = { 60.0f, 40.0f, 120.0f, 80.0f }){
        PopupDeclaration result;
        const WidgetState* owner = m_context.declare(key, WidgetKind::Popup);
        EXPECT_NE(owner, nullptr);
        if(!owner)
            return result;
        result.owner = *owner;
        result.scope.token = { owner->id, owner->declarationGeneration, 900u, 1u };
        result.scope.bounds = bounds;
        result.scope.viewport = { 0.0f, 0.0f, 400.0f, 300.0f };
        result.scope.parent = m_context.popupToken();
        EXPECT_TRUE(m_context.registerPopupScope(result.owner, result.scope));
        const WidgetState* action = m_context.declarePart(result.owner, "action", WidgetKind::Button);
        EXPECT_NE(action, nullptr);
        if(action)
            result.action = *action;
        return result;
    }

    [[nodiscard]] bool publish(const PopupDeclaration& popup){
        HitTarget barrier;
        barrier.rectangle = popup.scope.bounds;
        barrier.clip = popup.scope.viewport;
        if(!m_context.addTarget(popup.owner, barrier))
            return false;
        HitTarget action;
        action.rectangle = { popup.scope.bounds.x + 4.0f, popup.scope.bounds.y + 4.0f, 24.0f, 18.0f };
        action.clip = popup.scope.bounds;
        action.focusable = true;
        action.activatable = true;
        return m_context.addTarget(popup.action, action);
    }

    [[nodiscard]] bool finish(){
        return m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_context.input().queue(event));
        return m_context.input().process();
    }

    [[nodiscard]] const HitTarget* target(const WidgetId id)const{
        for(const auto& item : m_context.input().targets()){
            if(item.id == id)
                return &item;
        }
        return nullptr;
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Context m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNestedPopupContextTests, NestedActivationRestoresExactParentScopeAndLayer){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    EXPECT_FALSE(m_context.popupToken().valid());
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    ASSERT_TRUE(publish(parent));
    const PopupDeclaration child = reserve("child", { 180.0f, 120.0f, 100.0f, 70.0f });
    EXPECT_EQ(child.scope.parent, parent.scope.token);
    EXPECT_EQ(m_context.popupToken(), parent.scope.token);
    EXPECT_EQ(m_context.popupLayer(), 1u);
    ASSERT_TRUE(m_context.activatePopupScope(child.scope.token));
    ASSERT_TRUE(publish(child));
    EXPECT_EQ(m_context.popupToken(), child.scope.token);
    EXPECT_EQ(m_context.popupLayer(), 2u);
    ASSERT_TRUE(m_context.endPopupScope(true));
    EXPECT_EQ(m_context.popupToken(), parent.scope.token);
    EXPECT_EQ(m_context.popupLayer(), 1u);
    ASSERT_TRUE(m_context.endPopupScope(true));
    EXPECT_FALSE(m_context.popupToken().valid());
    EXPECT_EQ(m_context.popupLayer(), 0u);
    ASSERT_TRUE(finish());
    EXPECT_FALSE(m_context.input().hasPopup());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_EQ(m_context.input().focus(), child.action.id);
    EXPECT_EQ(m_context.input().hitTest({ 185.0f, 125.0f }), child.action.id);
    EXPECT_FALSE(m_context.input().hitTest({ 65.0f, 45.0f }).valid());
}

TEST_F(UiNestedPopupContextTests, ReservationOrderControlsLayersAcrossReactivationAndReversePaintOrder){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent", { 30.0f, 30.0f, 150.0f, 110.0f });
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    PopupDeclaration first = reserve("first", { 1.0f, 1.0f, 1.0f, 1.0f });
    PopupDeclaration second = reserve("second", { 1.0f, 1.0f, 1.0f, 1.0f });
    EXPECT_EQ(m_context.popupLayer(first.scope.token), 2u);
    EXPECT_EQ(m_context.popupLayer(second.scope.token), 3u);
    ASSERT_TRUE(m_context.endPopupScope(true));
    first.scope.bounds = { 100.0f, 70.0f, 100.0f, 60.0f };
    second.scope.bounds = first.scope.bounds;
    ASSERT_TRUE(m_context.updatePopupScope(first.scope.token, first.scope));
    ASSERT_TRUE(m_context.updatePopupScope(second.scope.token, second.scope));
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    ASSERT_TRUE(publish(parent));
    ASSERT_TRUE(m_context.activatePopupScope(second.scope.token));
    ASSERT_TRUE(publish(second));
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(m_context.activatePopupScope(first.scope.token));
    ASSERT_TRUE(publish(first));
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(m_context.endPopupScope(true));
    EXPECT_EQ(m_context.topPopupToken(), second.scope.token);
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(first.action.id), nullptr);
    ASSERT_NE(target(second.action.id), nullptr);
    EXPECT_EQ(target(first.action.id)->layer, 2u);
    EXPECT_EQ(target(second.action.id)->layer, 3u);
    EXPECT_GT(target(first.action.id)->paintOrder, target(second.action.id)->paintOrder);
    EXPECT_EQ(m_context.input().hitTest({ 105.0f, 75.0f }), second.action.id);
    EXPECT_EQ(m_context.input().focus(), second.action.id);
}

TEST_F(UiNestedPopupContextTests, HiddenAncestorDropsReservedDescendantsAndTheirPublishedTargets){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    ASSERT_TRUE(publish(parent));
    const PopupDeclaration child = reserve("child");
    ASSERT_TRUE(m_context.activatePopupScope(child.scope.token));
    ASSERT_TRUE(publish(child));
    const PopupDeclaration deferred = reserve("deferred");
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(m_context.endPopupScope(false));
    EXPECT_FALSE(m_context.hasPopupScope(parent.scope.token));
    EXPECT_FALSE(m_context.hasPopupScope(child.scope.token));
    EXPECT_FALSE(m_context.hasPopupScope(deferred.scope.token));
    EXPECT_FALSE(m_context.topPopupToken().valid());
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiNestedPopupContextTests, DiscardingInactiveFamilyPreservesAnUnrelatedTopLevelScopeAndItsLayer){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    ASSERT_TRUE(publish(parent));
    const PopupDeclaration child = reserve("child");
    ASSERT_TRUE(m_context.activatePopupScope(child.scope.token));
    ASSERT_TRUE(publish(child));
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(m_context.endPopupScope(true));
    const PopupDeclaration unrelated = reserve("unrelated", { 240.0f, 170.0f, 100.0f, 70.0f });
    ASSERT_TRUE(m_context.activatePopupScope(unrelated.scope.token));
    ASSERT_TRUE(publish(unrelated));
    ASSERT_TRUE(m_context.endPopupScope(true));
    m_context.discardPopupScope(parent.scope.token);
    m_context.discardPopupScope(parent.scope.token);
    EXPECT_FALSE(m_context.failed());
    EXPECT_FALSE(m_context.hasPopupScope(parent.scope.token));
    EXPECT_FALSE(m_context.hasPopupScope(child.scope.token));
    EXPECT_TRUE(m_context.hasPopupScope(unrelated.scope.token));
    EXPECT_EQ(m_context.popupLayer(unrelated.scope.token), 3u);
    EXPECT_EQ(m_context.topPopupToken(), unrelated.scope.token);
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_EQ(m_context.input().targets().size(), 2u);
    EXPECT_EQ(m_context.input().focus(), unrelated.action.id);
}

TEST_F(UiNestedPopupContextTests, DiscardingInactiveChildKeepsItsActiveParentBalanced){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    const PopupDeclaration child = reserve("child");
    m_context.discardPopupScope(child.scope.token);
    EXPECT_FALSE(m_context.failed());
    EXPECT_EQ(m_context.popupToken(), parent.scope.token);
    EXPECT_EQ(m_context.popupLayer(), 1u);
    ASSERT_TRUE(publish(parent));
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_EQ(m_context.input().focus(), parent.action.id);
    EXPECT_EQ(target(child.owner.id), nullptr);
}

TEST_F(UiNestedPopupContextTests, DiscardingAnActiveAncestorFailsWithoutRemovingItsActivation){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    const PopupDeclaration child = reserve("child");
    ASSERT_TRUE(m_context.activatePopupScope(child.scope.token));
    m_context.discardPopupScope(parent.scope.token);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.popupToken(), child.scope.token);
    EXPECT_TRUE(m_context.hasPopupScope(parent.scope.token));
    EXPECT_TRUE(m_context.hasPopupScope(child.scope.token));
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiNestedPopupContextTests, ChildCannotActivateWithoutItsExactCurrentParent){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    const PopupDeclaration child = reserve("child");
    ASSERT_TRUE(m_context.endPopupScope(true));
    EXPECT_FALSE(m_context.activatePopupScope(child.scope.token));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.popupToken().valid());
    EXPECT_FALSE(m_context.finishFrame());
}

TEST_F(UiNestedPopupContextTests, RegistrationRejectsAnAncestorTokenFromAnotherOpenEpoch){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    const WidgetState* declared = m_context.declare("child", WidgetKind::Popup);
    ASSERT_NE(declared, nullptr);
    const WidgetState child = *declared;
    PopupScope invalid = parent.scope;
    invalid.token = { child.id, child.declarationGeneration, 901u, 1u };
    invalid.parent = parent.scope.token;
    ++invalid.parent.openGeneration;
    EXPECT_FALSE(m_context.registerPopupScope(child, invalid));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.popupToken(), parent.scope.token);
    EXPECT_FALSE(m_context.hasPopupScope(invalid.token));
}

TEST_F(UiNestedPopupContextTests, FinalGeometryCannotRebindParentTokenOrReservedLayer){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    const PopupDeclaration child = reserve("child");
    PopupScope invalid = child.scope;
    invalid.layer = 1u;
    EXPECT_FALSE(m_context.updatePopupScope(child.scope.token, invalid));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.popupLayer(child.scope.token), 2u);
    m_context.abandonFrame();
    ASSERT_TRUE(begin(2u));
    const PopupDeclaration nextParent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(nextParent.scope.token));
    const PopupDeclaration nextChild = reserve("child");
    invalid = nextChild.scope;
    invalid.parent = {};
    EXPECT_FALSE(m_context.updatePopupScope(nextChild.scope.token, invalid));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.popupToken(), nextParent.scope.token);
}

TEST_F(UiNestedPopupContextTests, InvalidFinalGeometryCannotPublishThePreparedFamily){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    PopupScope invalid = parent.scope;
    invalid.bounds.width = 0.0f;
    EXPECT_FALSE(m_context.updatePopupScope(parent.scope.token, invalid));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_context.hasPopupScope(parent.scope.token));
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 0u);
}

TEST_F(UiNestedPopupContextTests, AcceptedChildActionCanOnlyBeConsumedDuringThatChildActivation){
    ASSERT_TRUE(begin(1u));
    const PopupDeclaration parent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(parent.scope.token));
    ASSERT_TRUE(publish(parent));
    const PopupDeclaration child = reserve("child", { 180.0f, 120.0f, 100.0f, 70.0f });
    ASSERT_TRUE(m_context.activatePopupScope(child.scope.token));
    ASSERT_TRUE(publish(child));
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 185.0f, 125.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 185.0f, 125.0f } }).pointerConsumed);
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(begin(2u));
    const PopupDeclaration nextParent = reserve("parent");
    ASSERT_TRUE(m_context.activatePopupScope(nextParent.scope.token));
    const PopupDeclaration nextChild = reserve("child", child.scope.bounds);
    EXPECT_EQ(nextChild.action.id, child.action.id);
    EXPECT_FALSE(m_context.takeActivation(nextChild.action, true));
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(m_context.activatePopupScope(nextChild.scope.token));
    EXPECT_TRUE(m_context.takeActivation(nextChild.action, true));
    EXPECT_FALSE(m_context.takeActivation(nextChild.action, true));
}

TEST_F(UiNestedPopupContextTests, EighthNestedActivationIsBoundedAndNinthReservationFails){
    ASSERT_TRUE(begin(1u));
    const AStringView keys[]{ "popup0", "popup1", "popup2", "popup3", "popup4", "popup5", "popup6", "popup7" };
    for(usize index = 0u; index < s_InputMaxPopups; ++index){
        const PopupDeclaration popup = reserve(keys[index]);
        ASSERT_TRUE(m_context.activatePopupScope(popup.scope.token));
        ASSERT_TRUE(publish(popup));
        EXPECT_EQ(m_context.popupLayer(), static_cast<u32>(index + 1u));
    }
    const WidgetState* declared = m_context.declare("overflow", WidgetKind::Popup);
    ASSERT_NE(declared, nullptr);
    const WidgetState overflow = *declared;
    PopupScope scope;
    scope.token = { overflow.id, overflow.declarationGeneration, 901u, 1u };
    scope.parent = m_context.popupToken();
    scope.bounds = { 60.0f, 40.0f, 120.0f, 80.0f };
    scope.viewport = { 0.0f, 0.0f, 400.0f, 300.0f };
    EXPECT_FALSE(m_context.registerPopupScope(overflow, scope));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

