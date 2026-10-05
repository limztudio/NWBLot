// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "text_input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Win32TextInputService::decodeFallbackCharInput(const u32 unit, u32& codePoint){
    codePoint = 0u;
    if(!isOwnerThread() || activeSession().valid())
        return false;
    if(!focused()){
        m_pendingHighSurrogate = 0u;
        return false;
    }
    if(unit > 0xffffu){
        m_pendingHighSurrogate = 0u;
        return false;
    }
    if(unit >= 0xd800u && unit <= 0xdbffu){
        m_pendingHighSurrogate = unit;
        return false;
    }
    if(unit >= 0xdc00u && unit <= 0xdfffu){
        if(m_pendingHighSurrogate == 0u)
            return false;
        codePoint = 0x10000u + ((m_pendingHighSurrogate - 0xd800u) << 10u) + (unit - 0xdc00u);
        m_pendingHighSurrogate = 0u;
        return true;
    }
    m_pendingHighSurrogate = 0u;
    if(unit == 0u)
        return false;
    codePoint = unit;
    return true;
}

bool Win32TextInputService::resetFallbackCharInput(){
    if(!isOwnerThread())
        return false;
    m_pendingHighSurrogate = 0u;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DecodeWin32FallbackCharInput(ITextInputService& service, const u32 unit, u32& codePoint){
    codePoint = 0u;
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Win32Imm32)
        return false;
    return checked_cast<Win32TextInputService*>(&service)->decodeFallbackCharInput(unit, codePoint);
}

bool ResetWin32FallbackCharInput(ITextInputService& service){
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Win32Imm32)
        return false;
    return checked_cast<Win32TextInputService*>(&service)->resetFallbackCharInput();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

