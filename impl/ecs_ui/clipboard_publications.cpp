// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_publications.h"

#include <core/os/clipboard_text.h>

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_clipboard_publications{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static UiClipboardPublicationStatus::Enum Status(const Core::ClipboardStatus::Enum status)noexcept{
    switch(status){
    case Core::ClipboardStatus::Success: return UiClipboardPublicationStatus::Published;
    case Core::ClipboardStatus::Unsupported: return UiClipboardPublicationStatus::Unsupported;
    case Core::ClipboardStatus::InvalidText: return UiClipboardPublicationStatus::InvalidText;
    case Core::ClipboardStatus::TooLarge: return UiClipboardPublicationStatus::TooLarge;
    default: return UiClipboardPublicationStatus::NativeFailure;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiClipboardPublications::UiClipboardPublications(Core::Alloc::GlobalArena& arena, Core::IClipboardService& service)
    : m_service(service)
    , m_completion(arena)
    , m_requests(arena)
{
    m_requests.reserve(s_UiClipboardMaxPublications);
}

UiClipboardPublications::~UiClipboardPublications(){
    if(!cancel())
        TerminateInvariant();
}


UiClipboardPublicationStatus::Enum UiClipboardPublications::request(const AStringView text, const Core::ClipboardChannel::Enum channel){
    if(!m_service.isOwnerThread())
        return UiClipboardPublicationStatus::WrongThread;
    if(channel > Core::ClipboardChannel::PrimarySelection || !m_service.capabilities(channel).writeText)
        return UiClipboardPublicationStatus::Unsupported;
    const auto validation = Core::ValidateClipboardUtf8Text(text);
    if(validation != Core::ClipboardStatus::Success)
        return __hidden_ui_clipboard_publications::Status(validation);
    if(m_requests.size() == s_UiClipboardMaxPublications)
        return UiClipboardPublicationStatus::QueueFull;
    const auto result = m_service.requestWriteText(channel, text);
    if(result.admission != Core::ClipboardAdmission::Accepted)
        return result.admission == Core::ClipboardAdmission::WrongThread
            ? UiClipboardPublicationStatus::WrongThread : UiClipboardPublicationStatus::QueueFull
        ;
    if(!result.token.valid())
        return UiClipboardPublicationStatus::NativeFailure;
    m_requests.push_back({ result.token, channel });
    return UiClipboardPublicationStatus::Pending;
}

UiClipboardPublicationResult UiClipboardPublications::drain(){
    UiClipboardPublicationResult result;
    if(!m_service.isOwnerThread()){
        result.status = UiClipboardPublicationStatus::WrongThread;
        return result;
    }
    for(usize index = 0u; index < m_requests.size();){
        const Publication publication = m_requests[index];
        const auto poll = m_service.poll(publication.token, m_completion);
        if(poll == Core::ClipboardPollResult::Pending){
            ++index;
            continue;
        }
        if(poll == Core::ClipboardPollResult::WrongThread){
            result.status = UiClipboardPublicationStatus::WrongThread;
            return result;
        }
        UiClipboardPublicationStatus::Enum status = UiClipboardPublicationStatus::NativeFailure;
        if(
            poll == Core::ClipboardPollResult::Completed && m_completion.token == publication.token
            && m_completion.operation == Core::ClipboardOperation::WriteText && m_completion.channel == publication.channel
        )
            status = __hidden_ui_clipboard_publications::Status(m_completion.status);
        if(status == UiClipboardPublicationStatus::Published)
            ++result.published;
        else{
            ++result.failed;
            result.status = status;
        }
        m_requests.erase(m_requests.begin() + static_cast<isize>(index));
    }
    if(result.failed == 0u){
        if(result.published != 0u)
            result.status = UiClipboardPublicationStatus::Published;
        else
            result.status = m_requests.empty() ? UiClipboardPublicationStatus::Idle : UiClipboardPublicationStatus::Pending;
    }
    return result;
}

bool UiClipboardPublications::cancel(){
    if(m_requests.empty())
        return true;
    if(!m_service.isOwnerThread())
        return false;
    bool cancelled = true;
    for(const auto& publication : m_requests){
        if(!m_service.cancel(publication.token) && m_service.poll(publication.token, m_completion) != Core::ClipboardPollResult::InvalidRequest)
            cancelled = false;
    }
    if(cancelled)
        m_requests.clear();
    return cancelled;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

