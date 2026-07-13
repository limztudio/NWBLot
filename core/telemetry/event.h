
#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Single telemetry wire-format revision shared by the stream header, event header, and per-event identity.
// (Per-payload codecs keep their own independent version constants.) Dev-stage: client + server rebuild
// together, so this is a self-describing revision marker, not a decode gate.
inline constexpr u16 s_TelemetryFormatVersion = 1u;
inline constexpr u32 s_EventMagic = 0x4E574254u; // NWBT


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EventKind{
    enum Enum : u16{
        Unknown,
        TextLog,
        Diagnostic,
        PerfFrame,
        FrameGraphFrame,
        MemoryFrame,
    };
};

namespace CaptureFlag{
    enum Mask : u32{
        None = 0u,
        TextLog = BitMask<u32>(0u),
        Diagnostic = BitMask<u32>(1u),
        Perf = BitMask<u32>(2u),
        FrameGraph = BitMask<u32>(3u),
        All = TextLog | Diagnostic | Perf | FrameGraph,
    };
};

constexpr CaptureFlag::Mask operator|(const CaptureFlag::Mask lhs, const CaptureFlag::Mask rhs)noexcept{
    return static_cast<CaptureFlag::Mask>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}
constexpr CaptureFlag::Mask operator&(const CaptureFlag::Mask lhs, const CaptureFlag::Mask rhs)noexcept{
    return static_cast<CaptureFlag::Mask>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
}
constexpr CaptureFlag::Mask& operator|=(CaptureFlag::Mask& lhs, const CaptureFlag::Mask rhs)noexcept{
    lhs = lhs | rhs;
    return lhs;
}
constexpr CaptureFlag::Mask& operator&=(CaptureFlag::Mask& lhs, const CaptureFlag::Mask rhs)noexcept{
    lhs = lhs & rhs;
    return lhs;
}
constexpr CaptureFlag::Mask operator~(const CaptureFlag::Mask value)noexcept{
    return static_cast<CaptureFlag::Mask>(~static_cast<u32>(value));
}
[[nodiscard]] constexpr bool AnyCapture(const CaptureFlag::Mask value)noexcept{
    return static_cast<u32>(value) != 0u;
}
[[nodiscard]] constexpr bool HasCapture(const CaptureFlag::Mask value, const CaptureFlag::Mask flag)noexcept{
    return AnyCapture(value & flag);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CaptureOptions{
    CaptureFlag::Mask flags = CaptureFlag::None;

    [[nodiscard]] static constexpr CaptureOptions Disabled(){
        return {};
    }

    [[nodiscard]] static constexpr CaptureOptions All(){
        CaptureOptions options;
        options.flags = CaptureFlag::All;
        return options;
    }

    [[nodiscard]] static constexpr CaptureOptions FrameGraphOnly(){
        CaptureOptions options;
        options.flags = CaptureFlag::FrameGraph;
        return options;
    }

    [[nodiscard]] static constexpr CaptureOptions PerfOnly(){
        CaptureOptions options;
        options.flags = CaptureFlag::Perf;
        return options;
    }

    [[nodiscard]] constexpr bool enabled()const{ return AnyCapture(flags); }
    [[nodiscard]] constexpr bool textLogEnabled()const{ return HasCapture(flags, CaptureFlag::TextLog); }
    [[nodiscard]] constexpr bool diagnosticEnabled()const{ return HasCapture(flags, CaptureFlag::Diagnostic); }
    [[nodiscard]] constexpr bool perfEnabled()const{ return HasCapture(flags, CaptureFlag::Perf); }
    [[nodiscard]] constexpr bool frameGraphEnabled()const{ return HasCapture(flags, CaptureFlag::FrameGraph); }
};

[[nodiscard]] constexpr bool IsValidEventKind(EventKind::Enum kind)noexcept;

struct EventHeader{
    u32 magic = s_EventMagic;
    u16 version = s_TelemetryFormatVersion;
    EventKind::Enum kind = EventKind::Unknown;
    u8 reserved = 0u;
    u32 streamId = 0u;
    u64 frameIndex = 0u;
    u64 timestampNanoseconds = 0u;
    u64 payloadBytes = 0u;

    [[nodiscard]] bool valid()const{
        return magic == s_EventMagic
            && kind != EventKind::Unknown
            && IsValidEventKind(kind)
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr bool IsValidEventKind(const EventKind::Enum kind)noexcept{
    switch(kind){
    case EventKind::Unknown:
    case EventKind::TextLog:
    case EventKind::Diagnostic:
    case EventKind::PerfFrame:
    case EventKind::FrameGraphFrame:
    case EventKind::MemoryFrame:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr CaptureFlag::Mask CaptureFlagForEventKind(const EventKind::Enum kind)noexcept{
    switch(kind){
    case EventKind::TextLog:
        return CaptureFlag::TextLog;
    case EventKind::Diagnostic:
        return CaptureFlag::Diagnostic;
    case EventKind::PerfFrame:
    case EventKind::MemoryFrame:
        return CaptureFlag::Perf;
    case EventKind::FrameGraphFrame:
        return CaptureFlag::FrameGraph;
    default:
        return CaptureFlag::None;
    }
}

[[nodiscard]] constexpr bool CaptureAllowsEventKind(const CaptureOptions& capture, const EventKind::Enum kind)noexcept{
    return HasCapture(capture.flags, CaptureFlagForEventKind(kind));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

