// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_service.h"

#include <cerrno>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_wayland_clipboard{
static constexpr Array<const char*, 4u> s_Utf8Mimes{
    "text/plain;charset=utf-8", "text/plain;charset=UTF-8", "UTF8_STRING", "text/plain"
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void SelectMime(const char* const mime, const char*& selected, u8& rank){
    if(!mime)
        return;
    for(usize index = 0u; index < s_Utf8Mimes.size(); ++index){
        const u8 candidate = static_cast<u8>(s_Utf8Mimes.size() - index);
        if(AStringView(mime) == s_Utf8Mimes[index] && candidate > rank){
            selected = s_Utf8Mimes[index];
            rank = candidate;
        }
    }
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WaylandClipboardService::attachSeat(wl_seat* const seat, const u32 seatGlobalName){
    NWB_ASSERT(isOwnerThread());
    const bool focused = seat && seat == m_seat && seatGlobalName == m_seatName && m_focused;
    releaseDevices();
    m_seat = seat;
    m_seatName = seat ? seatGlobalName : 0u;
    m_focused = focused;
    if(!seat)
        return;
    if(m_manager){
        m_device = wl_data_device_manager_get_data_device(m_manager, seat);
        static const wl_data_device_listener listener{ onDataOffer, onEnter, onLeave, onMotion, onDrop, onSelection };
        if(m_device && wl_data_device_add_listener(m_device, &listener, this) != 0)
            NWB_FATAL_ASSERT(false);
    }
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    if(m_primaryManager){
        m_primaryDevice = zwp_primary_selection_device_manager_v1_get_device(m_primaryManager, seat);
        static const zwp_primary_selection_device_v1_listener listener{ onPrimaryOffer, onPrimarySelection };
        if(m_primaryDevice && zwp_primary_selection_device_v1_add_listener(m_primaryDevice, &listener, this) != 0)
            NWB_FATAL_ASSERT(false);
    }
#endif
}

void WaylandClipboardService::onRegistryGlobal(
    void* const data,
    wl_registry* const registry,
    const u32 name,
    const char* const interfaceName,
    const u32 version){
    auto& service = *static_cast<WaylandClipboardService*>(data);
    if(AStringView(interfaceName) == wl_data_device_manager_interface.name && !service.m_manager){
        service.m_manager = static_cast<wl_data_device_manager*>(wl_registry_bind(registry, name, &wl_data_device_manager_interface, Min(version, 3u)));
        service.m_managerName = name;
        if(service.m_seat)
            service.attachSeat(service.m_seat, service.m_seatName);
    }
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    else if(AStringView(interfaceName) == zwp_primary_selection_device_manager_v1_interface.name && !service.m_primaryManager){
        service.m_primaryManager = static_cast<zwp_primary_selection_device_manager_v1*>(wl_registry_bind(registry, name, &zwp_primary_selection_device_manager_v1_interface, 1u));
        service.m_primaryManagerName = name;
        if(service.m_seat)
            service.attachSeat(service.m_seat, service.m_seatName);
    }
#endif
}

void WaylandClipboardService::onRegistryRemove(void* const data, wl_registry*, const u32 name){
    auto& service = *static_cast<WaylandClipboardService*>(data);
    if(name == service.m_seatName){
        service.attachSeat(nullptr, 0u);
        return;
    }
    if(name == service.m_managerName){
        const bool focused = service.m_focused;
        service.releaseDevices();
        wl_data_device_manager_destroy(service.m_manager);
        service.m_manager = nullptr;
        service.m_managerName = 0u;
        service.m_focused = focused;
        service.attachSeat(service.m_seat, service.m_seatName);
    }
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    else if(name == service.m_primaryManagerName){
        const bool focused = service.m_focused;
        service.releaseDevices();
        zwp_primary_selection_device_manager_v1_destroy(service.m_primaryManager);
        service.m_primaryManager = nullptr;
        service.m_primaryManagerName = 0u;
        service.m_focused = focused;
        service.attachSeat(service.m_seat, service.m_seatName);
    }
#endif
}

void WaylandClipboardService::addOffer(void* const handle, const ClipboardChannel::Enum channel){
    auto offer = MakeGlobalUnique<Offer>(m_arena, *this);
    offer->handle = handle;
    offer->channel = channel;
    if(m_offers.size() == s_MaxOffers)
        return;
    if(channel == ClipboardChannel::Clipboard){
        static const wl_data_offer_listener listener{ onOfferMime, onOfferActions, onOfferAction };
        if(wl_data_offer_add_listener(static_cast<wl_data_offer*>(handle), &listener, offer.get()) != 0)
            NWB_FATAL_ASSERT(false);
    }
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    else{
        static const zwp_primary_selection_offer_v1_listener listener{ onPrimaryMime };
        if(zwp_primary_selection_offer_v1_add_listener(static_cast<zwp_primary_selection_offer_v1*>(handle), &listener, offer.get()) != 0)
            NWB_FATAL_ASSERT(false);
    }
#endif
    m_offers.push_back(Move(offer));
}

ClipboardStatus::Enum WaylandClipboardService::writeSelection(const ClipboardChannel::Enum channel, const AStringView text){
    if(!m_focused || !m_serial || !m_seat)
        return ClipboardStatus::Unavailable;
    auto source = MakeGlobalUnique<Source>(m_arena, *this, m_arena);
    source->channel = channel;
    if(!text.empty())
        source->text.assign(text.data(), text.size());
    if(channel == ClipboardChannel::Clipboard){
        source->handle = wl_data_device_manager_create_data_source(m_manager);
        if(!source->handle)
            return ClipboardStatus::NativeFailure;
        auto* const native = static_cast<wl_data_source*>(source->handle);
        static const wl_data_source_listener listener{ onSourceTarget, onSourceSend, onSourceCancelled, onSourceDrop, onSourceFinished, onSourceAction };
        if(wl_data_source_add_listener(native, &listener, source.get()) != 0)
            NWB_FATAL_ASSERT(false);
        for(const char* const mime : __hidden_wayland_clipboard::s_Utf8Mimes)
            wl_data_source_offer(native, mime);
        wl_data_device_set_selection(m_device, native, m_serial);
    }
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    else{
        source->handle = zwp_primary_selection_device_manager_v1_create_source(m_primaryManager);
        if(!source->handle)
            return ClipboardStatus::NativeFailure;
        auto* const native = static_cast<zwp_primary_selection_source_v1*>(source->handle);
        static const zwp_primary_selection_source_v1_listener listener{ onPrimarySend, onPrimaryCancelled };
        if(zwp_primary_selection_source_v1_add_listener(native, &listener, source.get()) != 0)
            NWB_FATAL_ASSERT(false);
        for(const char* const mime : __hidden_wayland_clipboard::s_Utf8Mimes)
            zwp_primary_selection_source_v1_offer(native, mime);
        zwp_primary_selection_device_v1_set_selection(m_primaryDevice, native, m_serial);
    }
#else
    else
        return ClipboardStatus::Unsupported;
#endif
    m_sources[channel == ClipboardChannel::Clipboard ? 0u : 1u] = Move(source);
    return wl_display_flush(&m_display) >= 0 || errno == EAGAIN ? ClipboardStatus::Success : ClipboardStatus::NativeFailure;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WaylandClipboardService::onDataOffer(void* const data, wl_data_device*, wl_data_offer* const offer){
    static_cast<WaylandClipboardService*>(data)->addOffer(offer, ClipboardChannel::Clipboard);
}

void WaylandClipboardService::onEnter(void*, wl_data_device*, u32, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer*){}

void WaylandClipboardService::onLeave(void*, wl_data_device*){}

void WaylandClipboardService::onMotion(void*, wl_data_device*, u32, wl_fixed_t, wl_fixed_t){}

void WaylandClipboardService::onDrop(void*, wl_data_device*){}

void WaylandClipboardService::onSelection(void* const data, wl_data_device*, wl_data_offer* const offer){
    static_cast<WaylandClipboardService*>(data)->selectOffer(offer, ClipboardChannel::Clipboard);
}

void WaylandClipboardService::onOfferMime(void* const data, wl_data_offer*, const char* const mime){
    auto& offer = *static_cast<Offer*>(data);
    __hidden_wayland_clipboard::SelectMime(mime, offer.mime, offer.rank);
}

void WaylandClipboardService::onOfferActions(void*, wl_data_offer*, u32){}

void WaylandClipboardService::onOfferAction(void*, wl_data_offer*, u32){}

void WaylandClipboardService::onSourceTarget(void*, wl_data_source*, const char*){}

void WaylandClipboardService::onSourceSend(void* const data, wl_data_source*, const char* const mime, const int fd){
    auto& source = *static_cast<Source*>(data);
    const char* supported = nullptr;
    u8 rank = 0u;
    __hidden_wayland_clipboard::SelectMime(mime, supported, rank);
    if(supported)
        source.service.sendSource(source, fd);
    else{
        int rejectedFd = fd;
        CloseClipboardPipe(rejectedFd);
    }
}

void WaylandClipboardService::onSourceCancelled(void* const data, wl_data_source* const native){
    auto& source = *static_cast<Source*>(data);
    wl_data_source_destroy(native);
    source.handle = nullptr;
}

void WaylandClipboardService::onSourceDrop(void*, wl_data_source*){}

void WaylandClipboardService::onSourceFinished(void*, wl_data_source*){}

void WaylandClipboardService::onSourceAction(void*, wl_data_source*, u32){}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_OS_WITH_PRIMARY_SELECTION)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WaylandClipboardService::onPrimaryOffer(void* const data, zwp_primary_selection_device_v1*, zwp_primary_selection_offer_v1* const offer){
    static_cast<WaylandClipboardService*>(data)->addOffer(offer, ClipboardChannel::PrimarySelection);
}

void WaylandClipboardService::onPrimarySelection(void* const data, zwp_primary_selection_device_v1*, zwp_primary_selection_offer_v1* const offer){
    static_cast<WaylandClipboardService*>(data)->selectOffer(offer, ClipboardChannel::PrimarySelection);
}

void WaylandClipboardService::onPrimaryMime(void* const data, zwp_primary_selection_offer_v1*, const char* const mime){
    auto& offer = *static_cast<Offer*>(data);
    __hidden_wayland_clipboard::SelectMime(mime, offer.mime, offer.rank);
}

void WaylandClipboardService::onPrimarySend(void* const data, zwp_primary_selection_source_v1*, const char* const mime, const int fd){
    auto& source = *static_cast<Source*>(data);
    const char* supported = nullptr;
    u8 rank = 0u;
    __hidden_wayland_clipboard::SelectMime(mime, supported, rank);
    if(supported)
        source.service.sendSource(source, fd);
    else{
        int rejectedFd = fd;
        CloseClipboardPipe(rejectedFd);
    }
}

void WaylandClipboardService::onPrimaryCancelled(void* const data, zwp_primary_selection_source_v1* const native){
    auto& source = *static_cast<Source*>(data);
    zwp_primary_selection_source_v1_destroy(native);
    source.handle = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

