// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/text_input.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// XIM can forward a filtered physical event after its key release or native editing session has ended.
class X11FilteredKeyHistory final{
private:
    struct Stamp{
        u64 receivedAtMs = 0u;
        u32 timestamp = 0u;
        u32 serial = 0u;
        bool valid = false;
    };


private:
    static constexpr usize s_KeyCount = 256u;
    static constexpr u32 s_HalfRange = 1u << 31u;

    [[nodiscard]] static bool newer(u32 timestamp, u32 serial, const Stamp& previous);
    [[nodiscard]] static bool current(const Stamp& stamp, u64 receivedAtMs);


public:
    void synchronizeSession(TextInputSessionToken session);
    void recordFiltered(u32 keycode, bool released, u32 timestamp, u32 serial, u64 receivedAtMs);
    [[nodiscard]] bool isForwardedDuplicate(
        u32 keycode, bool released, u32 timestamp, u32 serial, bool sent, u64 receivedAtMs
    )const;
    [[nodiscard]] bool isRetiredDuplicate(
        u32 keycode, bool released, u32 timestamp, u32 serial, bool sent, u64 receivedAtMs
    )const;
    void reset();


private:
    // Native timestamps and zero-time XSendEvent input have independent ordering domains.
    TextInputSessionToken m_session;
    Array<Array<Stamp, 4u>, s_KeyCount> m_history{};
    Array<Array<Stamp, 4u>, s_KeyCount> m_retiredHistory{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

