// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/edit_click_tracker.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_click_tracker_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

constexpr UiTextEditOwner s_Owner{ { 11u }, 2u, 3u };
constexpr UiTextEditOwner s_OtherOwner{ { 12u }, 2u, 3u };
constexpr Ui::PopupToken s_Popup{ { 21u }, 1u, 1u, 1u };
constexpr Ui::PopupToken s_ReopenedPopup{ { 21u }, 1u, 1u, 2u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiEditClickTracker, ExactTimeAndDistanceBoundsRequireAReleasedFirstPress){
    UiEditClickTracker tracker;
    EXPECT_EQ(tracker.press(s_Owner, s_Popup, 4u, 5u, { 10.0f, 20.0f }, 1000u, false), UiEditClickKind::Caret);
    EXPECT_EQ(tracker.press(s_Owner, s_Popup, 4u, 5u, { 14.0f, 20.0f }, 1500u, false), UiEditClickKind::Caret);
    tracker.release(s_Owner);
    EXPECT_EQ(tracker.press(s_Owner, s_Popup, 4u, 5u, { 18.0f, 20.0f }, 2000u, false), UiEditClickKind::Word);
    tracker.release(s_Owner);
    EXPECT_EQ(tracker.press(s_Owner, s_Popup, 4u, 5u, { 18.0f, 20.0f }, 2001u, false), UiEditClickKind::Caret);
}

TEST(UiEditClickTracker, ClockReversalAndMovementCannotManufactureASecondClick){
    UiEditClickTracker tracker;
    EXPECT_EQ(tracker.press(s_Owner, {}, 1u, 1u, { 0.0f, 0.0f }, Limit<u64>::s_Max - 1u, false), UiEditClickKind::Caret);
    tracker.release(s_Owner);
    EXPECT_EQ(tracker.press(s_Owner, {}, 1u, 1u, { 0.0f, 0.0f }, 2u, false), UiEditClickKind::Caret);
    tracker.move({ 4.1f, 0.0f });
    tracker.release(s_Owner);
    EXPECT_EQ(tracker.press(s_Owner, {}, 1u, 1u, { 0.0f, 0.0f }, 3u, false), UiEditClickKind::Caret);
    tracker.release(s_Owner);
    EXPECT_EQ(tracker.press(s_Owner, {}, 1u, 1u, { 0.0f, 0.0f }, 504u, false), UiEditClickKind::Caret);
}

TEST(UiEditClickTracker, PopupReopenModelReplacementFocusLossAndShiftFenceTheSequence){
    UiEditClickTracker tracker;
    EXPECT_EQ(tracker.press(s_Owner, s_Popup, 1u, 1u, {}, 100u, false), UiEditClickKind::Caret);
    tracker.release(s_Owner);
    EXPECT_EQ(tracker.press(s_Owner, s_ReopenedPopup, 1u, 1u, {}, 120u, false), UiEditClickKind::Caret);
    tracker.release(s_Owner);
    EXPECT_EQ(tracker.press(s_Owner, s_ReopenedPopup, 1u, 2u, {}, 140u, false), UiEditClickKind::Caret);
    tracker.release(s_Owner);
    tracker.retainFocus(&s_OtherOwner);
    EXPECT_EQ(tracker.press(s_Owner, s_ReopenedPopup, 1u, 2u, {}, 160u, false), UiEditClickKind::Caret);
    tracker.release(s_Owner);
    EXPECT_EQ(tracker.press(s_Owner, s_ReopenedPopup, 1u, 2u, {}, 180u, true), UiEditClickKind::Caret);
    tracker.release(s_Owner);
    EXPECT_EQ(tracker.press(s_Owner, s_ReopenedPopup, 1u, 2u, {}, 200u, false), UiEditClickKind::Caret);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

