// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/text_input.h>
#include <core/os/platform_values.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct WaylandTextInputSurrounding{
    AStringView text;
    usize offsetByte = 0u;
    usize anchorByte = 0u;
    usize caretByte = 0u;
    b8 available = false;
};

struct WaylandTextInputProvenance{
    TextInputSessionToken token;
    u64 revision = 0u;
    b8 revisionKnown = false;
};

class WaylandTextInputSurroundingState;

// v3 deletion excludes the selected range and may address only the exact surrounding slice sent to the compositor.
[[nodiscard]] b8 CanApplyWaylandTextInputDeletion(const WaylandTextInputProvenance& provenance, u64 currentRevision, const WaylandTextInputSurroundingState& wire, usize anchorByte, usize caretByte, usize beforeBytes, usize afterBytes)noexcept;

// v3 messages accept at most 4000 UTF-8 bytes and must contain the entire selection.
[[nodiscard]] WaylandTextInputSurrounding SliceWaylandTextInputSurrounding(AStringView text, usize anchorByte, usize caretByte)noexcept;

// Record only a surrounding snapshot that fits and was sent to the compositor.
class WaylandTextInputSurroundingState final{
    friend b8 CanApplyWaylandTextInputDeletion(const WaylandTextInputProvenance& provenance, u64 currentRevision, const WaylandTextInputSurroundingState& wire, usize anchorByte, usize caretByte, usize beforeBytes, usize afterBytes)noexcept;


public:
    void reset()noexcept;
    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }
    [[nodiscard]] WaylandTextInputSurrounding update(AStringView text, usize anchorByte, usize caretByte, u64 revision)noexcept;


private:
    usize m_offsetByte = 0u;
    usize m_lengthBytes = 0u;
    usize m_anchorByte = 0u;
    usize m_caretByte = 0u;
    u64 m_revision = 0u;
};

// Round the native client-pixel caret rectangle outward in surface-local coordinates, including negative positions.
[[nodiscard]] TextInputRect WaylandTextInputRectForPixels(TextInputRect pixels, i32 bufferScale)noexcept;

// Caret changes must preserve the reason attached to a deferred surrounding-text update.
class WaylandTextInputDeferredState final{
public:
    void clear()noexcept;
    void caretChanged()noexcept{ m_pending = true; }
    void surroundingChanged(TextInputChangeCause::Enum cause)noexcept;
    [[nodiscard]] b8 pending()const noexcept{ return m_pending; }
    [[nodiscard]] TextInputChangeCause::Enum cause()const noexcept{ return m_cause; }


private:
    b8 m_pending = false;
    TextInputChangeCause::Enum m_cause = TextInputChangeCause::InputMethod;
};

// Retain exact recent snapshot provenance and the full current session's serial interval across wraparound.
// Delayed current-session commits/preedit survive ring eviction; deletion requires a retained revision.
class WaylandTextInputSerialTracker final{
private:
    using Record = ::NWB::Core::WaylandTextInputSerialRecord;


public:
    static constexpr usize s_MaxRecords = ::NWB::Core::s_OsLinuxWaylandTextInputStateMaxRecords;


public:
    void reset()noexcept;
    void record(u32 serial, TextInputSessionToken token, u64 revision)noexcept;
    [[nodiscard]] WaylandTextInputProvenance resolve(u32 serial, TextInputSessionToken current)const noexcept;


private:
    Array<Record, s_MaxRecords> m_records{};
    TextInputSessionToken m_token;
    u32 m_firstSerial = 0u;
    u32 m_lastSerial = 0u;
    usize m_nextRecord = 0u;
    usize m_recordCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

