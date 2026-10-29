// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/shadow/light_space_settings.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LightSpaceCaptureIdentity{
    u64 scene = 0u;
    u64 lighting = 0u;
    u64 layout = 0u;
    b8 trusted = false;
};

struct LightSpaceCaptureTicket{
    u64 sequence = 0u;
    b8 reuse = false;


public:
    [[nodiscard]] b8 valid()const noexcept{ return sequence != 0u; }
};

// A capture publishes only after its complete native packet and retained resource states are accepted.
class LightSpaceCaptureHistory final{
public:
    void beginFrame()noexcept;
    [[nodiscard]] LightSpaceCaptureTicket prepare(const LightSpaceCaptureIdentity& identity, SoftwareShadowCaptureCadence::Enum cadence)noexcept;
    void recordCapture(const LightSpaceCaptureTicket& ticket)noexcept;
    [[nodiscard]] b8 accept(const LightSpaceCaptureTicket& ticket)noexcept;
    void invalidate()noexcept;
    [[nodiscard]] u64 acceptedCaptures()const noexcept{ return m_acceptedCaptures; }

    [[nodiscard]] u64 acceptedReuses()const noexcept{ return m_acceptedReuses; }

    [[nodiscard]] u32 acceptedReuseAge()const noexcept{ return m_acceptedReuseCount; }


private:
    LightSpaceCaptureIdentity m_acceptedIdentity;
    LightSpaceCaptureIdentity m_pendingIdentity;
    LightSpaceCaptureTicket m_pendingTicket;
    u64 m_sequence = 0u;
    u64 m_acceptedCaptures = 0u;
    u64 m_acceptedReuses = 0u;
    u8 m_acceptedReuseCount = 0u;
    u8 m_pendingReuseLimit = 0u;
    b8 m_accepted = false;
    b8 m_captureRecorded = false;
    b8 m_frameAccepted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

