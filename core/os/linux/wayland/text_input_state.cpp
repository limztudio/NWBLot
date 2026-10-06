// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_state.h"

#include <core/os/text_input_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_wayland_text_input_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_MaxSurroundingBytes = 4000u;
static constexpr u32 s_SerialHalfRange = 0x80000000u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CanApplyWaylandTextInputDeletion(
    const WaylandTextInputProvenance& provenance,
    const u64 currentRevision,
    const WaylandTextInputSurroundingState& wire,
    const usize anchorByte,
    const usize caretByte,
    const usize beforeBytes,
    const usize afterBytes)noexcept{
    if(
        !provenance.token.valid() || !provenance.revisionKnown || currentRevision == 0u
        || provenance.revision != currentRevision || wire.m_revision != currentRevision
        || wire.m_anchorByte > wire.m_lengthBytes || wire.m_caretByte > wire.m_lengthBytes
        || anchorByte < wire.m_offsetByte || caretByte < wire.m_offsetByte
        || anchorByte - wire.m_offsetByte != wire.m_anchorByte || caretByte - wire.m_offsetByte != wire.m_caretByte
    )
        return false;
    return
        beforeBytes <= Min(wire.m_anchorByte, wire.m_caretByte)
        && afterBytes <= wire.m_lengthBytes - Max(wire.m_anchorByte, wire.m_caretByte)
    ;
}

WaylandTextInputSurrounding SliceWaylandTextInputSurrounding(
    const AStringView text,
    const usize anchorByte,
    const usize caretByte)noexcept{
    using namespace __hidden_wayland_text_input_state;
    if(anchorByte > text.size() || caretByte > text.size())
        return {};
    if(!IsTextInputUtf8Boundary(text, anchorByte) || !IsTextInputUtf8Boundary(text, caretByte))
        return {};
    const usize firstSelection = Min(anchorByte, caretByte);
    const usize lastSelection = Max(anchorByte, caretByte);
    if(lastSelection - firstSelection > s_MaxSurroundingBytes)
        return {};
    const usize spare = s_MaxSurroundingBytes - (lastSelection - firstSelection);
    usize first = firstSelection > spare / 2u ? firstSelection - spare / 2u : 0u;
    usize last = Min(text.size(), first + s_MaxSurroundingBytes);
    if(last < lastSelection)
        last = lastSelection;
    if(last - first < s_MaxSurroundingBytes)
        first = last > s_MaxSurroundingBytes ? last - s_MaxSurroundingBytes : 0u;
    while(first < firstSelection && !IsTextInputUtf8Boundary(text, first))
        ++first;
    while(last > lastSelection && !IsTextInputUtf8Boundary(text, last))
        --last;
    return { text.substr(first, last - first), first, anchorByte - first, caretByte - first, true };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WaylandTextInputSurroundingState::reset()noexcept{
    m_offsetByte = 0u;
    m_lengthBytes = 0u;
    m_anchorByte = 0u;
    m_caretByte = 0u;
    m_revision = 0u;
}

WaylandTextInputSurrounding WaylandTextInputSurroundingState::update(
    const AStringView text,
    const usize anchorByte,
    const usize caretByte,
    const u64 revision)noexcept{
    const WaylandTextInputSurrounding slice = SliceWaylandTextInputSurrounding(text, anchorByte, caretByte);
    if(slice.available){
        m_offsetByte = slice.offsetByte;
        m_lengthBytes = slice.text.size();
        m_anchorByte = slice.anchorByte;
        m_caretByte = slice.caretByte;
        m_revision = revision;
    }
    return slice;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputRect WaylandTextInputRectForPixels(const TextInputRect pixels, const i32 bufferScale)noexcept{
    const i64 scale = Max(bufferScale, 1);
    const i64 x = pixels.x;
    const i64 y = pixels.y;
    const i64 firstX = x >= 0 ? x / scale : -((-x + scale - 1) / scale);
    const i64 firstY = y >= 0 ? y / scale : -((-y + scale - 1) / scale);
    const i64 edgeX = x + pixels.width;
    const i64 edgeY = y + pixels.height;
    const i64 lastX = edgeX >= 0 ? (edgeX + scale - 1) / scale : -((-edgeX) / scale);
    const i64 lastY = edgeY >= 0 ? (edgeY + scale - 1) / scale : -((-edgeY) / scale);
    return {
        static_cast<i32>(firstX), static_cast<i32>(firstY),
        static_cast<i32>(Max(lastX - firstX, i64(1))), static_cast<i32>(Max(lastY - firstY, i64(1)))
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WaylandTextInputSerialTracker::reset()noexcept{
    m_token = {};
    m_firstSerial = 0u;
    m_lastSerial = 0u;
    m_nextRecord = 0u;
    m_recordCount = 0u;
}

void WaylandTextInputSerialTracker::record(
    const u32 serial,
    const TextInputSessionToken token,
    const u64 revision)noexcept{
    if(!token.valid()){
        reset();
        return;
    }
    if(m_token != token){
        reset();
        m_token = token;
        m_firstSerial = serial;
    }
    m_lastSerial = serial;
    m_records[m_nextRecord] = { serial, revision };
    m_nextRecord = (m_nextRecord + 1u) % s_MaxRecords;
    m_recordCount = Min(m_recordCount + 1u, s_MaxRecords);
}

WaylandTextInputProvenance WaylandTextInputSerialTracker::resolve(
    const u32 serial,
    const TextInputSessionToken current)const noexcept{
    using namespace __hidden_wayland_text_input_state;
    if(!current.valid() || m_token != current)
        return {};
    const u32 distance = serial - m_firstSerial;
    const u32 lastDistance = m_lastSerial - m_firstSerial;
    if(distance >= s_SerialHalfRange || lastDistance >= s_SerialHalfRange || distance > lastDistance)
        return {};
    for(usize index = 0u; index < m_recordCount; ++index){
        const Record& record = m_records[index];
        if(record.serial == serial)
            return { current, record.revision, record.revision != 0u };
    }
    return { current, 0u, false };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

