// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/tooltip.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_tooltip_behavior_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

static_assert(!IsConstructible_V<TooltipState, const TooltipState&>);
static_assert(!IsConstructible_V<TooltipState, TooltipState&&>);

inline constexpr WidgetId s_Anchor{ 201u };
inline constexpr u64 s_Declaration = 17u;
inline constexpr PopupToken s_Popup{ WidgetId{ 301u }, 19u, 23u, 7u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiTooltipBehaviorTests, StatesStartHiddenWithDistinctNonzeroInstanceLifetimes){
    TooltipState first;
    TooltipState second;
    EXPECT_NE(first.instanceGeneration(), 0u);
    EXPECT_NE(second.instanceGeneration(), 0u);
    EXPECT_NE(first.instanceGeneration(), second.instanceGeneration());
    EXPECT_EQ(first.revision(), 1u);
    EXPECT_FALSE(first.visible());
    EXPECT_FLOAT_EQ(first.placement().bounds.width, 0.0f);
    EXPECT_FLOAT_EQ(first.placement().bounds.height, 0.0f);
}

TEST(UiTooltipBehaviorTests, NewHoverDoesNotCountTimeFromBeforeTheBindingWasKnown){
    TooltipState state;
    const TooltipOptions options;
    const u64 revision = state.revision();
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    EXPECT_GT(state.revision(), revision);
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, ContiguousHoverAppearsAtTheExactDelayBoundary){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    for(u32 frame = 0u; frame < 3u; ++frame){
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.125f, options));
        EXPECT_FALSE(state.visible());
    }
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.125f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, ChangedHoverActivityRestartsDelayWithoutAnObservedUnhoveredFrame){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 7u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 7u, true, 0.25f, options));
    const u64 previousRevision = state.revision();
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 8u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    EXPECT_GT(state.revision(), previousRevision);
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 8u, true, 0.25f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 8u, true, 0.25f, options));
    EXPECT_TRUE(state.visible());
    const u64 visibleRevision = state.revision();
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 9u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    EXPECT_GT(state.revision(), visibleRevision);
}

TEST(UiTooltipBehaviorTests, UnchangedHoverActivityPreservesAccumulatedDelay){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 7u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 7u, true, 0.25f, options));
    const u64 waitingRevision = state.revision();
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 7u, true, 0.0f, options));
    EXPECT_EQ(state.revision(), waitingRevision);
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 7u, true, 0.25f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, ZeroDelayAppearsOnTheFirstEnabledAcceptedHover){
    TooltipState state;
    TooltipOptions options;
    options.delaySeconds = 0.0f;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, false, 0.0f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    EXPECT_TRUE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, false, 0.0f, options));
    EXPECT_FALSE(state.visible());
    options.enabled = false;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 10.0f, options));
    EXPECT_FALSE(state.visible());
}

TEST(UiTooltipBehaviorTests, ZeroDeltaAndSaturatedHoverDoNotInventTimeOrRevisionChanges){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    const u64 waitingRevision = state.revision();
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    EXPECT_FALSE(state.visible());
    EXPECT_EQ(state.revision(), waitingRevision);
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, Limit<f32>::s_Max, options));
    EXPECT_TRUE(state.visible());
    const u64 visibleRevision = state.revision();
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, Limit<f32>::s_Max, options));
    EXPECT_TRUE(state.visible());
    EXPECT_EQ(state.revision(), visibleRevision);
}

TEST(UiTooltipBehaviorTests, LeavingAVisibleAnchorRequiresACompleteNewHoverDelay){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.5f, options));
    ASSERT_TRUE(state.visible());
    const u64 visibleRevision = state.revision();
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, false, 100.0f, options));
    EXPECT_FALSE(state.visible());
    EXPECT_GT(state.revision(), visibleRevision);
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, LeavingBeforeTheDelayAlsoDiscardsAccumulatedTime){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, false, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, DisablingAndReenablingDoesNotCarryOldHoverTime){
    TooltipState state;
    TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.5f, options));
    ASSERT_TRUE(state.visible());
    options.enabled = false;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    options.enabled = true;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.5f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, ReplacingTheAnchorRetiresAVisibleTooltip){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.5f, options));
    const u64 visibleRevision = state.revision();
    const WidgetId replacement{ 202u };
    ASSERT_TRUE(TooltipBehavior::Update(state, replacement, s_Declaration, {}, 0u, 0u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    EXPECT_GT(state.revision(), visibleRevision);
    ASSERT_TRUE(TooltipBehavior::Update(state, replacement, s_Declaration, {}, 0u, 0u, true, 0.5f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, RenewedDeclarationRestartsTheDelayEvenWithTheSameAnchorKey){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration + 1u, {}, 0u, 0u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration + 1u, {}, 0u, 0u, true, 0.25f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration + 1u, {}, 0u, 0u, true, 0.25f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, EveryPopupLifetimeFieldParticipatesInTheHoverBinding){
    PopupToken replacements[5u] = { s_Popup, s_Popup, s_Popup, s_Popup, {} };
    ++replacements[0u].widget.value;
    ++replacements[1u].declarationGeneration;
    ++replacements[2u].instanceGeneration;
    ++replacements[3u].openGeneration;
    const TooltipOptions options;
    for(const PopupToken& replacement : replacements){
        TooltipState state;
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, s_Popup, 0u, 0u, true, 0.0f, options));
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, s_Popup, 0u, 0u, true, 0.5f, options));
        ASSERT_TRUE(state.visible());
        const u64 revision = state.revision();
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, replacement, 0u, 0u, true, 100.0f, options));
        EXPECT_FALSE(state.visible());
        EXPECT_GT(state.revision(), revision);
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, replacement, 0u, 0u, true, 0.5f, options));
        EXPECT_TRUE(state.visible());
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, s_Popup, 0u, 0u, true, 100.0f, options));
        EXPECT_FALSE(state.visible());
    }
}

TEST(UiTooltipBehaviorTests, FocusLossRetiresHoverEvenWhenPointerAndFocusReturnBeforeTheNextUpdate){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.5f, options));
    ASSERT_TRUE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 1u, 0u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 1u, 0u, true, 0.5f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, ChangingDelayCannotMakePreviousElapsedTimeQualifyTheNewOptions){
    TooltipState state;
    TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
    options.delaySeconds = 0.125f;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.125f, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, ChangingWidthGapOrSideAlsoStartsANewHoverLifetime){
    TooltipOptions replacements[3u];
    replacements[0u].maximumWidth = 256.0f;
    replacements[1u].gap = 8.0f;
    replacements[2u].side = PopupPlacementSide::Right;
    const TooltipOptions options;
    for(const TooltipOptions& replacement : replacements){
        TooltipState state;
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.5f, options));
        ASSERT_TRUE(state.visible());
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 100.0f, replacement));
        EXPECT_FALSE(state.visible());
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.5f, replacement));
        EXPECT_TRUE(state.visible());
    }
}

TEST(UiTooltipBehaviorTests, AllDeclaredPlacementSidesAndFiniteLargeLimitsAreAccepted){
    const PopupPlacementSide::Enum sides[] = { PopupPlacementSide::Below, PopupPlacementSide::Above,
        PopupPlacementSide::Right, PopupPlacementSide::Left, PopupPlacementSide::Center };
    for(const PopupPlacementSide::Enum side : sides){
        TooltipState state;
        TooltipOptions options;
        options.delaySeconds = 0.0f;
        options.maximumWidth = Limit<f32>::s_Max;
        options.gap = Limit<f32>::s_Max;
        options.side = side;
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
        EXPECT_TRUE(state.visible());
    }
    TooltipState state;
    TooltipOptions options;
    options.delaySeconds = Limit<f32>::s_Max;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, Limit<f32>::s_Max, options));
    EXPECT_TRUE(state.visible());
}

TEST(UiTooltipBehaviorTests, InvalidTimingPreservesTheOldBindingAndItsAccumulatedDelay){
    const f32 invalidDeltas[] = { -1.0f, Limit<f32>::s_QuietNaN, Limit<f32>::s_Infinity, -Limit<f32>::s_Infinity };
    const TooltipOptions options;
    for(const f32 delta : invalidDeltas){
        TooltipState state;
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
        const u64 revision = state.revision();
        EXPECT_FALSE(TooltipBehavior::Update(state, WidgetId{ 202u }, s_Declaration + 1u, s_Popup, 1u, 0u, false, delta, options));
        EXPECT_FALSE(state.visible());
        EXPECT_EQ(state.revision(), revision);
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
        EXPECT_TRUE(state.visible());
    }
}

TEST(UiTooltipBehaviorTests, InvalidOptionsAreRejectedBeforeAnyBindingOrDelayMutation){
    TooltipOptions invalid[12u];
    invalid[0u].delaySeconds = -1.0f;
    invalid[1u].delaySeconds = Limit<f32>::s_QuietNaN;
    invalid[2u].delaySeconds = Limit<f32>::s_Infinity;
    invalid[3u].maximumWidth = 0.0f;
    invalid[4u].maximumWidth = -1.0f;
    invalid[5u].maximumWidth = Limit<f32>::s_QuietNaN;
    invalid[6u].maximumWidth = Limit<f32>::s_Infinity;
    invalid[7u].gap = -1.0f;
    invalid[8u].gap = Limit<f32>::s_QuietNaN;
    invalid[9u].gap = Limit<f32>::s_Infinity;
    invalid[10u].side = static_cast<PopupPlacementSide::Enum>(255u);
    invalid[11u].enabled = false;
    invalid[11u].maximumWidth = 0.0f;
    const TooltipOptions options;
    for(const TooltipOptions& rejected : invalid){
        TooltipState state;
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
        const u64 revision = state.revision();
        EXPECT_FALSE(TooltipBehavior::Update(state, WidgetId{ 202u }, s_Declaration + 1u, s_Popup, 1u, 0u, false, 0.5f, rejected));
        EXPECT_FALSE(state.visible());
        EXPECT_EQ(state.revision(), revision);
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
        EXPECT_TRUE(state.visible());
    }
}

TEST(UiTooltipBehaviorTests, InvalidAnchorsDeclarationsAndPartialPopupTokensPreserveState){
    struct Binding{
        WidgetId anchor;
        u64 declaration;
        PopupToken popup;
    };
    const Binding invalid[] = {
        { {}, s_Declaration, s_Popup },
        { s_Anchor, 0u, s_Popup },
        { s_Anchor, s_Declaration, { {}, 19u, 23u, 7u } },
        { s_Anchor, s_Declaration, { s_Popup.widget, 0u, 23u, 7u } },
        { s_Anchor, s_Declaration, { s_Popup.widget, 19u, 0u, 7u } },
        { s_Anchor, s_Declaration, { s_Popup.widget, 19u, 23u, 0u } },
        { s_Anchor, s_Declaration, { s_Popup.widget, 0u, 0u, 0u } }
    };
    const TooltipOptions options;
    for(const Binding& rejected : invalid){
        TooltipState state;
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
        const u64 revision = state.revision();
        EXPECT_FALSE(TooltipBehavior::Update(state, rejected.anchor, rejected.declaration, rejected.popup, 1u, 0u, false, 0.5f, options));
        EXPECT_FALSE(state.visible());
        EXPECT_EQ(state.revision(), revision);
        ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.25f, options));
        EXPECT_TRUE(state.visible());
    }
}

TEST(UiTooltipBehaviorTests, InvalidUpdatesCannotHideAnAlreadyVisibleTooltip){
    TooltipState state;
    TooltipOptions options;
    options.delaySeconds = 0.0f;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, true, 0.0f, options));
    ASSERT_TRUE(state.visible());
    const u64 revision = state.revision();
    options.enabled = false;
    options.maximumWidth = 0.0f;
    EXPECT_FALSE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, {}, 0u, 0u, false, 0.0f, options));
    EXPECT_TRUE(state.visible());
    EXPECT_EQ(state.revision(), revision);
}

TEST(UiTooltipBehaviorTests, ExplicitResetInvalidatesEvenAnAlreadyHiddenStateWithoutReplacingItsInstance){
    TooltipState state;
    const u64 instance = state.instanceGeneration();
    const u64 revision = state.revision();
    state.reset();
    EXPECT_EQ(state.instanceGeneration(), instance);
    EXPECT_GT(state.revision(), revision);
    EXPECT_FALSE(state.visible());
    const u64 resetRevision = state.revision();
    state.reset();
    EXPECT_GT(state.revision(), resetRevision);
    EXPECT_EQ(state.instanceGeneration(), instance);
}

TEST(UiTooltipBehaviorTests, ExplicitResetRetiresVisibleStateAndAllAccumulatedHoverTime){
    TooltipState state;
    const TooltipOptions options;
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, s_Popup, 5u, 0u, true, 0.0f, options));
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, s_Popup, 5u, 0u, true, 0.5f, options));
    ASSERT_TRUE(state.visible());
    const u64 revision = state.revision();
    state.reset();
    EXPECT_FALSE(state.visible());
    EXPECT_GT(state.revision(), revision);
    EXPECT_FLOAT_EQ(state.placement().bounds.width, 0.0f);
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, s_Popup, 5u, 0u, true, 100.0f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, s_Popup, 5u, 0u, true, 0.25f, options));
    EXPECT_FALSE(state.visible());
    ASSERT_TRUE(TooltipBehavior::Update(state, s_Anchor, s_Declaration, s_Popup, 5u, 0u, true, 0.25f, options));
    EXPECT_TRUE(state.visible());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

