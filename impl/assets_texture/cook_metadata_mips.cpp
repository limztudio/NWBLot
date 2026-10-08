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


[[nodiscard]] Expected<u64> BuildMipLevels(
    const Path& nwbFilePath,
    const TexturePayloadFormat::Enum payloadFormat,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    Texture::MipLevelVector& outMipLevels
){
    outMipLevels.clear();

    const auto mipCountResult = ComputeCompleteMipCount(dimension, width, height, depth);
    if(!mipCountResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': texture dimensions cannot form a complete mip chain")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    outMipLevels.reserve(*mipCountResult);
    u32 mipWidth = width;
    u32 mipHeight = height;
    u32 mipDepth = depth;
    u64 offsetBytes = 0u;
    for(u32 mipIndex = 0u; mipIndex < *mipCountResult; ++mipIndex){
        const auto planeLayoutResult = ComputeMipPlaneBlockLayout(payloadFormat, mipWidth, mipHeight);
        if(!planeLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip {} block grid exceeds runtime limits")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return MakeUnexpected(Failure{});
        }
        const auto sliceCountResult = ComputeMipSliceCount(dimension, mipDepth);
        if(!sliceCountResult){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip {} has an invalid slice count")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return MakeUnexpected(Failure{});
        }
        if(planeLayoutResult->planeByteCount > Limit<u64>::s_Max / *sliceCountResult){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip {} byte size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return MakeUnexpected(Failure{});
        }
        const u64 sizeBytes = planeLayoutResult->planeByteCount * *sliceCountResult;
        if(sizeBytes > Limit<u64>::s_Max - offsetBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip payload offsets overflow")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
            );
            return MakeUnexpected(Failure{});
        }

        TextureMipLevel mip;
        mip.width = mipWidth;
        mip.height = mipHeight;
        mip.blockCountX = planeLayoutResult->blocksX;
        mip.blockCountY = planeLayoutResult->blocksY;
        mip.offsetBytes = offsetBytes;
        mip.sizeBytes = sizeBytes;
        mip.sliceCount = *sliceCountResult;
        outMipLevels.push_back(mip);

        offsetBytes += sizeBytes;
        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
        if(dimension == TextureDimension::Texture3D)
            mipDepth = mipDepth > 1u ? mipDepth >> 1u : 1u;
    }
    return offsetBytes;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

