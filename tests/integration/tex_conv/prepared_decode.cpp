// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_texture/cook.h>
#include <impl/assets_texture/texture_mip_decoder.h>

#include <core/common/application_entry.h>

#include <logger/client/module.h>

#include <global/bit.h>
#include <global/filesystem.h>
#include <global/simd/convert.h>

#include <basisu_transcoder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tex_conv_prepared_decode{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_DecodeArena("tests/integration/tex_conv/prepared_decode");
inline constexpr usize s_MaxFixtureBytes = 64u * 1024u * 1024u;
inline constexpr u32 s_ChannelCount = 4u;
inline constexpr TStringView s_LoggerAppName = NWB_TEXT("tex_conv_prepared_decode");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool Decode(const AStringView source, const AStringView destination, const AStringView mipText){
    u32 mipIndex = 0u;
    if(mipText.empty())
        return false;
    for(const char digit : mipText){
        if(digit < '0' || digit > '9' || mipIndex > (Limit<u32>::s_Max - static_cast<u32>(digit - '0')) / 10u)
            return false;
        mipIndex = mipIndex * 10u + static_cast<u32>(digit - '0');
    }

    NWB::Core::Assets::AssetArena assetArena(s_DecodeArena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_DecodeArena);
    const NWB::Path sourcePath(assetArena, source);
    const ::Path<NWB::Core::Alloc::ScratchArena> destinationPath(scratchArena, destination);
    AString<NWB::Core::Alloc::ScratchArena> metadata(scratchArena);
    if(!ReadTextFile(sourcePath, metadata))
        return false;
    NWB::Core::Metascript::Document document(assetArena);
    if(!document.parse(metadata))
        return false;
    const auto entry = NWB::Impl::ParseTextureCookMetadata(sourcePath.parentPath(), "tests", sourcePath, document, assetArena, scratchArena);
    if(!entry)
        return false;
    const auto builtTexture = NWB::Impl::BuildTextureAsset(*entry, assetArena);
    if(!builtTexture || mipIndex >= builtTexture->mipLevels().size())
        return false;
    const NWB::Impl::Texture& texture = *builtTexture;
    const NWB::Impl::TextureMipLevel& mip = texture.mipLevels()[mipIndex];
    const u64 planeTexels = static_cast<u64>(mip.width) * mip.height;
    const u64 bytesPerPlaneTexel = static_cast<u64>(mip.sliceCount) * s_ChannelCount * sizeof(f32);
    if(bytesPerPlaneTexel == 0u || planeTexels > s_MaxFixtureBytes / bytesPerPlaneTexel)
        return false;
    const usize componentCount = static_cast<usize>(planeTexels * mip.sliceCount * s_ChannelCount);
    const bool hdr = IsHdrTexturePayloadFormat(texture.payloadFormat());
    basist::basisu_transcoder_init();
    NWB::Impl::TextureMipDecoder decoder;
    const auto decoded = decoder.decode(texture, mip, mipIndex, hdr ? NWB::Core::Format::RGBA16_FLOAT : NWB::Core::Format::RGBA8_UNORM, scratchArena);
    if(!decoded || decoded->bytes.size() != componentCount * (hdr ? sizeof(Half) : sizeof(u8)))
        return false;

    // Export the selected mip extent followed by interleaved linear RGBA binary32 values in little-endian order.
    Vector<u8, NWB::Core::Alloc::ScratchArena> output(scratchArena);
    output.resize(s_ChannelCount * sizeof(u32) + componentCount * sizeof(f32));
    usize offset = 0u;
    const u32 dimensions[] = { mip.width, mip.height, mip.sliceCount, s_ChannelCount };
    for(const u32 value : dimensions){
        for(u32 byte = 0u; byte < sizeof(u32); ++byte)
            output[offset++] = static_cast<u8>(value >> (byte * s_BitsPerByte));
    }
    for(usize index = 0u; index < componentCount; ++index){
        f32 value;
        if(hdr){
            Half half;
            NWB_MEMCPY(&half, sizeof(half), decoded->bytes.data() + index * sizeof(Half), sizeof(half));
            value = ConvertHalfToFloat(half);
        }
        else
            value = static_cast<f32>(decoded->bytes[index]) / 255.0f;
        const u32 bits = BitCast<u32>(value);
        for(u32 byte = 0u; byte < sizeof(u32); ++byte)
            output[offset++] = static_cast<u8>(bits >> (byte * s_BitsPerByte));
    }
    return WriteBinaryFile(destinationPath, output);
}

int Run(const int argc, char** argv){
    NWB::Log::ClientStandalone logger;
    if(!logger.init(s_LoggerAppName)){
        NWB_TCERR << NWB_TEXT("[tex_conv_prepared_decode] logger.init() failed\n");
        return 1;
    }
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Log::BreakPolicy::BreakOnFatal);
    if(argc != 4 || !Decode(argv[1], argv[2], argv[3])){
        NWB_LOGGER_ERROR(NWB_TEXT("tex_conv_prepared_decode: expected valid texture metadata with its paired raw sidecar, output filename, and mip index."));
        return 1;
    }
    return 0;
}

int EntryPoint(const isize argc, char** argv, void*){
    return Run(static_cast<int>(argc), argv);
}

#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, Run);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_APPLICATION_ENTRY_POINT(__hidden_tex_conv_prepared_decode::EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

