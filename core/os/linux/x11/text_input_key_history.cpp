// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_key_history.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool X11FilteredKeyHistory::newer(const u32 timestamp, const u32 serial, const Stamp& previous){
    const u32 difference = timestamp == previous.timestamp ? serial - previous.serial : timestamp - previous.timestamp;
    return difference != 0u && difference < s_HalfRange;
}

bool X11FilteredKeyHistory::current(const Stamp& stamp, const u64 receivedAtMs){
    // A 32-bit X timestamp cannot order events across an inactivity interval of half its range.
    return stamp.valid && receivedAtMs >= stamp.receivedAtMs && receivedAtMs - stamp.receivedAtMs < s_HalfRange;
}

void X11FilteredKeyHistory::recordFiltered(
    const u32 keycode, const bool released, const u32 timestamp, const u32 serial, const u64 receivedAtMs){
    if(keycode == 0u || keycode >= s_KeyCount)
        return;
    Stamp& stamp = m_history[keycode][(released ? 1u : 0u) + (timestamp == 0u ? 2u : 0u)];
    if(!current(stamp, receivedAtMs) || newer(timestamp, serial, stamp))
        stamp = { receivedAtMs, timestamp, serial, true };
}

bool X11FilteredKeyHistory::isForwardedDuplicate(
    const u32 keycode, const bool released, const u32 timestamp, const u32 serial, const bool sent, const u64 receivedAtMs)const{
    if(keycode == 0u || keycode >= s_KeyCount || sent)
        return false;
    const Stamp& stamp = m_history[keycode][(released ? 1u : 0u) + (timestamp == 0u ? 2u : 0u)];
    return current(stamp, receivedAtMs) && !newer(timestamp, serial, stamp);
}

void X11FilteredKeyHistory::reset(){
    for(auto& key : m_history)
        key.fill({});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

