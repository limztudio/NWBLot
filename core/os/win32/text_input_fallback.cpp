// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "text_input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u32> Win32TextInputService::decodeFallbackCharInput(const u32 unit)noexcept{
    if(!isOwnerThread() || activeSession().valid())
        return MakeUnexpected(Failure{});
    if(!focused()){
        m_pendingHighSurrogate = 0u;
        return MakeUnexpected(Failure{});
    }
    if(unit > s_MaxU16){
        m_pendingHighSurrogate = 0u;
        return MakeUnexpected(Failure{});
    }
    if(unit >= BasicStringDetail::s_UnicodeHighSurrogateMin && unit <= BasicStringDetail::s_UnicodeHighSurrogateMax){
        m_pendingHighSurrogate = unit;
        return MakeUnexpected(Failure{});
    }
    if(unit >= BasicStringDetail::s_UnicodeLowSurrogateMin && unit <= BasicStringDetail::s_UnicodeLowSurrogateMax){
        if(m_pendingHighSurrogate == 0u)
            return MakeUnexpected(Failure{});
        const u32 codePoint = BasicStringDetail::s_Utf8FourByteMinCodePoint + ((m_pendingHighSurrogate - BasicStringDetail::s_UnicodeHighSurrogateMin) << BasicStringDetail::s_Utf16SurrogatePayloadBits) + (unit - BasicStringDetail::s_UnicodeLowSurrogateMin);
        m_pendingHighSurrogate = 0u;
        return codePoint;
    }
    m_pendingHighSurrogate = 0u;
    if(unit == 0u)
        return MakeUnexpected(Failure{});
    return unit;
}

bool Win32TextInputService::resetFallbackCharInput()noexcept{
    if(!isOwnerThread())
        return false;
    m_pendingHighSurrogate = 0u;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u32> DecodeWin32FallbackCharInput(ITextInputService& service, const u32 unit)noexcept{
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Win32Imm32)
        return MakeUnexpected(Failure{});
    return checked_cast<Win32TextInputService*>(&service)->decodeFallbackCharInput(unit);
}

bool ResetWin32FallbackCharInput(ITextInputService& service)noexcept{
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Win32Imm32)
        return false;
    return checked_cast<Win32TextInputService*>(&service)->resetFallbackCharInput();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

