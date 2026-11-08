// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/text_input.h>
#include <core/os/platform_values.adhd.h>
#include <core/os/linux/x11/text_input_key_history_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// XIM can forward a filtered physical event after its key release or native editing session has ended.
class X11FilteredKeyHistory final{
private:
    using Stamp = X11FilteredKeyHistoryStamp;


private:
    static constexpr usize s_KeyCount = ::NWB::Core::s_OsLinuxX11TextInputKeyHistoryKeyCount;
    // Press and release, each for native timestamps and for zero-time XSendEvent input.
    static constexpr usize s_KindCount = ::NWB::Core::s_OsLinuxX11TextInputKeyHistoryKindCount;
    static constexpr u32 s_ZeroTimeKindOffset = ::NWB::Core::s_OsLinuxX11TextInputKeyHistoryZeroTimeKindOffset;
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
    // Native timestamps and zero-time XSendEvent input have independent ordering domains.
    TextInputSessionToken m_session;
    Array<Array<Stamp, s_KindCount>, s_KeyCount> m_history{};
    Array<Array<Stamp, s_KindCount>, s_KeyCount> m_retiredHistory{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

