// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/edit_clipboard_controller.h>

#include <core/os/clipboard_service.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_clipboard_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;

class DelayedClipboard final : public QueuedClipboardService{
public:
    explicit DelayedClipboard(Alloc::GlobalArena& arena)
        : QueuedClipboardService(arena)
    {}


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(const ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }

    [[nodiscard]] bool deliver(const ClipboardRequestToken token, const ClipboardStatus::Enum status, const AStringView text = {}){
        return completeNativeRequest(token, status, text);
    }


protected:
    virtual void startNativeRequest(ClipboardRequestToken, ClipboardOperation::Enum, ClipboardChannel::Enum, AStringView)override{}
};

inline constexpr UiTextEditOwner s_Owner{ { 51u }, 2u, 3u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiEditClipboard, CutWaitsForSuccessfulNativePublicationBeforeDeleting){
    Tests::TestArena arena;
    DelayedClipboard service(arena.arena);
    UiEditClipboardController controller(arena.arena, service);
    Ui::EditModel model(arena.arena);
    ASSERT_TRUE(model.setText("abcd"));
    ASSERT_TRUE(model.setSelection(1u, 3u));
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Cut).status, UiEditClipboardStatus::Pending);
    EXPECT_EQ(model.text(), "abcd");
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(controller.token(), ClipboardStatus::NativeFailure));
    EXPECT_EQ(controller.drain(s_Owner, model).status, UiEditClipboardStatus::NativeFailure);
    EXPECT_EQ(model.selectedText(), "bc");
    EXPECT_FALSE(model.canUndo());
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Cut).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(controller.token(), ClipboardStatus::Success));
    EXPECT_TRUE(controller.drain(s_Owner, model).textChanged);
    EXPECT_EQ(model.text(), "ad");
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), "abcd");
    EXPECT_EQ(model.selectedText(), "bc");
}

TEST(UiEditClipboard, CopiedOwnerGenerationsRejectReplacedWidgetDeclarationAndModel){
    Tests::TestArena arena;
    DelayedClipboard service(arena.arena);
    UiEditClipboardController controller(arena.arena, service);
    Ui::EditModel model(arena.arena);
    ASSERT_TRUE(model.setText("base"));
    const UiTextEditOwner replacements[]{ { { 52u }, 2u, 3u }, { { 51u }, 4u, 3u }, { { 51u }, 2u, 5u } };
    for(const auto& owner : replacements){
        ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
        ASSERT_TRUE(service.pump());
        const auto old = controller.token();
        ASSERT_TRUE(service.deliver(old, ClipboardStatus::Success, "old"));
        EXPECT_EQ(controller.drain(owner, model).status, UiEditClipboardStatus::StaleOwner);
        EXPECT_EQ(model.text(), "base");
        EXPECT_FALSE(controller.pending());
        EXPECT_FALSE(service.deliver(old, ClipboardStatus::Success, "late"));
    }
}

TEST(UiEditClipboard, TextRevisionSelectionAndPreeditChangesFencePendingTransfer){
    Tests::TestArena arena;
    DelayedClipboard service(arena.arena);
    UiEditClipboardController controller(arena.arena, service);
    Ui::EditModel model(arena.arena);
    ASSERT_TRUE(model.setText("base"));
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(model.setSelection(0u, 0u));
    EXPECT_EQ(controller.drain(s_Owner, model).status, UiEditClipboardStatus::StaleModel);
    EXPECT_EQ(model.text(), "base");
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(model.replaceSelection("changed"));
    EXPECT_EQ(controller.drain(s_Owner, model).status, UiEditClipboardStatus::StaleModel);
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(model.beginComposition());
    EXPECT_EQ(controller.drain(s_Owner, model).status, UiEditClipboardStatus::StaleModel);
    EXPECT_TRUE(model.composition().active);
    EXPECT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::CompositionActive);
}

TEST(UiEditClipboard, ExplicitCancellationInvalidatesDeliveryAndNewRequestSupersedesOld){
    Tests::TestArena arena;
    DelayedClipboard service(arena.arena);
    UiEditClipboardController controller(arena.arena, service);
    Ui::EditModel model(arena.arena);
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    const auto first = controller.token();
    ASSERT_TRUE(controller.cancel());
    EXPECT_FALSE(service.deliver(first, ClipboardStatus::Success, "old"));
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    const auto second = controller.token();
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    EXPECT_NE(controller.token(), second);
    EXPECT_FALSE(service.deliver(second, ClipboardStatus::Success, "old"));
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(controller.token(), ClipboardStatus::Success, "new"));
    EXPECT_TRUE(controller.drain(s_Owner, model).textChanged);
    EXPECT_EQ(model.text(), "new");
}

TEST(UiEditClipboard, IdenticalExternalResetInvalidatesCopiedPasteDespiteUnchangedTextRevision){
    Tests::TestArena arena;
    DelayedClipboard service(arena.arena);
    UiEditClipboardController controller(arena.arena, service);
    Ui::EditModel model(arena.arena);
    ASSERT_TRUE(model.setText("base"));
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    const auto old = controller.token();
    const u64 revision = model.revision();
    const u64 externalRevision = model.externalRevision();
    ASSERT_TRUE(model.setText("base"));
    EXPECT_EQ(model.revision(), revision);
    EXPECT_GT(model.externalRevision(), externalRevision);
    ASSERT_TRUE(service.deliver(old, ClipboardStatus::Success, "old"));
    EXPECT_EQ(controller.drain(s_Owner, model).status, UiEditClipboardStatus::StaleModel);
    EXPECT_EQ(model.text(), "base");
    EXPECT_FALSE(controller.pending());
}

TEST(UiEditClipboard, TextLimitAndMalformedClipboardPreserveSelectionHistoryAndCommittedText){
    Tests::TestArena arena;
    DelayedClipboard service(arena.arena);
    UiEditClipboardController controller(arena.arena, service);
    Ui::EditModel model(arena.arena, { 4u });
    ASSERT_TRUE(model.setText("abcd"));
    ASSERT_TRUE(model.setSelection(1u, 3u));
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(controller.token(), ClipboardStatus::Success, "xyz"));
    EXPECT_EQ(controller.drain(s_Owner, model).status, UiEditClipboardStatus::TooLarge);
    EXPECT_EQ(model.selectedText(), "bc");
    EXPECT_FALSE(model.canUndo());
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(controller.token(), ClipboardStatus::Success, "\xC0\xAF"));
    EXPECT_EQ(controller.drain(s_Owner, model).status, UiEditClipboardStatus::InvalidText);
    EXPECT_EQ(model.text(), "abcd");
    EXPECT_EQ(model.selectedText(), "bc");
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(controller.token(), ClipboardStatus::Success, ""));
    EXPECT_FALSE(controller.drain(s_Owner, model).textChanged);
    EXPECT_EQ(model.selectedText(), "bc");
}

TEST(UiEditClipboard, ReadOnlyPreventsCutPasteAndCancelsMutationIfPolicyChangesDuringTransfer){
    Tests::TestArena arena;
    DelayedClipboard service(arena.arena);
    UiEditClipboardController controller(arena.arena, service);
    Ui::EditModel model(arena.arena);
    ASSERT_TRUE(model.setText("abcd"));
    ASSERT_TRUE(model.selectAll());
    EXPECT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Cut, ClipboardChannel::Clipboard, true).status,
        UiEditClipboardStatus::ReadOnly);
    EXPECT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste, ClipboardChannel::Clipboard, true).status,
        UiEditClipboardStatus::ReadOnly);
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Copy, ClipboardChannel::Clipboard, true).status,
        UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(controller.token(), ClipboardStatus::Success));
    EXPECT_EQ(controller.drain(s_Owner, model, true).status, UiEditClipboardStatus::Published);
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(service.pump());
    ASSERT_TRUE(service.deliver(controller.token(), ClipboardStatus::Success, "new"));
    EXPECT_EQ(controller.drain(s_Owner, model, true).status, UiEditClipboardStatus::ReadOnly);
    EXPECT_EQ(model.text(), "abcd");
    EXPECT_EQ(model.selectedText(), "abcd");
}

TEST(UiEditClipboard, UnsupportedPrimarySelectionRejectsBeforeTransferAdmission){
    Tests::TestArena arena;
    DelayedClipboard service(arena.arena);
    UiEditClipboardController controller(arena.arena, service);
    Ui::EditModel model(arena.arena);
    EXPECT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste, ClipboardChannel::PrimarySelection).status,
        UiEditClipboardStatus::Unsupported);
}

TEST(UiEditClipboard, OwnerThreadViolationCannotPollApplyOrCancelAHostRequest){
    Tests::TestArena arena;
    DelayedClipboard service(arena.arena);
    UiEditClipboardController controller(arena.arena, service);
    Ui::EditModel model(arena.arena);
    ASSERT_EQ(controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    UiEditClipboardStatus::Enum requestStatus = UiEditClipboardStatus::Idle;
    UiEditClipboardStatus::Enum drainStatus = UiEditClipboardStatus::Idle;
    bool cancelled = true;
    Thread other([&](){
        requestStatus = controller.request(s_Owner, model, Ui::EditClipboardAction::Paste).status;
        drainStatus = controller.drain(s_Owner, model).status;
        cancelled = controller.cancel();
    });
    other.join();
    EXPECT_EQ(requestStatus, UiEditClipboardStatus::WrongThread);
    EXPECT_EQ(drainStatus, UiEditClipboardStatus::WrongThread);
    EXPECT_FALSE(cancelled);
    EXPECT_TRUE(controller.pending());
    EXPECT_TRUE(controller.cancel());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

