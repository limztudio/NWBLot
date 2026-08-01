// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>

#include <basisu_comp.h>
#include <basisu_enc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_encode{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BasisLibrary final{
public:
    BasisLibrary() = default;
    ~BasisLibrary(){
        if(m_initialized)
            basisu::basisu_encoder_deinit();
    }
    BasisLibrary(const BasisLibrary&) = delete;
    BasisLibrary& operator=(const BasisLibrary&) = delete;


public:
    [[nodiscard]] bool initialize(){
        m_initialized = basisu::basisu_encoder_init(false);
        if(m_initialized)
            return true;

        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to initialize the Basis Universal encoder."));
        return false;
    }


private:
    bool m_initialized = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EncodeTexture(const Path& inputPath, const bool srgb, TexturePayload& outPayload){
    __hidden_encode::BasisLibrary library;
    if(!library.initialize())
        return false;

    basisu::job_pool jobPool(1u);
    basisu::basis_compressor_params parameters;
    parameters.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
    parameters.set_srgb_options(srgb);
    parameters.m_read_source_images = true;
    const AString inputPathText = PathToUtf8(inputPath);
    parameters.m_source_filenames.push_back(AInteropString(inputPathText.data(), inputPathText.size()));
    parameters.m_mip_gen = true;
    parameters.m_mip_smallest_dimension = 1u;
    parameters.m_mip_wrapping = false;
    parameters.m_status_output = false;
    parameters.m_compute_stats = false;
    parameters.m_print_stats = false;
    parameters.m_write_output_basis_or_ktx2_files = false;
    parameters.m_create_ktx2_file = false;
    parameters.m_pJob_pool = &jobPool;

    basisu::basis_compressor compressor;
    if(!compressor.init(parameters)){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal failed to initialize '{}'."), PathToString<tchar>(inputPath));
        return false;
    }

    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        NWB_LOGGER_WARNING(
            NWB_TEXT("tex_conv: UASTC encoding failed for '{}' (Basis Universal error {}).")
            , PathToString<tchar>(inputPath)
            , static_cast<u32>(encodeResult)
        );
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(backendOutput.m_tex_format != basist::basis_tex_format::cUASTC_LDR_4x4){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal did not produce UASTC LDR 4x4 blocks."));
        return false;
    }
    if(backendOutput.m_slice_desc.empty() || backendOutput.m_slice_desc.size() != backendOutput.m_slice_image_data.size()){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal returned an incomplete UASTC mip chain."));
        return false;
    }

    const usize mipCount = backendOutput.m_slice_desc.size();
    if(mipCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal returned too many UASTC mip levels."));
        return false;
    }

    Vector<usize> sliceForMip(mipCount, Limit<usize>::s_Max);
    for(usize sliceIndex = 0u; sliceIndex < mipCount; ++sliceIndex){
        const basisu::basisu_backend_slice_desc& descriptor = backendOutput.m_slice_desc[sliceIndex];
        if(
            descriptor.m_source_file_index != 0u
            || static_cast<usize>(descriptor.m_mip_index) >= mipCount
            || sliceForMip[descriptor.m_mip_index] != Limit<usize>::s_Max
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal returned an unexpected texture-slice layout."));
            return false;
        }
        sliceForMip[descriptor.m_mip_index] = sliceIndex;
    }

    usize totalBytes = 0u;
    for(const basisu::uint8_vec& encodedBlocks : backendOutput.m_slice_image_data){
        const usize encodedSize = encodedBlocks.size_in_bytes();
        if(encodedSize > Limit<usize>::s_Max - totalBytes){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC payload is too large to store."));
            return false;
        }
        totalBytes += encodedSize;
    }

    outPayload.width = 0u;
    outPayload.height = 0u;
    outPayload.hasAlpha = false;
    outPayload.mips.clear();
    outPayload.bytes.clear();
    outPayload.mips.reserve(mipCount);
    outPayload.bytes.reserve(totalBytes);

    u64 offsetBytes = 0u;
    for(usize mipIndex = 0u; mipIndex < mipCount; ++mipIndex){
        const usize sliceIndex = sliceForMip[mipIndex];
        if(sliceIndex == Limit<usize>::s_Max){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal omitted a mip level."));
            return false;
        }

        const basisu::basisu_backend_slice_desc& descriptor = backendOutput.m_slice_desc[sliceIndex];
        const basisu::uint8_vec& encodedBlocks = backendOutput.m_slice_image_data[sliceIndex];
        if(descriptor.m_orig_width == 0u || descriptor.m_orig_height == 0u){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal produced a mip level with an invalid resolution."));
            return false;
        }

        const u64 blocksX64 = (static_cast<u64>(descriptor.m_orig_width) + s_UastcBlockWidth - 1u) / s_UastcBlockWidth;
        const u64 blocksY64 = (static_cast<u64>(descriptor.m_orig_height) + s_UastcBlockHeight - 1u) / s_UastcBlockHeight;
        if(!FitsU32(blocksX64) || !FitsU32(blocksY64)){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal produced a mip resolution outside the UASTC range."));
            return false;
        }

        const u32 blocksX = static_cast<u32>(blocksX64);
        const u32 blocksY = static_cast<u32>(blocksY64);
        const u64 expectedSize =
            static_cast<u64>(blocksX)
            * static_cast<u64>(blocksY)
            * static_cast<u64>(s_UastcBytesPerBlock)
        ;
        const usize encodedSize = encodedBlocks.size_in_bytes();
        if(
            descriptor.m_num_blocks_x != blocksX
            || descriptor.m_num_blocks_y != blocksY
            || expectedSize != static_cast<u64>(encodedSize)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: Basis Universal produced an invalid UASTC block layout."));
            return false;
        }
        if(offsetBytes > Limit<u64>::s_Max - expectedSize){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: UASTC payload offset overflowed."));
            return false;
        }

        outPayload.mips.emplace_back();
        MipLevel& mip = outPayload.mips.back();
        mip.level = static_cast<u32>(mipIndex);
        mip.width = descriptor.m_orig_width;
        mip.height = descriptor.m_orig_height;
        mip.blocksX = blocksX;
        mip.blocksY = blocksY;
        mip.offsetBytes = offsetBytes;
        mip.sizeBytes = expectedSize;

        const u8* const source = encodedBlocks.get_ptr();
        outPayload.bytes.insert(outPayload.bytes.end(), source, source + encodedSize);
        offsetBytes += expectedSize;
    }

    outPayload.width = outPayload.mips.front().width;
    outPayload.height = outPayload.mips.front().height;
    outPayload.hasAlpha = compressor.get_any_source_image_has_alpha();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

