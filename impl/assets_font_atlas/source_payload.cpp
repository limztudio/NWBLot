// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "source_payload.h"

#include <global/scope_exit.h>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd/zstd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_source{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr int s_CompressionLevel = 9;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool SetCompressionParameter(ZSTD_CCtx& context, const ZSTD_cParameter parameter, const int value){
    if(ZSTD_isError(ZSTD_CCtx_setParameter(&context, parameter, value))){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas source compression parameter failed"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FontAtlasSource{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize MaximumPackedBytes(const u32 rawBytes){
    if(rawBytes == 0u || rawBytes > s_MaxPayloadBytes)
        return 0u;
    return ZSTD_compressBound(rawBytes);
}

bool EncodePayload(const Core::Assets::AssetBytes& raw, Core::Assets::AssetBytes& packed){
    using namespace __hidden_font_atlas_source;
    if(raw.empty() || raw.size() > s_MaxPayloadBytes)
        return false;
    ZSTD_CCtx* context = ZSTD_createCCtx();
    if(!context){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas source compression context allocation failed"));
        return false;
    }
    ScopeExit cleanup([context]()noexcept{
        if(ZSTD_isError(ZSTD_freeCCtx(context)))
            NWB_ASSERT(false);
    });

    if(
        !SetCompressionParameter(*context, ZSTD_c_compressionLevel, s_CompressionLevel)
        || !SetCompressionParameter(*context, ZSTD_c_contentSizeFlag, 1)
        || !SetCompressionParameter(*context, ZSTD_c_checksumFlag, 1)
        || !SetCompressionParameter(*context, ZSTD_c_dictIDFlag, 0)
        || !SetCompressionParameter(*context, ZSTD_c_nbWorkers, 0)
    )
        return false;
    Core::Assets::AssetBytes candidate(packed.get_allocator().arena());
    candidate.resize(MaximumPackedBytes(static_cast<u32>(raw.size())));
    const usize size = ZSTD_compress2(context, candidate.data(), candidate.size(), raw.data(), raw.size());
    if(ZSTD_isError(size)){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas source compression failed"));
        return false;
    }
    candidate.resize(size);
    packed = Move(candidate);
    return true;
}

bool DecodePayload(const Core::Assets::AssetBytes& packed, const u32 expectedBytes, Core::Assets::AssetBytes& raw){
    const usize packedLimit = MaximumPackedBytes(expectedBytes);
    if(packedLimit == 0u || packed.empty() || packed.size() > packedLimit)
        return false;
    ZSTD_frameHeader header = {};
    if(
        ZSTD_getFrameHeader(&header, packed.data(), packed.size()) != 0u
        || header.frameType != ZSTD_frame || header.frameContentSize != expectedBytes
        || header.checksumFlag != 1u || header.dictID != 0u || header.windowSize > s_MaxPayloadBytes
    )
        return false;
    const usize frameSize = ZSTD_findFrameCompressedSize(packed.data(), packed.size());
    if(ZSTD_isError(frameSize) || frameSize != packed.size())
        return false;
    Core::Assets::AssetBytes candidate(raw.get_allocator().arena());
    candidate.resize(expectedBytes);
    const usize size = ZSTD_decompress(candidate.data(), candidate.size(), packed.data(), packed.size());
    if(ZSTD_isError(size) || size != expectedBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Font atlas source decompression failed or checksum mismatch"));
        return false;
    }
    raw = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

