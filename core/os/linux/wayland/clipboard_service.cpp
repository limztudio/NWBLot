// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_service.h"

#include <core/common/log.h>

#include <cerrno>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


WaylandClipboardService::Offer::~Offer(){
    if(!handle)
        return;
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    if(channel == ClipboardChannel::PrimarySelection)
        zwp_primary_selection_offer_v1_destroy(static_cast<zwp_primary_selection_offer_v1*>(handle));
    else
#endif
        wl_data_offer_destroy(static_cast<wl_data_offer*>(handle));
}

WaylandClipboardService::Source::~Source(){
    if(!handle)
        return;
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    if(channel == ClipboardChannel::PrimarySelection)
        zwp_primary_selection_source_v1_destroy(static_cast<zwp_primary_selection_source_v1*>(handle));
    else
#endif
        wl_data_source_destroy(static_cast<wl_data_source*>(handle));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


WaylandClipboardService::WaylandClipboardService(Alloc::GlobalArena& arena, wl_display& display)
    : QueuedClipboardService(arena)
    , m_arena(arena)
    , m_display(display)
    , m_offers(arena)
    , m_writers(arena)
    , m_received(arena)
{
    m_offers.reserve(s_MaxOffers);
    m_writers.reserve(s_MaxWriters);
}

WaylandClipboardService::~WaylandClipboardService(){
    releaseDevices();
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    if(m_primaryManager)
        zwp_primary_selection_device_manager_v1_destroy(m_primaryManager);
#endif
    if(m_manager)
        wl_data_device_manager_destroy(m_manager);
    if(m_registry)
        wl_registry_destroy(m_registry);
}

bool WaylandClipboardService::initialize(){
    m_registry = wl_display_get_registry(&m_display);
    if(!m_registry)
        return false;
    static const wl_registry_listener listener{ OnRegistryGlobal, OnRegistryRemove };
    if(wl_registry_add_listener(m_registry, &listener, this) != 0 || wl_display_roundtrip(&m_display) < 0)
        return false;
    return true;
}

void WaylandClipboardService::releaseDevices(){
    if(m_readToken.valid())
        finishRead(ClipboardStatus::Unavailable);
    m_selected = {};
    m_offers.clear();
    m_sources[0].reset();
    m_sources[1].reset();
    m_writers.clear();
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    if(m_primaryDevice){
        zwp_primary_selection_device_v1_destroy(m_primaryDevice);
        m_primaryDevice = nullptr;
    }
#endif
    if(m_device){
        if(wl_data_device_get_version(m_device) >= 2u)
            wl_data_device_release(m_device);
        else
            wl_data_device_destroy(m_device);
        m_device = nullptr;
    }
    m_focused = false;
    m_serial = 0u;
}

void WaylandClipboardService::observeSerial(const u32 serial){
    NWB_ASSERT(isOwnerThread());
    if(m_seat && serial)
        m_serial = serial;
}

void WaylandClipboardService::setKeyboardFocus(const bool focused){
    NWB_ASSERT(isOwnerThread());
    m_focused = focused && m_seat;
    if(!m_focused){
        m_serial = 0u;
        m_selected = {};
        m_offers.clear();
        if(m_readToken.valid())
            finishRead(ClipboardStatus::Unavailable);
    }
}

ClipboardCapabilities WaylandClipboardService::capabilities(const ClipboardChannel::Enum channel)const noexcept{
    bool available = channel == ClipboardChannel::Clipboard && m_device;
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    if(channel == ClipboardChannel::PrimarySelection)
        available = m_primaryDevice;
#endif
    return { available, available };
}

void WaylandClipboardService::startNativeRequest(
    const ClipboardRequestToken token,
    const ClipboardOperation::Enum operation,
    const ClipboardChannel::Enum channel,
    const AStringView text){
    if(operation == ClipboardOperation::WriteText){
        const ClipboardStatus::Enum status = writeSelection(channel, text);
        if(!completeNativeRequest(token, status))
            NWB_FATAL_ASSERT(false);
        return;
    }
    Offer* const offer = m_selected[channel == ClipboardChannel::Clipboard ? 0u : 1u];
    if(!m_focused || !offer || offer->rank == 0u){
        if(!completeNativeRequest(token, ClipboardStatus::Unavailable))
            NWB_FATAL_ASSERT(false);
        return;
    }
    int writeFd = -1;
    if(!OpenClipboardPipe(m_readFd, writeFd)){
        if(!completeNativeRequest(token, ClipboardStatus::NativeFailure))
            NWB_FATAL_ASSERT(false);
        return;
    }
    m_readToken = token;
    m_readDeadline = TimerAddMS(TimerNow(), s_TimeoutMs);
    m_received.clear();
    const NotNull<const char*> mime = NativeMimeForRank(offer->rank);
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    if(channel == ClipboardChannel::PrimarySelection)
        zwp_primary_selection_offer_v1_receive(static_cast<zwp_primary_selection_offer_v1*>(offer->handle), mime.get(), writeFd);
    else
#endif
        wl_data_offer_receive(static_cast<wl_data_offer*>(offer->handle), mime.get(), writeFd);
    CloseClipboardPipe(writeFd);
    if(wl_display_flush(&m_display) < 0 && errno != EAGAIN)
        finishRead(ClipboardStatus::NativeFailure);
}

void WaylandClipboardService::cancelNativeRequest(const ClipboardRequestToken token){
    if(m_readToken != token)
        return;
    m_readToken = {};
    CloseClipboardPipe(m_readFd);
    m_received.clear();
}

void WaylandClipboardService::finishRead(const ClipboardStatus::Enum status){
    const ClipboardRequestToken token = m_readToken;
    m_readToken = {};
    CloseClipboardPipe(m_readFd);
    if(token.valid() && !completeNativeRequest(token, status, m_received.text()))
        NWB_FATAL_ASSERT(false);
    m_received.clear();
}

void WaylandClipboardService::pumpNativeRequests(){
    const Timer now = TimerNow();
    if(m_readToken.valid()){
        bool finished = false;
        const ClipboardStatus::Enum status = now >= m_readDeadline
            ? ClipboardStatus::Unavailable
            : ReadClipboardPipe(m_readFd, m_received, finished)
        ;
        if(finished || status != ClipboardStatus::Success)
            finishRead(status);
    }
    for(usize index = m_writers.size(); index > 0u; --index){
        ClipboardPipeWriter& writer = *m_writers[index - 1u];
        const ClipboardStatus::Enum status = writer.advance();
        if(writer.finished() || status != ClipboardStatus::Success)
            m_writers.erase(m_writers.begin() + static_cast<isize>(index - 1u));
    }
    for(usize index = m_offers.size(); index > 0u; --index){
        Offer* const offer = m_offers[index - 1u].get();
        if(offer != m_selected[0] && offer != m_selected[1] && now >= TimerAddMS(offer->created, s_TimeoutMs))
            m_offers.erase(m_offers.begin() + static_cast<isize>(index - 1u));
    }
    if(wl_display_flush(&m_display) < 0 && errno != EAGAIN && m_readToken.valid())
        finishRead(ClipboardStatus::NativeFailure);
}

WaylandClipboardService::Offer* WaylandClipboardService::findOffer(void* const handle, const ClipboardChannel::Enum channel)noexcept{
    for(const auto& offer : m_offers){
        if(offer->handle == handle && offer->channel == channel)
            return offer.get();
    }
    return nullptr;
}

void WaylandClipboardService::selectOffer(void* const handle, const ClipboardChannel::Enum channel){
    const usize index = channel == ClipboardChannel::Clipboard ? 0u : 1u;
    Offer* const previous = m_selected[index];
    m_selected[index] = handle ? findOffer(handle, channel) : nullptr;
    if(previous && previous != m_selected[index]){
        for(usize offerIndex = 0u; offerIndex < m_offers.size(); ++offerIndex){
            if(m_offers[offerIndex].get() == previous){
                m_offers.erase(m_offers.begin() + static_cast<isize>(offerIndex));
                return;
            }
        }
    }
}

void WaylandClipboardService::sendSource(Source& source, const int fd){
    if(m_writers.size() == s_MaxWriters){
        int rejectedFd = fd;
        CloseClipboardPipe(rejectedFd);
        return;
    }
    auto writer = MakeGlobalUnique<ClipboardPipeWriter>(m_arena, m_arena);
    if(writer->begin(fd, source.text))
        m_writers.push_back(Move(writer));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GlobalUniquePtr<IClipboardService> CreateWaylandClipboardService(
    Alloc::GlobalArena& arena,
    wl_display& display){
    auto service = MakeGlobalUnique<WaylandClipboardService>(arena, arena, display);
    if(!service->initialize()){
        NWB_LOGGER_ERROR(NWB_TEXT("Wayland clipboard initialization failed"));
        return {};
    }
    return service;
}

void AttachWaylandClipboardSeat(IClipboardService& service, wl_seat* const seat, const u32 seatGlobalName){
    checked_cast<WaylandClipboardService*>(&service)->attachSeat(seat, seatGlobalName);
}

void ObserveWaylandClipboardInputSerial(IClipboardService& service, const u32 serial){
    checked_cast<WaylandClipboardService*>(&service)->observeSerial(serial);
}

void SetWaylandClipboardKeyboardFocus(IClipboardService& service, const bool focused){
    checked_cast<WaylandClipboardService*>(&service)->setKeyboardFocus(focused);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

