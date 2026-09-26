// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_capture_history.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LightSpaceCaptureHistory::beginFrame()noexcept{
    // A skipped or rejected frame cannot extend the lifetime of the prior map generation.
    if(!m_frameAccepted)
        m_accepted = false;
    m_pendingTicket = {};
    m_captureRecorded = false;
    m_frameAccepted = false;
}

LightSpaceCaptureTicket LightSpaceCaptureHistory::prepare(
    const LightSpaceCaptureIdentity& identity, const SoftwareShadowCaptureCadence::Enum cadence)noexcept{
    const bool reuse = cadence == SoftwareShadowCaptureCadence::ReuseOneFrame
        && m_accepted && !m_reuseConsumed && identity.trusted && m_acceptedIdentity.trusted
        && identity.scene == m_acceptedIdentity.scene && identity.lighting == m_acceptedIdentity.lighting
        && identity.layout == m_acceptedIdentity.layout;
    ++m_sequence;
    if(m_sequence == 0u)
        ++m_sequence;
    m_pendingIdentity = identity;
    m_pendingTicket = { m_sequence, reuse };
    m_captureRecorded = false;
    // Refresh writes the maps in place. Their old identity must be unavailable even if recording or submission fails.
    if(!reuse)
        m_accepted = false;
    return m_pendingTicket;
}

void LightSpaceCaptureHistory::recordCapture(const LightSpaceCaptureTicket& ticket)noexcept{
    if(ticket.valid() && ticket.sequence == m_pendingTicket.sequence && !ticket.reuse && !m_pendingTicket.reuse)
        m_captureRecorded = true;
}

bool LightSpaceCaptureHistory::accept(const LightSpaceCaptureTicket& ticket)noexcept{
    if(!ticket.valid() || ticket.sequence != m_pendingTicket.sequence || ticket.reuse != m_pendingTicket.reuse)
        return false;
    if(ticket.reuse){
        if(!m_accepted || m_reuseConsumed)
            return false;
        m_reuseConsumed = true;
        ++m_acceptedReuses;
    }
    else{
        if(!m_captureRecorded)
            return false;
        m_acceptedIdentity = m_pendingIdentity;
        m_accepted = m_pendingIdentity.trusted;
        m_reuseConsumed = false;
        ++m_acceptedCaptures;
    }
    m_pendingTicket = {};
    m_frameAccepted = true;
    return true;
}

void LightSpaceCaptureHistory::invalidate()noexcept{
    m_accepted = false;
    m_pendingTicket = {};
    m_captureRecorded = false;
    m_frameAccepted = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

