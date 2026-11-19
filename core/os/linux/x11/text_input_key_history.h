// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/text_input.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class X11FilteredKeyHistory final{
private:
    struct Stamp{
        u64 receivedAtMs = 0u;
        u32 timestamp = 0u;
        u32 serial = 0u;
        b8 valid = false;
    };


private:
    static constexpr usize s_KeyCount = 256u;
    static constexpr usize s_KindCount = 4u;
    static constexpr u32 s_ZeroTimeKindOffset = 2u;
    static constexpr u32 s_HalfRange = 1u << 31u;


private:
    [[nodiscard]] static b8 Newer(u32 timestamp, u32 serial, const Stamp& previous)noexcept;
    [[nodiscard]] static b8 Current(const Stamp& stamp, u64 receivedAtMs)noexcept;


public:
    void synchronizeSession(TextInputSessionToken session)noexcept;
    void recordFiltered(u32 keycode, b8 released, u32 timestamp, u32 serial, u64 receivedAtMs)noexcept;
    [[nodiscard]] b8 isForwardedDuplicate(u32 keycode, b8 released, u32 timestamp, u32 serial, b8 sent, u64 receivedAtMs)const noexcept;
    [[nodiscard]] b8 isRetiredDuplicate(u32 keycode, b8 released, u32 timestamp, u32 serial, b8 sent, u64 receivedAtMs)const noexcept;
    void reset()noexcept;


private:
    TextInputSessionToken m_session;
    Array<Array<Stamp, s_KindCount>, s_KeyCount> m_history{};
    Array<Array<Stamp, s_KindCount>, s_KeyCount> m_retiredHistory{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

