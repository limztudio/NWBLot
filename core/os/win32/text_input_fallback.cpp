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
    if(unit > 0xffffu){
        m_pendingHighSurrogate = 0u;
        return MakeUnexpected(Failure{});
    }
    if(unit >= 0xd800u && unit <= 0xdbffu){
        m_pendingHighSurrogate = unit;
        return MakeUnexpected(Failure{});
    }
    if(unit >= 0xdc00u && unit <= 0xdfffu){
        if(m_pendingHighSurrogate == 0u)
            return MakeUnexpected(Failure{});
        const u32 codePoint = 0x10000u + ((m_pendingHighSurrogate - 0xd800u) << 10u) + (unit - 0xdc00u);
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


Expected<u32> DecodeWin32FallbackCharInput(ITextInputService& service, const u32 unit){
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Win32Imm32)
        return MakeUnexpected(Failure{});
    return checked_cast<Win32TextInputService*>(&service)->decodeFallbackCharInput(unit);
}

bool ResetWin32FallbackCharInput(ITextInputService& service){
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Win32Imm32)
        return false;
    return checked_cast<Win32TextInputService*>(&service)->resetFallbackCharInput();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

