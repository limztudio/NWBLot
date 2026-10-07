// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata_helpers.h"

#include <core/common/log.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureFormat::ComputeMipPlaneBlockLayout;
using TextureFormat::ComputeMipSliceCount;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildMipLevels(
    const Path& nwbFilePath,
    const TexturePayloadFormat::Enum payloadFormat,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    Texture::MipLevelVector& outMipLevels,
    u64& outPayloadByteCount
){
    outMipLevels.clear();
    outPayloadByteCount = 0u;

    u32 mipCount = 0u;
    if(!ComputeCompleteMipCount(dimension, width, height, depth, mipCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': texture dimensions cannot form a complete mip chain")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    outMipLevels.reserve(mipCount);
    u32 mipWidth = width;
    u32 mipHeight = height;
    u32 mipDepth = depth;
    u64 offsetBytes = 0u;
    for(u32 mipIndex = 0u; mipIndex < mipCount; ++mipIndex){
        u32 blockCountX = 0u;
        u32 blockCountY = 0u;
        u64 sliceSizeBytes = 0u;
        if(!ComputeMipPlaneBlockLayout(payloadFormat, mipWidth, mipHeight, blockCountX, blockCountY, sliceSizeBytes)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip {} block grid exceeds runtime limits")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        u32 sliceCount = 0u;
        if(!ComputeMipSliceCount(dimension, mipDepth, sliceCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip {} has an invalid slice count")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        if(sliceSizeBytes > Limit<u64>::s_Max / sliceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip {} byte size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        const u64 sizeBytes = sliceSizeBytes * sliceCount;
        if(sizeBytes > Limit<u64>::s_Max - offsetBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip payload offsets overflow")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }

        TextureMipLevel mip;
        mip.width = mipWidth;
        mip.height = mipHeight;
        mip.blockCountX = blockCountX;
        mip.blockCountY = blockCountY;
        mip.offsetBytes = offsetBytes;
        mip.sizeBytes = sizeBytes;
        mip.sliceCount = sliceCount;
        outMipLevels.push_back(mip);

        offsetBytes += sizeBytes;
        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
        if(dimension == TextureDimension::Texture3D)
            mipDepth = mipDepth > 1u ? mipDepth >> 1u : 1u;
    }
    outPayloadByteCount = offsetBytes;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

