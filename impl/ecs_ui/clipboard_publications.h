// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/os/clipboard.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiClipboardPublicationStatus{
    enum Enum : u8{ Idle, Pending, Published, Unsupported, InvalidText, TooLarge, NativeFailure, WrongThread, QueueFull };
};

struct UiClipboardPublicationResult{
    UiClipboardPublicationStatus::Enum status = UiClipboardPublicationStatus::Idle;
    usize published = 0u;
    usize failed = 0u;
};

inline constexpr usize s_UiClipboardMaxPublications = 16u;

// Copies are immutable native publications. Their owned OS requests survive later edits, focus and widget removal.
// Drain on the OS thread; destruction cancels remaining requests. Service and arena outlive this bounded owner.
class UiClipboardPublications final : NoCopy{
private:
    struct Publication{
        Core::ClipboardRequestToken token;
        Core::ClipboardChannel::Enum channel = Core::ClipboardChannel::Clipboard;
    };


public:
    UiClipboardPublications(Core::Alloc::GlobalArena& arena, Core::IClipboardService& service);
    ~UiClipboardPublications();


public:
    [[nodiscard]] UiClipboardPublicationStatus::Enum request(
        AStringView text, Core::ClipboardChannel::Enum channel = Core::ClipboardChannel::Clipboard
    );
    [[nodiscard]] UiClipboardPublicationResult drain();
    [[nodiscard]] bool cancel();
    [[nodiscard]] usize pending()const noexcept{ return m_requests.size(); }


private:
    Core::IClipboardService& m_service;
    Core::ClipboardCompletion m_completion;
    Vector<Publication, Core::Alloc::GlobalArena> m_requests;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

