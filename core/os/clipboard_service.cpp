// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_service.h"

#include <global/atomic.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_service{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u64 AllocateServiceIdentity(){
    static Atomic<u64> s_NextIdentity{ 1u };
    const u64 identity = s_NextIdentity.fetch_add(1u, MemoryOrder::relaxed);
    if(identity == 0u)
        TerminateInvariant();
    return identity;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


QueuedClipboardService::QueuedClipboardService(Alloc::GlobalArena& arena)
    : m_arena(arena)
    , m_requests(arena)
    , m_ownerThread(QueryCurrentThreadId())
    , m_serviceIdentity(__hidden_clipboard_service::AllocateServiceIdentity())
{
    m_requests.reserve(s_ClipboardMaxOutstandingRequests);
}
QueuedClipboardService::~QueuedClipboardService(){
    NWB_ASSERT(isOwnerThread());
}

ClipboardRequestResult QueuedClipboardService::requestReadText(const ClipboardChannel::Enum channel){
    return enqueue(channel, ClipboardOperation::ReadText, {});
}

ClipboardRequestResult QueuedClipboardService::requestWriteText(const ClipboardChannel::Enum channel, const AStringView text){
    return enqueue(channel, ClipboardOperation::WriteText, text);
}

ClipboardPollResult::Enum QueuedClipboardService::poll(const ClipboardRequestToken token, ClipboardCompletion& completion){
    if(!isOwnerThread())
        return ClipboardPollResult::WrongThread;
    Request* const request = findRequest(token);
    if(!request)
        return ClipboardPollResult::InvalidRequest;
    if(!request->completed)
        return ClipboardPollResult::Pending;

    completion.text.assign(request->text.data(), request->text.size());
    completion.token = request->token;
    completion.operation = request->operation;
    completion.channel = request->channel;
    completion.status = request->status;
    request->token = {};
    request->text.clear();
    return ClipboardPollResult::Completed;
}

bool QueuedClipboardService::cancel(const ClipboardRequestToken token){
    if(!isOwnerThread())
        return false;
    Request* const request = findRequest(token);
    if(!request)
        return false;
    request->token = {};
    if(!request->executing)
        request->text.clear();
    return true;
}

bool QueuedClipboardService::pump(){
    if(!isOwnerThread() || m_pumping)
        return false;
    m_pumping = true;
    ScopeExit finishPump([this]()noexcept{ m_pumping = false; });
    const u64 generationLimit = m_nextGeneration;
    for(;;){
        Request* next = nullptr;
        for(Request& request : m_requests){
            if(
                request.token.valid()
                && request.token.generation < generationLimit
                && !request.completed
                && (!next || request.token.generation < next->token.generation)
            )
                next = &request;
        }
        if(!next)
            return true;

        const ClipboardCapabilities support = capabilities(next->channel);
        const bool supported = next->operation == ClipboardOperation::ReadText ? support.readText : support.writeText;
        next->executing = true;
        ScopeExit finishRequest([next]()noexcept{ next->executing = false; });
        if(!supported)
            next->status = ClipboardStatus::Unsupported;
        else if(next->operation == ClipboardOperation::ReadText)
            next->status = readNativeText(next->channel, next->text);
        else
            next->status = writeNativeText(next->channel, next->text);
        if(!next->token.valid() || next->operation == ClipboardOperation::WriteText || next->status != ClipboardStatus::Success)
            next->text.clear();
        next->completed = next->token.valid();
    }
}

ClipboardRequestResult QueuedClipboardService::enqueue(
    const ClipboardChannel::Enum channel,
    const ClipboardOperation::Enum operation,
    const AStringView text){
    if(!isOwnerThread())
        return { .token = {}, .admission = ClipboardAdmission::WrongThread };

    Request* available = nullptr;
    for(Request& request : m_requests){
        if(!request.token.valid() && !request.executing){
            available = &request;
            break;
        }
    }
    if(!available){
        if(m_requests.size() == s_ClipboardMaxOutstandingRequests)
            return { .token = {}, .admission = ClipboardAdmission::QueueFull };
        available = &m_requests.emplace_back(m_arena);
    }
    if(m_nextGeneration == Limit<u64>::s_Max)
        TerminateInvariant();

    available->token = { .service = m_serviceIdentity, .generation = m_nextGeneration };
    ++m_nextGeneration;
    available->operation = operation;
    available->channel = channel;
    available->completed = false;
    available->status = ClipboardStatus::Unavailable;
    available->text.clear();
    if(text.size() > s_ClipboardMaxTextBytes){
        available->completed = true;
        available->status = ClipboardStatus::TooLarge;
    }
    else if(text.find('\0') != AStringView::npos){
        available->completed = true;
        available->status = ClipboardStatus::InvalidText;
    }
    else if(!text.empty())
        available->text.assign(text.data(), text.size());
    return { .token = available->token, .admission = ClipboardAdmission::Accepted };
}

QueuedClipboardService::Request* QueuedClipboardService::findRequest(const ClipboardRequestToken token){
    if(!token.valid() || token.service != m_serviceIdentity)
        return nullptr;
    for(Request& request : m_requests){
        if(request.token == token)
            return &request;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

