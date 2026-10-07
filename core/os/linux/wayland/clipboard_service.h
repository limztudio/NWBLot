// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "clipboard.h"

#include <core/os/clipboard_service.h>
#include <core/os/linux/pipe_io.h>

#include <wayland-client.h>
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
#include <primary-selection-client-protocol.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class WaylandClipboardService final : public QueuedClipboardService{
private:
    struct Offer{
        WaylandClipboardService& service;
        void* handle = nullptr;
        // Zero is unsupported; positive ranks select the static MIME table in reverse order.
        u8 rank = 0u;
        ClipboardChannel::Enum channel = ClipboardChannel::Clipboard;
        Timer created = TimerNow();

        explicit Offer(WaylandClipboardService& owner) : service(owner){}
        ~Offer();
    };

    struct Source{
        WaylandClipboardService& service;
        AString<Alloc::GlobalArena> text;
        void* handle = nullptr;
        ClipboardChannel::Enum channel = ClipboardChannel::Clipboard;

        Source(WaylandClipboardService& owner, Alloc::GlobalArena& arena)
            : service(owner)
            , text(arena)
        {}
        ~Source();
    };


public:
    static constexpr usize s_MaxOffers = 32u;
    static constexpr usize s_MaxWriters = 8u;
    static constexpr u32 s_TimeoutMs = 5000u;


private:
    [[nodiscard]] static NotNull<const char*> NativeMimeForRank(u8 rank);


public:
    WaylandClipboardService(Alloc::GlobalArena& arena, wl_display& display);
    virtual ~WaylandClipboardService()override;


public:
    [[nodiscard]] bool initialize();
    void attachSeat(wl_seat* seat, u32 seatGlobalName);
    void observeSerial(u32 serial);
    void setKeyboardFocus(bool focused);
    [[nodiscard]] virtual ClipboardCapabilities capabilities(ClipboardChannel::Enum channel)const noexcept override;


protected:
    virtual void startNativeRequest(ClipboardRequestToken token, ClipboardOperation::Enum operation, ClipboardChannel::Enum channel, AStringView text)override;
    virtual void cancelNativeRequest(ClipboardRequestToken token)override;
    virtual void pumpNativeRequests()override;


private:
    void releaseDevices();
    void finishRead(ClipboardStatus::Enum status);
    [[nodiscard]] Offer* findOffer(void* handle, ClipboardChannel::Enum channel)noexcept;
    void addOffer(void* handle, ClipboardChannel::Enum channel);
    void selectOffer(void* handle, ClipboardChannel::Enum channel);
    void sendSource(Source& source, int fd);
    [[nodiscard]] ClipboardStatus::Enum writeSelection(ClipboardChannel::Enum channel, AStringView text);


private:
    static void OnRegistryGlobal(void* data, wl_registry* registry, u32 name, const char* interfaceName, u32 version);
    static void OnRegistryRemove(void* data, wl_registry* registry, u32 name);
    static void OnDataOffer(void* data, wl_data_device* device, wl_data_offer* offer);
    static void OnEnter(void* data, wl_data_device* device, u32 serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y, wl_data_offer* offer);
    static void OnLeave(void* data, wl_data_device* device);
    static void OnMotion(void* data, wl_data_device* device, u32 time, wl_fixed_t x, wl_fixed_t y);
    static void OnDrop(void* data, wl_data_device* device);
    static void OnSelection(void* data, wl_data_device* device, wl_data_offer* offer);
    static void OnOfferMime(void* data, wl_data_offer* offer, const char* mime);
    static void OnOfferActions(void* data, wl_data_offer* offer, u32 actions);
    static void OnOfferAction(void* data, wl_data_offer* offer, u32 action);
    static void OnSourceTarget(void* data, wl_data_source* source, const char* mime);
    static void OnSourceSend(void* data, wl_data_source* source, const char* mime, int fd);
    static void OnSourceCancelled(void* data, wl_data_source* source);
    static void OnSourceDrop(void* data, wl_data_source* source);
    static void OnSourceFinished(void* data, wl_data_source* source);
    static void OnSourceAction(void* data, wl_data_source* source, u32 action);
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    static void OnPrimaryOffer(void* data, zwp_primary_selection_device_v1* device, zwp_primary_selection_offer_v1* offer);
    static void OnPrimarySelection(void* data, zwp_primary_selection_device_v1* device, zwp_primary_selection_offer_v1* offer);
    static void OnPrimaryMime(void* data, zwp_primary_selection_offer_v1* offer, const char* mime);
    static void OnPrimarySend(void* data, zwp_primary_selection_source_v1* source, const char* mime, int fd);
    static void OnPrimaryCancelled(void* data, zwp_primary_selection_source_v1* source);
#endif


private:
    Alloc::GlobalArena& m_arena;
    wl_display& m_display;
    wl_registry* m_registry = nullptr;
    wl_seat* m_seat = nullptr;
    wl_data_device_manager* m_manager = nullptr;
    wl_data_device* m_device = nullptr;
#if defined(NWB_OS_WITH_PRIMARY_SELECTION)
    zwp_primary_selection_device_manager_v1* m_primaryManager = nullptr;
    zwp_primary_selection_device_v1* m_primaryDevice = nullptr;
    u32 m_primaryManagerName = 0u;
#endif
    u32 m_managerName = 0u;
    u32 m_seatName = 0u;
    u32 m_serial = 0u;
    bool m_focused = false;
    Vector<GlobalUniquePtr<Offer>, Alloc::GlobalArena> m_offers;
    Array<Offer*, 2u> m_selected{};
    Array<GlobalUniquePtr<Source>, 2u> m_sources;
    Vector<GlobalUniquePtr<ClipboardPipeWriter>, Alloc::GlobalArena> m_writers;
    ClipboardRequestToken m_readToken;
    ClipboardTextAccumulator m_received;
    int m_readFd = -1;
    Timer m_readDeadline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

