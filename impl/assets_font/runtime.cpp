// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "binary_payload.h"
#include "font_validation.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_FontAssetCodecAutoRegistrar, FontAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Font::loadBinary(const Core::Assets::AssetBytes& binary){
    usize cursor = 0u;
    FontBinaryPayload::HeaderBinary header;
    if(!Core::Assets::ReadMagicHeaderPayload(
        binary,
        cursor,
        header,
        FontBinaryPayload::s_FontMagic,
        GLB_TEXT("Font::loadBinary"),
        GLB_TEXT("font")
    ))
        return false;
    if(header.version != FontBinaryPayload::s_FontVersion || header.reserved != 0u || header.faceIndex != 0u){
        NWB_LOGGER_ERROR(GLB_TEXT("Font::loadBinary failed: unsupported version, flags, or face index; recook required"));
        return false;
    }
    if(header.byteCount == 0u || header.byteCount > s_FontMaxSourceBytes){
        NWB_LOGGER_ERROR(GLB_TEXT("Font::loadBinary failed: source size must be 1..{} bytes"), s_FontMaxSourceBytes);
        return false;
    }
    if(header.byteCount != binary.size() - cursor){
        NWB_LOGGER_ERROR(GLB_TEXT("Font::loadBinary failed: truncated source bytes or trailing payload"));
        return false;
    }

    Font candidate(m_fontBytes.get_allocator().arena(), virtualPath());
    candidate.m_fontBytes.assign(binary.begin() + cursor, binary.end());
    candidate.m_faceIndex = header.faceIndex;
    if(!candidate.validatePayload())
        return false;
    *this = Move(candidate);
    return true;
}

bool Font::validatePayload()const{
    return checkVirtualPath(GLB_TEXT("Font::validatePayload")) && ValidateFontSource(m_fontBytes, m_faceIndex);
}

void Font::setFontBytes(Core::Assets::AssetBytes&& bytes, const u32 faceIndex)noexcept{
    m_fontBytes = Move(bytes);
    m_faceIndex = faceIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

