// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"

#include <core/common/log.h>

#include <global/text_utils.h>

#include <wchar.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Bool X11TextInputService::OnPreeditStart(XIC context, const XPointer data, XPointer){
    auto& service = *reinterpret_cast<X11TextInputService*>(data);
    if(service.m_resetting || context != service.m_context || service.m_nativeToken != service.activeSession())
        return 0;
    service.m_preedit.clear();
    service.m_caretVisible = true;
    service.publishPreedit();
    return static_cast<int>(s_TextInputMaxEventTextBytes);
}

Bool X11TextInputService::OnPreeditDone(XIC context, const XPointer data, XPointer){
    auto& service = *reinterpret_cast<X11TextInputService*>(data);
    if(service.m_resetting || context != service.m_context || service.m_nativeToken != service.activeSession())
        return 0;
    service.m_preedit.clear();
    service.publishPreedit();
    return 0;
}

Bool X11TextInputService::OnPreeditDraw(XIC context, const XPointer data, const XPointer callData){
    auto& service = *reinterpret_cast<X11TextInputService*>(data);
    if(service.m_resetting || context != service.m_context || service.m_nativeToken != service.activeSession() || !callData)
        return 0;
    const auto& draw = *reinterpret_cast<const XIMPreeditDrawCallbackStruct*>(callData);
    if(draw.chg_first < 0 || draw.chg_length < 0 || draw.caret < 0){
        service.nativeFailure();
        return 0;
    }
    service.m_insertion.clear();
    const bool feedbackOnly = draw.text && !draw.text->string.multi_byte;
    bool valid = true;
    if(feedbackOnly)
        valid = service.m_preedit.moveCaret(static_cast<usize>(draw.caret));
    else{
        if(draw.text)
            valid = service.convertPreeditText(*draw.text);
        if(valid){
            valid = service.m_preedit.replace(
                static_cast<usize>(draw.chg_first), static_cast<usize>(draw.chg_length),
                service.m_insertion, static_cast<usize>(draw.caret)
            );
        }
    }
    if(!valid)
        service.nativeFailure();
    else
        service.publishPreedit();
    return 0;
}

Bool X11TextInputService::OnPreeditCaret(XIC context, const XPointer data, const XPointer callData){
    auto& service = *reinterpret_cast<X11TextInputService*>(data);
    if(service.m_resetting || context != service.m_context || service.m_nativeToken != service.activeSession() || !callData)
        return 0;
    auto& caret = *reinterpret_cast<XIMPreeditCaretCallbackStruct*>(callData);
    const AStringView text = service.m_preedit.text();
    usize position = 0u;
    usize current = 0u;
    while(position < service.m_preedit.caretByte()){
        u32 codePoint = 0u;
        const i32 bytes = DecodeUtf8CodePoint(text.substr(position), codePoint);
        position += static_cast<usize>(bytes);
        ++current;
    }
    switch(caret.direction){
    case XIMForwardChar:
        if(service.m_preedit.caretByte() < text.size())
            ++current;
        break;
    case XIMBackwardChar:
        if(current > 0u)
            --current;
        break;
    case XIMLineStart:
        current = 0u;
        break;
    case XIMLineEnd:
        current = service.m_preedit.moveCaretToEnd();
        break;
    case XIMAbsolutePosition:
        if(caret.position < 0){
            service.nativeFailure();
            return 0;
        }
        current = static_cast<usize>(caret.position);
        break;
    default:
        // Word and vertical motions need client text layout; retain the current scalar position.
        break;
    }
    if(!service.m_preedit.moveCaret(current)){
        service.nativeFailure();
        return 0;
    }
    service.m_caretVisible = caret.style != XIMIsInvisible;
    caret.position = static_cast<int>(current);
    service.publishPreedit();
    return 0;
}

bool X11TextInputService::convertPreeditText(const XIMText& text){
    m_insertion.clear();
    mbstate_t state{};
    const char* next = text.string.multi_byte;
    for(usize index = 0u; index < text.length; ++index){
        u32 codePoint = 0u;
        if(text.encoding_is_wchar)
            codePoint = static_cast<u32>(text.string.wide_char[index]);
        else{
            wchar_t wide = 0;
            const usize bytes = ::mbrtowc(&wide, next, MB_CUR_MAX, &state);
            if(bytes == static_cast<usize>(-1) || bytes == static_cast<usize>(-2) || bytes == 0u)
                return false;
            next += bytes;
            codePoint = static_cast<u32>(wide);
        }
        char bytes[4]{};
        usize count = 0u;
        if(EncodeTextInputCodePoint(codePoint, bytes, count) != TextInputAdmission::Accepted)
            return false;
        if(count > s_TextInputMaxEventTextBytes - m_insertion.size())
            return false;
        m_insertion.append(bytes, count);
    }
    return true;
}

void X11TextInputService::publishPreedit(){
    const TextInputAdmission::Enum status = emitPreedit(
        m_nativeToken, m_preedit.text(), m_preedit.caretByte(), m_preedit.caretByte(), m_caretVisible
    );
    if(status != TextInputAdmission::Accepted && status != TextInputAdmission::InvalidSession)
        nativeFailure();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

