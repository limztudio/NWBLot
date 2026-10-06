// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_tools_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiPopupToolsTests;

class UiPopupToolsBuilderTests : public PopupToolsFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupToolsBuilderTests, TooltipDelayBeginsOnlyAfterTheAnchorLayoutIsAccepted){
    m_tooltipOptions.delaySeconds = 0.5f;
    m_deltaSeconds = 0.25f;
    ASSERT_TRUE(prepareTools(1u, true, false));
    EXPECT_FALSE(send({ InputEventType::PointerMove, { 30.0f, 30.0f } }).hover.valid());
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(3u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(4u, true, false));
    EXPECT_TRUE(m_tooltip.visible());
}

TEST_F(UiPopupToolsBuilderTests, ZeroDelayTooltipPaintsWithoutCreatingPopupTargetsOrTakingFocus){
    ASSERT_TRUE(acceptTools(1u, true, false));
    const usize targetCount = m_context.input().targets().size();
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    EXPECT_TRUE(m_tooltip.visible());
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_EQ(m_context.input().targets().size(), targetCount);
    EXPECT_EQ(target(id("tip", "panel")), nullptr);
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_FALSE(snapshot.commands().empty());
    EXPECT_GT(snapshot.commands().back().layer, 0u);
    for(const HitTarget& accepted : m_context.input().targets()){
        EXPECT_FALSE(accepted.popup.valid());
        EXPECT_EQ(accepted.layer, 0u);
    }
    press(Core::Key::Tab);
    EXPECT_EQ(m_context.input().focus(), anchor());
}

TEST_F(UiPopupToolsBuilderTests, VisibleTooltipPaintsItsDeclarationStyleAfterALaterStyleChange){
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 280.0f }));
    EXPECT_FALSE(m_builder.button("anchor", "Anchor", m_anchorOptions));
    m_builder.tooltipStyle().background = Name("button.normal");
    ASSERT_TRUE(m_builder.tooltip("tip", "anchor", "Helpful text", m_tooltip, m_tooltipOptions));
    m_builder.tooltipStyle().background = Name("button.pressed");
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_tooltip.visible());
    const DrawSnapshot snapshot = m_paint.freeze();
    Rect background;
    ASSERT_TRUE(skinQuad(snapshot, 6u, background));
    EXPECT_FLOAT_EQ(background.x, m_tooltip.placement().bounds.x);
    EXPECT_FLOAT_EQ(background.y, m_tooltip.placement().bounds.y);
}

TEST_F(UiPopupToolsBuilderTests, LeavingTheAnchorCancelsVisibleTooltipAndRequiresAnotherDelay){
    m_tooltipOptions.delaySeconds = 0.5f;
    m_deltaSeconds = 0.5f;
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    ASSERT_TRUE(acceptTools(3u, true, false));
    ASSERT_TRUE(m_tooltip.visible());
    EXPECT_NE(send({ InputEventType::PointerMove, { 790.0f, 590.0f } }).hover, anchor());
    ASSERT_TRUE(acceptTools(4u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(5u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(6u, true, false));
    EXPECT_TRUE(m_tooltip.visible());
}

TEST_F(UiPopupToolsBuilderTests, LeaveAndReturnBetweenDeclarationsRestartsTheTooltipDelay){
    m_tooltipOptions.delaySeconds = 0.5f;
    m_deltaSeconds = 0.25f;
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    ASSERT_TRUE(acceptTools(3u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    const u64 activity = m_context.input().hoverActivityGeneration();
    EXPECT_NE(send({ InputEventType::PointerMove, { 790.0f, 590.0f } }).hover, anchor());
    ASSERT_TRUE(moveToAnchor());
    EXPECT_GT(m_context.input().hoverActivityGeneration(), activity);
    ASSERT_TRUE(acceptTools(4u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(5u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(6u, true, false));
    EXPECT_TRUE(m_tooltip.visible());
}

TEST_F(UiPopupToolsBuilderTests, SameAnchorPointerMotionKeepsTheTooltipDelay){
    m_tooltipOptions.delaySeconds = 0.5f;
    m_deltaSeconds = 0.25f;
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    const u64 activity = m_context.input().hoverActivityGeneration();
    const Point pointer = m_context.input().pointerPosition();
    EXPECT_EQ(send({ InputEventType::PointerMove, { pointer.x + 5.0f, pointer.y } }).hover, anchor());
    EXPECT_EQ(m_context.input().hoverActivityGeneration(), activity);
    ASSERT_TRUE(acceptTools(3u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(4u, true, false));
    EXPECT_TRUE(m_tooltip.visible());
}

TEST_F(UiPopupToolsBuilderTests, ClickBetweenDeclarationsRestartsTheTooltipDelay){
    m_tooltipOptions.delaySeconds = 0.5f;
    m_deltaSeconds = 0.25f;
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    ASSERT_TRUE(acceptTools(3u, true, false));
    ASSERT_TRUE(acceptTools(4u, true, false));
    ASSERT_TRUE(m_tooltip.visible());
    const u64 activity = m_context.input().hoverActivityGeneration();
    const Point pointer = m_context.input().pointerPosition();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, pointer }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, pointer }).pointerConsumed);
    EXPECT_GT(m_context.input().hoverActivityGeneration(), activity);
    ASSERT_TRUE(acceptTools(5u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(6u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(7u, true, false));
    EXPECT_TRUE(m_tooltip.visible());
}

TEST_F(UiPopupToolsBuilderTests, PrimaryCaptureAndSecondaryPressSuppressTheTooltip){
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    ASSERT_TRUE(m_tooltip.visible());
    const Point pointer = m_context.input().pointerPosition();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, pointer }).pointerConsumed);
    ASSERT_TRUE(acceptTools(3u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, pointer }).pointerConsumed);
    ASSERT_TRUE(acceptTools(4u, true, false));
    ASSERT_TRUE(m_tooltip.visible());
    EXPECT_TRUE(send({ InputEventType::SecondaryDown, pointer }).pointerConsumed);
    ASSERT_TRUE(acceptTools(5u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    EXPECT_TRUE(send({ InputEventType::SecondaryUp, pointer }).pointerConsumed);
}

TEST_F(UiPopupToolsBuilderTests, DisabledTooltipAndDisabledAnchorCannotRetainVisibleHelp){
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    ASSERT_TRUE(m_tooltip.visible());
    m_tooltipOptions.enabled = false;
    ASSERT_TRUE(acceptTools(3u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    m_tooltipOptions.enabled = true;
    m_anchorOptions.enabled = false;
    ASSERT_TRUE(acceptTools(4u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    EXPECT_FALSE(m_context.input().hasPopup());
}

TEST_F(UiPopupToolsBuilderTests, UnknownPointerAndNativeFocusLossCannotQualifyTooltipHover){
    ASSERT_TRUE(acceptTools(1u, true, false));
    EXPECT_FALSE(m_context.input().pointerKnown());
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    ASSERT_TRUE(m_tooltip.visible());
    EXPECT_FALSE(send({ .type = InputEventType::FocusLost, .position = {} }).focus.valid());
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    ASSERT_TRUE(acceptTools(3u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
}

TEST_F(UiPopupToolsBuilderTests, OmittedTooltipAttachmentCannotReuseDelayWhileTheAnchorSurvives){
    m_tooltipOptions.delaySeconds = 0.5f;
    m_deltaSeconds = 0.25f;
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    ASSERT_TRUE(acceptTools(3u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    const u64 anchorDeclaration = target(anchor())->declarationGeneration;
    ASSERT_TRUE(acceptTools(4u, false, false));
    ASSERT_NE(target(anchor()), nullptr);
    EXPECT_EQ(target(anchor())->declarationGeneration, anchorDeclaration);
    ASSERT_TRUE(acceptTools(5u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(6u, true, false));
    EXPECT_FALSE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(7u, true, false));
    EXPECT_TRUE(m_tooltip.visible());
}

TEST_F(UiPopupToolsBuilderTests, FullyClippedReplacementGeometrySuppressesPreviouslyVisibleHelp){
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(moveToAnchor());
    ASSERT_TRUE(acceptTools(2u, true, false));
    ASSERT_TRUE(m_tooltip.visible());
    ASSERT_TRUE(acceptTools(3u, true, false, { 900.0f, 900.0f, 360.0f, 280.0f }));
    EXPECT_FALSE(m_tooltip.visible());
    EXPECT_FLOAT_EQ(m_tooltip.placement().bounds.width, 0.0f);
    const DrawSnapshot snapshot = m_paint.freeze();
    for(const DrawCommand& command : snapshot.commands())
        EXPECT_EQ(command.layer, 0u);
}

TEST_F(UiPopupToolsBuilderTests, TooltipInsideAPlainPopupPaintsAfterItsOverlayWithoutAddingAnInputScope){
    m_plainPopup.open();
    ASSERT_TRUE(acceptPopupTooltip(1u));
    const usize targetCount = m_context.input().targets().size();
    ASSERT_TRUE(moveToAnchor("plain"));
    ASSERT_TRUE(acceptPopupTooltip(2u));
    EXPECT_TRUE(m_tooltip.visible());
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().targets().size(), targetCount);
    EXPECT_EQ(target(id("tip", "plain")), nullptr);
    EXPECT_EQ(m_context.input().focus(), anchor("plain"));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_FALSE(snapshot.commands().empty());
    EXPECT_GT(snapshot.commands().back().layer, target(anchor("plain"))->layer);
}

TEST_F(UiPopupToolsBuilderTests, TooltipRequiresAPrecedingAnchorInTheSameContainer){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 280.0f }));
    EXPECT_FALSE(m_builder.button("anchor", "Anchor", m_anchorOptions));
    ASSERT_TRUE(m_builder.beginRow("inner"));
    EXPECT_FALSE(m_builder.tooltip("tip", "anchor", "Help", m_tooltip, m_tooltipOptions));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiPopupToolsBuilderTests, MissingTooltipAnchorRejectsTheCandidate){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 280.0f }));
    EXPECT_FALSE(m_builder.tooltip("tip", "missing", "Help", m_tooltip, m_tooltipOptions));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiPopupToolsBuilderTests, InvalidTooltipSizeRejectsBeforeScopeEnd){
    m_tooltipOptions.maximumWidth = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(declareTools(1u, true, false));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiPopupToolsBuilderTests, OneTooltipStateCannotOwnTwoAttachmentsInTheSameFrame){
    ASSERT_TRUE(declareTools(1u, true, false));
    EXPECT_FALSE(m_builder.button("second", "Second", m_anchorOptions));
    EXPECT_FALSE(m_builder.tooltip("other", "second", "Other help", m_tooltip, m_tooltipOptions));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiPopupToolsBuilderTests, ExplicitTooltipResetBeforeScopeEndRejectsItsBorrowedCandidate){
    ASSERT_TRUE(acceptTools(1u, true, false));
    ASSERT_TRUE(declareTools(2u, true, false));
    const u64 revision = m_tooltip.revision();
    m_tooltip.reset();
    EXPECT_GT(m_tooltip.revision(), revision);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_FALSE(m_tooltip.visible());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupToolsBuilderTests, SecondaryTriggerUsesOnlyTheExactlyAcceptedAnchorLayout){
    ASSERT_TRUE(prepareTools(1u, false));
    EXPECT_FALSE(send({ InputEventType::SecondaryDown, { 30.0f, 30.0f } }).pointerConsumed);
    EXPECT_FALSE(send({ InputEventType::SecondaryUp, { 30.0f, 30.0f } }).pointerConsumed);
    EXPECT_FALSE(m_menu.isOpen());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(openBySecondary(2u));
    EXPECT_TRUE(m_menuResult.opened);
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_FALSE(m_anchorActivated);
    EXPECT_TRUE(m_context.input().hasPopup());
    ASSERT_NE(menuHost(), nullptr);
}

TEST_F(UiPopupToolsBuilderTests, OrdinaryPrimaryActivationDoesNotOpenAnAttachedContextMenu){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(moveToAnchor());
    click(m_context.input().pointerPosition());
    ASSERT_TRUE(acceptTools(2u, false));
    EXPECT_TRUE(m_anchorActivated);
    EXPECT_FALSE(m_menuResult.opened);
    EXPECT_FALSE(m_menu.isOpen());
}

TEST_F(UiPopupToolsBuilderTests, KeyboardPreviewSkipsDisabledCommandsAndEnterCommitsItsStableKey){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    press(Core::Key::Down);
    press(Core::Key::Down);
    ASSERT_TRUE(acceptTools(3u, false));
    EXPECT_EQ(m_menu.cursorKey(), 4u);
    EXPECT_FALSE(m_menuResult.activated);
    press(Core::Key::Enter);
    ASSERT_TRUE(acceptTools(4u, false));
    EXPECT_TRUE(m_menuResult.activated);
    EXPECT_EQ(m_menuResult.key, 4u);
    EXPECT_TRUE(m_menuResult.closed);
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_context.input().hasPopup());
}

TEST_F(UiPopupToolsBuilderTests, PointerReleaseCommitsOnlyAnEnabledCommand){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    const HitTarget* disabled = menuRow(3u);
    ASSERT_NE(disabled, nullptr);
    EXPECT_FALSE(disabled->enabled);
    click({ disabled->rectangle.x + disabled->rectangle.width * 0.5f,
        disabled->rectangle.y + disabled->rectangle.height * 0.5f });
    ASSERT_TRUE(acceptTools(3u, false));
    EXPECT_FALSE(m_menuResult.activated);
    ASSERT_TRUE(m_menu.isOpen());
    const HitTarget* enabled = menuRow(5u);
    ASSERT_NE(enabled, nullptr);
    click({ enabled->rectangle.x + enabled->rectangle.width * 0.5f,
        enabled->rectangle.y + enabled->rectangle.height * 0.5f });
    ASSERT_TRUE(acceptTools(4u, false));
    EXPECT_TRUE(m_menuResult.activated);
    EXPECT_EQ(m_menuResult.key, 5u);
    EXPECT_FALSE(m_menu.isOpen());
}

TEST_F(UiPopupToolsBuilderTests, OutsideClickCancelsAndConsumesTheCompletePointerSequence){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    click({ 790.0f, 590.0f });
    ASSERT_TRUE(acceptTools(3u, false));
    EXPECT_TRUE(m_menuResult.closed);
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_FALSE(m_anchorActivated);
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_context.input().hasPopup());
}

TEST_F(UiPopupToolsBuilderTests, NativeFocusLossCancelsMenuEvenIfFocusReturnsBeforeDeclaration){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    EXPECT_FALSE(send({ .type = InputEventType::FocusLost, .position = {} }).focus.valid());
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    ASSERT_TRUE(acceptTools(3u, false));
    EXPECT_TRUE(m_menuResult.closed);
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_context.input().focus().valid());
}

TEST_F(UiPopupToolsBuilderTests, EmptyMenuRemainsOpenAndCannotProduceACommand){
    m_source.count = 0u;
    ++m_source.contentRevision;
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    EXPECT_EQ(m_menu.cursorKey(), 0u);
    press(Core::Key::Enter);
    ASSERT_TRUE(acceptTools(3u, false));
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_EQ(m_menuResult.key, 0u);
    EXPECT_TRUE(m_menu.isOpen());
}

TEST_F(UiPopupToolsBuilderTests, DisabledAttachmentDoesNotPublishAContextMenuTrigger){
    m_menuOptions.enabled = false;
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_NE(target(anchor()), nullptr);
    EXPECT_FALSE(target(anchor())->contextMenu);
    ASSERT_TRUE(moveToAnchor());
    secondaryClick(m_context.input().pointerPosition());
    ASSERT_TRUE(acceptTools(2u, false));
    EXPECT_FALSE(m_menuResult.opened);
    EXPECT_FALSE(m_menu.isOpen());
}

TEST_F(UiPopupToolsBuilderTests, DisablingTheAnchorClosesItsOpenMenuAndRemovesTheTrigger){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    m_anchorOptions.enabled = false;
    ASSERT_TRUE(acceptTools(3u, false));
    EXPECT_TRUE(m_menuResult.closed);
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_context.input().hasPopup());
    ASSERT_NE(target(anchor()), nullptr);
    EXPECT_FALSE(target(anchor())->contextMenu);
}

TEST_F(UiPopupToolsBuilderTests, FullyClippedAnchorCancelsItsOpenContextMenuDuringScopeEnd){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    ASSERT_TRUE(acceptTools(3u, false, true, { 900.0f, 900.0f, 360.0f, 280.0f }));
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_FALSE(m_context.input().hasPopup());
}

TEST_F(UiPopupToolsBuilderTests, RecreatedMenuAttachmentClosesOldPopupWhileItsAnchorSurvives){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    const u64 anchorDeclaration = target(anchor())->declarationGeneration;
    ASSERT_TRUE(acceptTools(3u, false, false));
    ASSERT_NE(target(anchor()), nullptr);
    EXPECT_EQ(target(anchor())->declarationGeneration, anchorDeclaration);
    EXPECT_FALSE(m_context.input().hasPopup());
    ASSERT_TRUE(acceptTools(4u, false));
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_FALSE(m_context.input().hasPopup());
}

TEST_F(UiPopupToolsBuilderTests, OneMenuStateCannotOwnTwoAttachmentsInTheSameFrame){
    ASSERT_TRUE(declareTools(1u, false));
    EXPECT_FALSE(m_builder.button("second", "Second", m_anchorOptions));
    const ContextMenuResult second = m_builder.contextMenu("other", "second", m_source, m_menu, m_menuOptions);
    EXPECT_FALSE(second.valid);
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiPopupToolsBuilderTests, MissingMenuAnchorRejectsTheCandidate){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 280.0f }));
    const ContextMenuResult result = m_builder.contextMenu("menu", "missing", m_source, m_menu, m_menuOptions);
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiPopupToolsBuilderTests, InvalidMenuRowHeightRejectsTheCandidate){
    m_menuOptions.rowHeight = 0.0f;
    EXPECT_FALSE(declareTools(1u, false));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiPopupToolsBuilderTests, ExternalMenuCloseBeforeScopeEndPreservesTheMutationAndRejectsTheLoan){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(m_menu.open({ 40.0f, 60.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(declareTools(2u, false));
    const u64 revision = m_menu.revision();
    m_menu.close();
    EXPECT_GT(m_menu.revision(), revision);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiPopupToolsBuilderTests, SourceMutationDuringRowPaintingRejectsTheCandidateWithoutRollingBackData){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(m_menu.open({ 40.0f, 60.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(declareTools(2u, false));
    m_source.changeRevisionOnText = true;
    const u64 revision = m_source.contentRevision;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_GT(m_source.contentRevision, revision);
    EXPECT_TRUE(m_menu.isOpen());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiPopupToolsBuilderTests, ReentrantSourceClosePreservesTheClosedStateAndRejectsDeferredPainting){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(m_menu.open({ 40.0f, 60.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(declareTools(2u, false));
    m_source.closeOnText = &m_menu;
    const u64 revision = m_menu.revision();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_GT(m_menu.revision(), revision);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiPopupToolsBuilderTests, MenuSourceCallbackCannotMutateAnEarlierBorrowedTooltipAndStillPublish){
    ASSERT_TRUE(acceptTools(1u));
    ASSERT_TRUE(m_menu.open({ 40.0f, 60.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(declareTools(2u));
    const u64 revision = m_tooltip.revision();
    m_source.resetTooltipOnText = &m_tooltip;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_GT(m_tooltip.revision(), revision);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

