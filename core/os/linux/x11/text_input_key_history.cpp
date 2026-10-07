// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_key_history.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool X11FilteredKeyHistory::Newer(const u32 timestamp, const u32 serial, const Stamp& previous)noexcept{
    const u32 difference = timestamp == previous.timestamp ? serial - previous.serial : timestamp - previous.timestamp;
    return difference != 0u && difference < s_HalfRange;
}

bool X11FilteredKeyHistory::Current(const Stamp& stamp, const u64 receivedAtMs)noexcept{
    // A 32-bit X timestamp cannot order events across an inactivity interval of half its range.
    return stamp.valid && receivedAtMs >= stamp.receivedAtMs && receivedAtMs - stamp.receivedAtMs < s_HalfRange;
}

void X11FilteredKeyHistory::synchronizeSession(const TextInputSessionToken session)noexcept{
    if(session == m_session)
        return;
    for(usize key = 0u; key < s_KeyCount; ++key){
        for(usize kind = 0u; kind < m_history[key].size(); ++kind){
            const Stamp& source = m_history[key][kind];
            Stamp& retired = m_retiredHistory[key][kind];
            if(
                source.valid && (
                    !retired.valid || source.receivedAtMs > retired.receivedAtMs
                    || (source.receivedAtMs == retired.receivedAtMs && Newer(source.timestamp, source.serial, retired))
                )
            )
                retired = source;
        }
    }
    m_session = session;
}

void X11FilteredKeyHistory::recordFiltered(
    const u32 keycode, const bool released, const u32 timestamp, const u32 serial, const u64 receivedAtMs
)noexcept{
    if(keycode == 0u || keycode >= s_KeyCount)
        return;
    Stamp& stamp = m_history[keycode][(released ? 1u : 0u) + (timestamp == 0u ? 2u : 0u)];
    if(!Current(stamp, receivedAtMs) || Newer(timestamp, serial, stamp))
        stamp = { receivedAtMs, timestamp, serial, true };
}

bool X11FilteredKeyHistory::isForwardedDuplicate(
    const u32 keycode, const bool released, const u32 timestamp, const u32 serial, const bool sent, const u64 receivedAtMs
)const noexcept{
    if(keycode == 0u || keycode >= s_KeyCount || sent)
        return false;
    const Stamp& stamp = m_history[keycode][(released ? 1u : 0u) + (timestamp == 0u ? 2u : 0u)];
    return Current(stamp, receivedAtMs) && !Newer(timestamp, serial, stamp);
}

bool X11FilteredKeyHistory::isRetiredDuplicate(
    const u32 keycode, const bool released, const u32 timestamp, const u32 serial, const bool sent, const u64 receivedAtMs
)const noexcept{
    if(keycode == 0u || keycode >= s_KeyCount || sent)
        return false;
    const Stamp& stamp = m_retiredHistory[keycode][(released ? 1u : 0u) + (timestamp == 0u ? 2u : 0u)];
    return Current(stamp, receivedAtMs) && !Newer(timestamp, serial, stamp);
}

void X11FilteredKeyHistory::reset()noexcept{
    m_session = {};
    for(auto& key : m_history)
        key.fill({});
    for(auto& key : m_retiredHistory)
        key.fill({});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

