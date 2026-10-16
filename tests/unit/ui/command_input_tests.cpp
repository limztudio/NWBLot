// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/ui/native_input.h>

#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_command_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

HitTarget Target(const u64 id = 1u){
    HitTarget target;
    target.id = { id };
    target.rectangle = { 0.0f, 0.0f, 80.0f, 40.0f };
    target.clip = { 0.0f, 0.0f, 200.0f, 100.0f };
    target.declarationGeneration = 7u;
    target.focusable = true;
    target.activatable = true;
    target.focusOnCommit = true;
    return target;
}

HitTarget Control(const u64 id = 1u){
    HitTarget target = Target(id);
    target.navigable = true;
    target.horizontalNavigation = true;
    target.control = { 11u, 21u, 31u };
    return target;
}

InputEvent Command(const InputEventType::Enum type, const InputSource source, const InputCommand::Enum command, const bool repeat = false){
    InputEvent event;
    event.type = type;
    event.source = source;
    event.command = command;
    event.repeat = repeat;
    return event;
}

InputEvent Key(const InputEventType::Enum type, const bool shift = false, const bool repeat = false){
    InputEvent event;
    event.type = type;
    event.key = Core::Key::W;
    event.shift = shift;
    event.repeat = repeat;
    return event;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RoutedInput{
    InputRoutingResult routing;
    InputEvent event;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiCommandInputTests : public testing::Test{
public:
    UiCommandInputTests()
        : m_arena(Name("tests/ui/command_input"))
        , m_router(m_arena)
    {}


protected:
    [[nodiscard]] RoutedInput send(const InputEvent& event){
        const auto resolved = m_router.queue(UiTestSupport::NativeInput(event));
        EXPECT_TRUE(resolved);
        if(!resolved)
            return {};
        return { m_router.process(), *resolved };
    }

    [[nodiscard]] Expected<ControlAction> take(const HitTarget& host)noexcept{
        return m_router.consumeControlAction(host.id, host.declarationGeneration, host.control);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiCommandInputTests, TwoControllersRetainIndependentOwnersForTheSameCommand){
    const HitTarget host = Control();
    ASSERT_TRUE(m_router.commitTargets(&host, 1u, 1u));
    const InputSource first{ 1u, 5u };
    const InputSource second{ 2u, 5u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, first, InputCommand::Down)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, second, InputCommand::Down)).routing.commandConsumed);
    ControlAction action;
    const auto actionResult1 = take(host);
    ASSERT_TRUE(actionResult1);
    action = *actionResult1;
    EXPECT_EQ(action.kind, ControlActionKind::Down);
    const u64 firstSequence = action.id.sequence;
    const auto actionResult2 = take(host);
    ASSERT_TRUE(actionResult2);
    action = *actionResult2;
    EXPECT_EQ(action.kind, ControlActionKind::Down);
    EXPECT_GT(action.id.sequence, firstSequence);
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, first, InputCommand::None)).routing.commandConsumed);
    EXPECT_FALSE(m_router.ownsSource(first));
    EXPECT_TRUE(m_router.ownsSource(second));
    InputEvent resolved;
    const auto dispatchResult = send(Command(InputEventType::CommandDown, second, InputCommand::Right));
    EXPECT_TRUE(dispatchResult.routing.commandConsumed);
    resolved = dispatchResult.event;
    EXPECT_EQ(resolved.command, InputCommand::Down);
    EXPECT_TRUE(resolved.repeat);
    const auto actionResult3 = take(host);
    ASSERT_TRUE(actionResult3);
    action = *actionResult3;
    EXPECT_EQ(action.kind, ControlActionKind::Down);
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, second, InputCommand::None)).routing.commandConsumed);
    EXPECT_FALSE(m_router.ownsSource(second));
    EXPECT_FALSE(take(host));
}

TEST_F(UiCommandInputTests, HeldPhysicalPressPinsCommandSelectionAndEditAdmissionAcrossProfileReplacement){
    const HitTarget host = Control();
    ASSERT_TRUE(m_router.commitTargets(&host, 1u, 1u));
    const InputKeyBinding initial{ Core::Key::W, 0, Core::InputModifier::Shift, InputCommand::Down, InputSelectionPolicy::Shift, true, true };
    ASSERT_TRUE(m_router.setBindings(&initial, 1u));
    InputEvent resolved;
    const auto dispatchResult1 = send(Key(InputEventType::KeyDown, true));
    EXPECT_TRUE(dispatchResult1.routing.commandConsumed);
    resolved = dispatchResult1.event;
    EXPECT_EQ(resolved.command, InputCommand::Down);
    EXPECT_TRUE(resolved.extend);
    EXPECT_TRUE(resolved.edit);
    EXPECT_TRUE(resolved.allowText);
    const InputKeyBinding replacement{ Core::Key::W, 0, 0, InputCommand::Up, InputSelectionPolicy::None, false };
    ASSERT_TRUE(m_router.setBindings(&replacement, 1u));
    m_router.restoreDefaultBindings();
    const auto dispatchResult2 = send(Key(InputEventType::KeyDown));
    EXPECT_TRUE(dispatchResult2.routing.commandConsumed);
    resolved = dispatchResult2.event;
    EXPECT_EQ(resolved.command, InputCommand::Down);
    EXPECT_TRUE(resolved.repeat);
    EXPECT_TRUE(resolved.extend);
    EXPECT_TRUE(resolved.edit);
    EXPECT_TRUE(resolved.allowText);
    EXPECT_TRUE(m_router.ownsKey(Core::Key::W));
    const auto dispatchResult3 = send(Key(InputEventType::KeyUp));
    EXPECT_TRUE(dispatchResult3.routing.commandConsumed);
    resolved = dispatchResult3.event;
    EXPECT_EQ(resolved.command, InputCommand::Down);
    EXPECT_TRUE(resolved.allowText);
    EXPECT_FALSE(m_router.ownsKey(Core::Key::W));
    ControlAction action;
    const auto actionResult1 = take(host);
    ASSERT_TRUE(actionResult1);
    action = *actionResult1;
    EXPECT_EQ(action.kind, ControlActionKind::Down);
    const auto actionResult2 = take(host);
    ASSERT_TRUE(actionResult2);
    action = *actionResult2;
    EXPECT_EQ(action.kind, ControlActionKind::Down);
    ASSERT_TRUE(m_router.setBindings(&replacement, 1u));
    const auto dispatchResult4 = send(Key(InputEventType::KeyDown));
    EXPECT_TRUE(dispatchResult4.routing.commandConsumed);
    resolved = dispatchResult4.event;
    EXPECT_EQ(resolved.command, InputCommand::Up);
    EXPECT_FALSE(resolved.extend);
    EXPECT_FALSE(resolved.edit);
    EXPECT_FALSE(resolved.allowText);
    const auto actionResult3 = take(host);
    ASSERT_TRUE(actionResult3);
    action = *actionResult3;
    EXPECT_EQ(action.kind, ControlActionKind::Up);
}

TEST_F(UiCommandInputTests, AnUnconsumedInitialPressCannotClaimUiAfterFocusAppears){
    const InputSource source{ 1u, 1u };
    EXPECT_FALSE(send(Command(InputEventType::CommandDown, source, InputCommand::Activate)).routing.commandConsumed);
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_FALSE(send(Command(InputEventType::CommandDown, source, InputCommand::Activate, true)).routing.commandConsumed);
    EXPECT_FALSE(m_router.ownsSource(source));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(send(Command(InputEventType::CommandUp, source, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, source, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, source, InputCommand::None)).routing.commandConsumed);
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_EQ(m_router.actions()[0u].origin, InputActionSource::Command);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
}

TEST_F(UiCommandInputTests, RetiredControlOwnerCannotReviveWhenTheExactOldTargetReturns){
    const HitTarget host = Control();
    ASSERT_TRUE(m_router.commitTargets(&host, 1u, 1u));
    const InputSource source{ 3u, 9u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, source, InputCommand::Down)).routing.commandConsumed);
    ControlAction action;
    const auto actionResult1 = take(host);
    ASSERT_TRUE(actionResult1);
    action = *actionResult1;
    m_router.invalidateTarget(host.id);
    ASSERT_TRUE(m_router.commitTargets(&host, 1u, 2u));
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, source, InputCommand::Down, true)).routing.commandConsumed);
    EXPECT_FALSE(take(host));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, source, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, source, InputCommand::Down)).routing.commandConsumed);
    const auto actionResult2 = take(host);
    ASSERT_TRUE(actionResult2);
    action = *actionResult2;
}

TEST_F(UiCommandInputTests, RetiredEditorCannotBorrowAHeldCommandAfterReturningWithItsOldIdentity){
    HitTarget editor = Target();
    editor.textEditable = true;
    ASSERT_TRUE(m_router.commitTargets(&editor, 1u, 1u));
    const InputSource source{ 3u, 9u };
    InputEvent resolved;
    const auto dispatchResult1 = send(Command(InputEventType::CommandDown, source, InputCommand::Left));
    EXPECT_TRUE(dispatchResult1.routing.commandConsumed);
    resolved = dispatchResult1.event;
    EXPECT_TRUE(m_router.canEditCommand(resolved));
    m_router.invalidateTarget(editor.id);
    ASSERT_TRUE(m_router.commitTargets(&editor, 1u, 2u));
    const auto dispatchResult2 = send(Command(InputEventType::CommandDown, source, InputCommand::Left, true));
    EXPECT_TRUE(dispatchResult2.routing.commandConsumed);
    resolved = dispatchResult2.event;
    EXPECT_FALSE(m_router.canEditCommand(resolved));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, source, InputCommand::None)).routing.commandConsumed);
    const auto dispatchResult3 = send(Command(InputEventType::CommandDown, source, InputCommand::Left));
    EXPECT_TRUE(dispatchResult3.routing.commandConsumed);
    resolved = dispatchResult3.event;
    EXPECT_TRUE(m_router.canEditCommand(resolved));
}

TEST_F(UiCommandInputTests, HeldEditingCommandCannotMoveToAnotherFocusedEditor){
    HitTarget first = Target();
    first.textEditable = true;
    HitTarget second = Target(2u);
    second.textEditable = true;
    second.focusOnCommit = false;
    second.rectangle.x = 100.0f;
    const HitTarget targets[]{ first, second };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 1u));
    const InputSource edit{ 3u, 1u };
    InputEvent resolved;
    const auto dispatchResult1 = send(Command(InputEventType::CommandDown, edit, InputCommand::Left));
    EXPECT_TRUE(dispatchResult1.routing.commandConsumed);
    resolved = dispatchResult1.event;
    EXPECT_TRUE(m_router.canEditCommand(resolved));
    const InputSource traverse{ 3u, 2u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, traverse, InputCommand::FocusNext)).routing.commandConsumed);
    EXPECT_EQ(m_router.focus(), second.id);
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, traverse, InputCommand::None)).routing.commandConsumed);
    const auto dispatchResult2 = send(Command(InputEventType::CommandDown, edit, InputCommand::Left, true));
    EXPECT_TRUE(dispatchResult2.routing.commandConsumed);
    resolved = dispatchResult2.event;
    EXPECT_FALSE(m_router.canEditCommand(resolved));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, edit, InputCommand::None)).routing.commandConsumed);
    const auto dispatchResult3 = send(Command(InputEventType::CommandDown, edit, InputCommand::Left));
    EXPECT_TRUE(dispatchResult3.routing.commandConsumed);
    resolved = dispatchResult3.event;
    EXPECT_TRUE(m_router.canEditCommand(resolved));
}

TEST_F(UiCommandInputTests, DelegatedAcceptStillReachesEditorSubmissionWhileVerticalNavigationDoesNot){
    HitTarget editor = Target();
    editor.textEditable = true;
    HitTarget host = Control(2u);
    host.focusOnCommit = false;
    host.rectangle.x = 100.0f;
    editor.commandOwner = host.id;
    editor.commandOwnerDeclarationGeneration = host.declarationGeneration;
    editor.commandControl = host.control;
    const HitTarget targets[]{ editor, host };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 1u));
    const InputSource submit{ 4u, 1u };
    InputEvent resolved;
    const auto dispatchResult1 = send(Command(InputEventType::CommandDown, submit, InputCommand::Accept));
    EXPECT_TRUE(dispatchResult1.routing.commandConsumed);
    resolved = dispatchResult1.event;
    EXPECT_TRUE(m_router.canEditCommand(resolved));
    EXPECT_FALSE(take(host));
    const auto submitRelease = m_router.queue(UiTestSupport::NativeInput(Command(InputEventType::CommandUp, submit, InputCommand::None)));
    ASSERT_TRUE(submitRelease);
    EXPECT_TRUE(m_router.canSubmitCommand(*submitRelease));
    EXPECT_TRUE(m_router.process().commandConsumed);
    ControlAction action;
    const auto actionResult1 = take(host);
    ASSERT_TRUE(actionResult1);
    action = *actionResult1;
    EXPECT_EQ(action.kind, ControlActionKind::Submit);
    EXPECT_FALSE(send(Command(InputEventType::CommandUp, submit, InputCommand::None)).routing.commandConsumed);
    const InputSource navigate{ 4u, 2u };
    const auto dispatchResult2 = send(Command(InputEventType::CommandDown, navigate, InputCommand::Down));
    EXPECT_TRUE(dispatchResult2.routing.commandConsumed);
    resolved = dispatchResult2.event;
    EXPECT_FALSE(m_router.canEditCommand(resolved));
    const auto actionResult2 = take(host);
    ASSERT_TRUE(actionResult2);
    action = *actionResult2;
    EXPECT_EQ(action.kind, ControlActionKind::Down);
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, navigate, InputCommand::None)).routing.commandConsumed);
    const auto dispatchResult3 = send(Command(InputEventType::CommandDown, submit, InputCommand::Accept));
    EXPECT_TRUE(dispatchResult3.routing.commandConsumed);
    resolved = dispatchResult3.event;
    EXPECT_TRUE(m_router.canEditCommand(resolved));
    m_router.invalidateTarget(host.id);
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 2u));
    const auto dispatchResult4 = send(Command(InputEventType::CommandDown, submit, InputCommand::Accept, true));
    EXPECT_TRUE(dispatchResult4.routing.commandConsumed);
    resolved = dispatchResult4.event;
    EXPECT_FALSE(m_router.canEditCommand(resolved));
    EXPECT_FALSE(take(host));
}

TEST_F(UiCommandInputTests, ClearFocusAndNativeLossRetainReleaseOwnershipButCancelPendingSubmissions){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    const InputSource held{ 5u, 1u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, held, InputCommand::Activate)).routing.commandConsumed);
    m_router.clearFocus();
    EXPECT_TRUE(m_router.ownsSource(held));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, held, InputCommand::None)).routing.commandConsumed);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, held, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_TRUE(send({ InputEventType::FocusLost }).routing.commandConsumed);
    EXPECT_TRUE(m_router.ownsSource(held));
    EXPECT_TRUE(m_router.wantsCommands());
    const InputSource traverse{ 5u, 2u };
    EXPECT_FALSE(send(Command(InputEventType::CommandDown, traverse, InputCommand::FocusNext)).routing.commandConsumed);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, held, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(send({ InputEventType::FocusGained }).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, held, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(send(Command(InputEventType::CommandDown, traverse, InputCommand::FocusNext, true)).routing.commandConsumed);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(send(Command(InputEventType::CommandUp, traverse, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, traverse, InputCommand::FocusNext)).routing.commandConsumed);
    EXPECT_EQ(m_router.focus(), target.id);
}

TEST_F(UiCommandInputTests, QueuedReleaseSeparatesOldAndNewBindingSnapshotsBeforeProcessing){
    const HitTarget host = Control();
    ASSERT_TRUE(m_router.commitTargets(&host, 1u, 1u));
    const InputKeyBinding initial{ Core::Key::W, 0, 0, InputCommand::Left };
    ASSERT_TRUE(m_router.setBindings(&initial, 1u));
    InputEvent first;
    InputEvent release;
    InputEvent second;
    const auto firstResult = m_router.queue(UiTestSupport::NativeInput(Key(InputEventType::KeyDown)));
    ASSERT_TRUE(firstResult);
    first = *firstResult;
    const InputKeyBinding replacement{ Core::Key::W, 0, 0, InputCommand::Right };
    ASSERT_TRUE(m_router.setBindings(&replacement, 1u));
    const auto releaseResult = m_router.queue(UiTestSupport::NativeInput(Key(InputEventType::KeyUp)));
    ASSERT_TRUE(releaseResult);
    release = *releaseResult;
    const auto secondResult = m_router.queue(UiTestSupport::NativeInput(Key(InputEventType::KeyDown)));
    ASSERT_TRUE(secondResult);
    second = *secondResult;
    EXPECT_EQ(first.command, InputCommand::Left);
    EXPECT_EQ(release.command, InputCommand::Left);
    EXPECT_EQ(second.command, InputCommand::Right);
    EXPECT_TRUE(m_router.process().commandConsumed);
    ControlAction action;
    const auto actionResult1 = take(host);
    ASSERT_TRUE(actionResult1);
    action = *actionResult1;
    EXPECT_EQ(action.kind, ControlActionKind::Left);
    const u64 sequence = action.id.sequence;
    const auto actionResult2 = take(host);
    ASSERT_TRUE(actionResult2);
    action = *actionResult2;
    EXPECT_EQ(action.kind, ControlActionKind::Right);
    EXPECT_GT(action.id.sequence, sequence);
    EXPECT_TRUE(m_router.ownsKey(Core::Key::W));
}

TEST_F(UiCommandInputTests, OrphanRepeatsAfterNativeLossAndResetNeedReleaseBeforeEditingOrNavigation){
    HitTarget editor = Target();
    editor.textEditable = true;
    ASSERT_TRUE(m_router.commitTargets(&editor, 1u, 1u));
    const InputSource edit{ 7u, 1u };
    InputEvent resolved;
    const auto dispatchResult1 = send(Command(InputEventType::CommandDown, edit, InputCommand::Left));
    EXPECT_TRUE(dispatchResult1.routing.commandConsumed);
    resolved = dispatchResult1.event;
    EXPECT_TRUE(m_router.canEditCommand(resolved));
    EXPECT_TRUE(send({ InputEventType::FocusLost }).routing.commandConsumed);
    EXPECT_FALSE(send({ InputEventType::FocusGained }).routing.commandConsumed);
    ASSERT_TRUE(m_router.commitTargets(&editor, 1u, 2u));
    EXPECT_TRUE(m_router.focus().valid());
    const auto dispatchResult2 = send(Command(InputEventType::CommandDown, edit, InputCommand::Left, true));
    EXPECT_TRUE(dispatchResult2.routing.commandConsumed);
    resolved = dispatchResult2.event;
    EXPECT_FALSE(m_router.canEditCommand(resolved));
    EXPECT_TRUE(m_router.ownsSource(edit));
    const auto dispatchResult3 = send(Command(InputEventType::CommandDown, edit, InputCommand::Left));
    EXPECT_TRUE(dispatchResult3.routing.commandConsumed);
    resolved = dispatchResult3.event;
    EXPECT_FALSE(m_router.canEditCommand(resolved));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, edit, InputCommand::None)).routing.commandConsumed);
    const auto dispatchResult4 = send(Command(InputEventType::CommandDown, edit, InputCommand::Left));
    EXPECT_TRUE(dispatchResult4.routing.commandConsumed);
    resolved = dispatchResult4.event;
    EXPECT_TRUE(m_router.canEditCommand(resolved));
    m_router.reset();
    HitTarget button = Target(2u);
    button.focusOnCommit = false;
    button.rectangle.x = 100.0f;
    const HitTarget targets[]{ editor, button };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 1u));
    const InputSource traverse{ 7u, 2u };
    EXPECT_FALSE(send(Command(InputEventType::CommandDown, traverse, InputCommand::FocusNext, true)).routing.commandConsumed);
    EXPECT_EQ(m_router.focus(), editor.id);
    EXPECT_FALSE(send(Command(InputEventType::CommandUp, traverse, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, traverse, InputCommand::FocusNext)).routing.commandConsumed);
    EXPECT_EQ(m_router.focus(), button.id);
    const InputSource activate{ 7u, 3u };
    EXPECT_FALSE(send(Command(InputEventType::CommandDown, activate, InputCommand::Activate, true)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeActivation(button.id));
    EXPECT_FALSE(send(Command(InputEventType::CommandUp, activate, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, activate, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeActivation(button.id));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, activate, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(m_router.consumeActivation(button.id));
}

TEST_F(UiCommandInputTests, MalformedCommandSourcesRejectWithoutChangingExistingOwners){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    InputEvent output;
    const InputSource invalidSources[]{ {}, { 1u, 0u } };
    for(const InputSource source : invalidSources){
        EXPECT_FALSE(m_router.queue(UiTestSupport::NativeInput(Command(InputEventType::CommandDown, source, InputCommand::Activate))));
    }
    EXPECT_FALSE(m_router.queue(UiTestSupport::NativeInput(Command(InputEventType::CommandDown, { 1u, 1u }, static_cast<InputCommand::Enum>(255u)))));
    EXPECT_FALSE(m_router.process().commandConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    InputEvent strayRelease = Command(InputEventType::CommandUp, { 2u, 2u }, InputCommand::None);
    strayRelease.allowText = true;
    const auto outputResult = m_router.queue(UiTestSupport::NativeInput(strayRelease));
    ASSERT_TRUE(outputResult);
    output = *outputResult;
    EXPECT_FALSE(output.allowText);
    EXPECT_FALSE(m_router.process().commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, { 0u, 1u }, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, { 0u, 1u }, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
}

TEST_F(UiCommandInputTests, ExplicitDeviceTerminationPreservesAnotherProvidersHeldSubmit){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    const InputSource first{ 31u, 7u };
    const InputSource second{ 32u, 7u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, first, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, second, InputCommand::Activate)).routing.commandConsumed);
    m_router.cancelDeviceInput(first.device);
    EXPECT_FALSE(m_router.ownsSource(first));
    EXPECT_TRUE(m_router.ownsSource(second));
    EXPECT_FALSE(send(Command(InputEventType::CommandUp, first, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, second, InputCommand::None)).routing.commandConsumed);
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_EQ(m_router.actions()[0u].inputSource, second);
}

TEST_F(UiCommandInputTests, ExactSourceCapacityRejectsOverflowWithoutLosingReleaseOrRebindingState){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    for(usize index = 0u; index < s_InputMaxSources; ++index){
        EXPECT_TRUE(send(Command(InputEventType::CommandDown, { 1u, index + 1u }, InputCommand::Left)).routing.commandConsumed);
    }
    const InputSource overflow{ 1u, s_InputMaxSources + 1u };
    EXPECT_FALSE(m_router.queue(UiTestSupport::NativeInput(Command(InputEventType::CommandDown, overflow, InputCommand::Right))));
    EXPECT_TRUE(m_router.ownsSource({ 1u, 1u }));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, { 1u, 1u }, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, overflow, InputCommand::Right)).routing.commandConsumed);
    EXPECT_TRUE(m_router.ownsSource(overflow));
    m_router.reset();
    EXPECT_TRUE(m_router.ownsSource(overflow));
    EXPECT_TRUE(m_router.wantsCommands());
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, overflow, InputCommand::None)).routing.commandConsumed);
    for(usize index = 1u; index < s_InputMaxSources; ++index)
        EXPECT_TRUE(send(Command(InputEventType::CommandUp, { 1u, index + 1u }, InputCommand::None)).routing.commandConsumed);
    EXPECT_FALSE(m_router.wantsCommands());
}

TEST_F(UiCommandInputTests, FullEventQueueCannotReleaseOrChangeAnAlreadyAdmittedSource){
    const HitTarget host = Control();
    ASSERT_TRUE(m_router.commitTargets(&host, 1u, 1u));
    const InputSource source{ 8u, 8u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, source, InputCommand::Down)).routing.commandConsumed);
    for(usize index = 0u; index < s_InputMaxEvents; ++index)
        ASSERT_TRUE(m_router.queue(UiTestSupport::NativeInput({ InputEventType::PointerMove })));
    InputEvent output;
    EXPECT_FALSE(m_router.queue(UiTestSupport::NativeInput(Command(InputEventType::CommandUp, source, InputCommand::None))));
    EXPECT_FALSE(m_router.process().commandConsumed);
    EXPECT_TRUE(m_router.ownsSource(source));
    const auto dispatchResult = send(Command(InputEventType::CommandDown, source, InputCommand::Up, true));
    EXPECT_TRUE(dispatchResult.routing.commandConsumed);
    output = dispatchResult.event;
    EXPECT_EQ(output.command, InputCommand::Down);
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, source, InputCommand::None)).routing.commandConsumed);
}

TEST_F(UiCommandInputTests, DirectContextMenuReleaseCopiesAcceptedAnchorAndDoesNotRepeatOrActivate){
    HitTarget target = Target();
    target.contextMenu = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    const InputSource source{ 9u, 9u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, source, InputCommand::ContextMenu)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, source, InputCommand::Activate, true)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeContextMenu(target.id, target.declarationGeneration));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, source, InputCommand::None)).routing.commandConsumed);
    ContextMenuAction action;
    const auto actionResult = m_router.consumeContextMenu(target.id, target.declarationGeneration);
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.origin, InputActionSource::Command);
    EXPECT_FLOAT_EQ(action.position.x, target.rectangle.x);
    EXPECT_FLOAT_EQ(action.position.y, target.rectangle.y + target.rectangle.height);
    EXPECT_FALSE(m_router.consumeContextMenu(target.id, target.declarationGeneration));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(send(Command(InputEventType::CommandUp, source, InputCommand::None)).routing.commandConsumed);
}

TEST_F(UiCommandInputTests, IndependentControllerSubmitReleasesDoNotTransferOrRepeat){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    const InputSource first{ 1u, 8u };
    const InputSource second{ 2u, 8u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, first, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, second, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(send(Command(InputEventType::CommandUp, { 3u, 8u }, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, first, InputCommand::None, true)).routing.commandConsumed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(m_router.pressed(target.id));
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, second, InputCommand::Submit, true)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, second, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(m_router.pressed(target.id));
    EXPECT_FALSE(send(Command(InputEventType::CommandUp, second, InputCommand::None)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeActivation(target.id));
}

TEST_F(UiCommandInputTests, SubmitCannotReviveWhenFocusReturnsToTheOriginalDeclaration){
    HitTarget first = Target();
    HitTarget second = Target(2u);
    second.focusOnCommit = false;
    second.rectangle.x = 100.0f;
    const HitTarget targets[]{ first, second };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 1u));
    const InputSource activate{ 5u, 5u };
    const InputSource traverse{ 5u, 6u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, activate, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, traverse, InputCommand::FocusNext)).routing.commandConsumed);
    EXPECT_EQ(m_router.focus(), second.id);
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, traverse, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, traverse, InputCommand::FocusPrevious)).routing.commandConsumed);
    EXPECT_EQ(m_router.focus(), first.id);
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, activate, InputCommand::None)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeActivation(first.id));
    EXPECT_FALSE(m_router.consumeActivation(second.id));
}

TEST_F(UiCommandInputTests, EditorCancelRetiresAllSubmitSourcesButKeepsTheirReleasesOwned){
    HitTarget editor = Target();
    editor.textEditable = true;
    editor.activatable = false;
    ASSERT_TRUE(m_router.commitTargets(&editor, 1u, 1u));
    const InputSource first{ 2u, 1u };
    const InputSource second{ 3u, 1u };
    const InputSource cancel{ 2u, 2u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, first, InputCommand::Submit)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, second, InputCommand::Accept)).routing.commandConsumed);
    EXPECT_TRUE(m_router.pressed(editor.id));
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, cancel, InputCommand::Cancel)).routing.commandConsumed);
    EXPECT_EQ(m_router.focus(), editor.id);
    EXPECT_FALSE(m_router.pressed(editor.id));
    for(const InputSource source : { first, second }){
        const auto release = m_router.queue(UiTestSupport::NativeInput(Command(InputEventType::CommandUp, source, InputCommand::None)));
        ASSERT_TRUE(release);
        EXPECT_FALSE(m_router.canSubmitCommand(*release));
        EXPECT_TRUE(m_router.process().commandConsumed);
    }
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, cancel, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, first, InputCommand::Submit)).routing.commandConsumed);
    const auto release = m_router.queue(UiTestSupport::NativeInput(Command(InputEventType::CommandUp, first, InputCommand::None, true)));
    ASSERT_TRUE(release);
    EXPECT_FALSE(release->repeat);
    EXPECT_TRUE(m_router.canSubmitCommand(*release));
    m_router.cancelSubmitCommands(editor.id);
    EXPECT_FALSE(m_router.canSubmitCommand(*release));
    EXPECT_TRUE(m_router.process().commandConsumed);
}

TEST_F(UiCommandInputTests, CustomSubmitBindingPinsItsReleaseAcrossModifierAndProfileChanges){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    const InputKeyBinding initial{ Core::Key::W, 0, Core::InputModifier::Shift, InputCommand::Activate };
    ASSERT_TRUE(m_router.setBindings(&initial, 1u));
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, true)).routing.commandConsumed);
    const InputKeyBinding replacement{ Core::Key::W, 0, 0, InputCommand::FocusNext };
    ASSERT_TRUE(m_router.setBindings(&replacement, 1u));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    const auto release = m_router.queue(UiTestSupport::NativeInput(Key(InputEventType::KeyUp)));
    ASSERT_TRUE(release);
    EXPECT_EQ(release->command, InputCommand::Activate);
    EXPECT_TRUE(m_router.process().commandConsumed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
}

TEST_F(UiCommandInputTests, QueuedReleaseDiscardedByResetRetiresOnlyTheReleasedCommand){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    const InputSource released{ 41u, 1u };
    const InputSource held{ 41u, 2u };
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, released, InputCommand::Activate)).routing.commandConsumed);
    EXPECT_TRUE(send(Command(InputEventType::CommandDown, held, InputCommand::Activate)).routing.commandConsumed);
    ASSERT_TRUE(m_router.queue(Command(InputEventType::CommandUp, released, InputCommand::None)));
    m_router.reset();
    EXPECT_FALSE(m_router.ownsSource(released));
    EXPECT_TRUE(m_router.ownsSource(held));
    EXPECT_TRUE(m_router.actions().empty());
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_FALSE(m_router.pressed(target.id));
    const auto fresh = send(Command(InputEventType::CommandDown, released, InputCommand::Activate));
    EXPECT_FALSE(fresh.event.repeat);
    EXPECT_TRUE(fresh.routing.commandConsumed);
    EXPECT_TRUE(m_router.pressed(target.id));
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, released, InputCommand::None)).routing.commandConsumed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(send(Command(InputEventType::CommandUp, held, InputCommand::None)).routing.commandConsumed);
    EXPECT_FALSE(m_router.consumeActivation(target.id));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

