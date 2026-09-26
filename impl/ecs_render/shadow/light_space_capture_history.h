// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "light_space_settings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LightSpaceCaptureIdentity{
    u64 scene = 0u;
    u64 lighting = 0u;
    u64 layout = 0u;
    bool trusted = false;
};

struct LightSpaceCaptureTicket{
    u64 sequence = 0u;
    bool reuse = false;

    [[nodiscard]] bool valid()const noexcept{ return sequence != 0u; }
};

// A capture publishes only after its complete native packet and retained resource states are accepted.
class LightSpaceCaptureHistory final{
public:
    void beginFrame()noexcept;
    [[nodiscard]] LightSpaceCaptureTicket prepare(const LightSpaceCaptureIdentity& identity, SoftwareShadowCaptureCadence::Enum cadence)noexcept;
    void recordCapture(const LightSpaceCaptureTicket& ticket)noexcept;
    [[nodiscard]] bool accept(const LightSpaceCaptureTicket& ticket)noexcept;
    void invalidate()noexcept;
    [[nodiscard]] u64 acceptedCaptures()const noexcept{ return m_acceptedCaptures; }
    [[nodiscard]] u64 acceptedReuses()const noexcept{ return m_acceptedReuses; }

private:
    LightSpaceCaptureIdentity m_acceptedIdentity;
    LightSpaceCaptureIdentity m_pendingIdentity;
    LightSpaceCaptureTicket m_pendingTicket;
    u64 m_sequence = 0u;
    u64 m_acceptedCaptures = 0u;
    u64 m_acceptedReuses = 0u;
    bool m_accepted = false;
    bool m_reuseConsumed = false;
    bool m_captureRecorded = false;
    bool m_frameAccepted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

