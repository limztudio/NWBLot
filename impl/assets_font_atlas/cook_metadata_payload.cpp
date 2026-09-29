// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.h"
#include "source_payload.h"

#include <core/assets/paths.h>

#include <global/base64.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FontAtlasMetadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ReadEmbeddedPayload(
    const Path& path,
    const Core::Metascript::Value& record,
    const u32 expectedBytes,
    Core::Assets::AssetBytes& outBytes){
    const usize packedLimit = FontAtlasSource::MaximumPackedBytes(expectedBytes);
    u32 packedBytes = 0u;
    const Core::Metascript::Value* chunks = Core::Assets::FindMetadataListField(path, record, s_DiagnosticPrefix, "data_base64");
    if(!chunks || packedLimit == 0u || !ReadU32(path, record, "packed_byte_count", packedBytes))
        return false;
    if(packedBytes == 0u || packedBytes > packedLimit)
        return false;
    const usize encodedChars = ((static_cast<usize>(packedBytes) + 2u) / 3u) * 4u;
    const usize chunkCount = (encodedChars + FontAtlasSource::s_Base64ChunkChars - 1u) / FontAtlasSource::s_Base64ChunkChars;
    if(chunks->asList().size() != chunkCount)
        return false;
    usize decodedBytes = 0u;
    usize remainingChars = encodedChars;
    for(const Core::Metascript::Value& chunk : chunks->asList()){
        if(!chunk.isString())
            return false;
        const AStringView encoded = chunk.asString();
        const usize expectedChars = Min(remainingChars, FontAtlasSource::s_Base64ChunkChars);
        usize chunkBytes = 0u;
        if(
            encoded.size() != expectedChars || encoded.empty()
            || (remainingChars > expectedChars && encoded.find('=') != AStringView::npos)
            || !Base64DecodedSize(encoded, packedBytes - decodedBytes, chunkBytes)
        )
            return false;
        decodedBytes += chunkBytes;
        remainingChars -= expectedChars;
    }
    if(decodedBytes != packedBytes || remainingChars != 0u)
        return false;
    Core::Assets::AssetBytes packed(outBytes.get_allocator().arena());
    packed.reserve(packedBytes);
    Core::Assets::AssetBytes chunkBytes(outBytes.get_allocator().arena());
    for(const Core::Metascript::Value& chunk : chunks->asList()){
        if(!DecodeBase64(chunk.asString(), chunkBytes, packedBytes - packed.size()))
            return false;
        packed.insert(packed.end(), chunkBytes.begin(), chunkBytes.end());
    }
    if(!FontAtlasSource::DecodePayload(packed, expectedBytes, outBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas meta '{}': invalid compressed embedded payload"), PathToString<tchar>(path));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

