// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata_helpers.h"

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <global/filesystem.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureFormat::ComputeMipPlaneBlockLayout;
using TextureFormat::ComputeMipSliceCount;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseMipLevels(
    const Path& nwbFilePath,
    const Value& asset,
    const TexturePayloadFormat::Enum payloadFormat,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    const u32 expectedMipCount,
    Texture::MipLevelVector& outMipLevels
){
    outMipLevels.clear();

    const Value* const mipsField = FindField(asset, s_MipsField);
    if(!mipsField || !mipsField->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_MipsField)
        );
        return false;
    }

    const Value::ListType& mips = mipsField->asList();
    if(mips.size() != expectedMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must contain a complete mip chain")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_MipsField)
        );
        return false;
    }

    outMipLevels.reserve(mips.size());
    u32 expectedWidth = width;
    u32 expectedHeight = height;
    u32 expectedDepth = depth;
    u64 expectedOffsetBytes = 0u;
    for(usize mipIndex = 0u; mipIndex < mips.size(); ++mipIndex){
        const Value& mipValue = mips[mipIndex];
        if(!mipValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] must be a map")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        if(!::NWB::Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath,
            mipValue,
            "Texture mip",
            { s_LevelField, s_WidthField, s_HeightField, s_SlicesField, s_BlocksXField, s_BlocksYField, s_OffsetBytesField, s_SizeBytesField }
        ))
            return false;

        u32 level = 0u;
        u32 mipWidth = 0u;
        u32 mipHeight = 0u;
        u32 sliceCount = 0u;
        u32 blockCountX = 0u;
        u32 blockCountY = 0u;
        u64 offsetBytes = 0u;
        u64 sizeBytes = 0u;
        if(
            !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_LevelField, 0u, Limit<u32>::s_Max, level)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_WidthField, 1u, Limit<u32>::s_Max, mipWidth)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_HeightField, 1u, Limit<u32>::s_Max, mipHeight)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_SlicesField, 1u, Limit<u32>::s_Max, sliceCount)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_BlocksXField, 1u, Limit<u32>::s_Max, blockCountX)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_BlocksYField, 1u, Limit<u32>::s_Max, blockCountY)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_OffsetBytesField, static_cast<u64>(0u), Limit<u64>::s_Max, offsetBytes)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_SizeBytesField, static_cast<u64>(1u), Limit<u64>::s_Max, sizeBytes)
        )
            return false;
        u32 expectedBlockCountX = 0u;
        u32 expectedBlockCountY = 0u;
        u64 expectedSliceSizeBytes = 0u;
        if(!ComputeMipPlaneBlockLayout(
            payloadFormat,
            expectedWidth,
            expectedHeight,
            expectedBlockCountX,
            expectedBlockCountY,
            expectedSliceSizeBytes
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] block grid exceeds runtime limits")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        u32 expectedSliceCount = 0u;
        if(!ComputeMipSliceCount(dimension, expectedDepth, expectedSliceCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] has an invalid slice count")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        if(expectedSliceSizeBytes > Limit<u64>::s_Max / expectedSliceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] byte size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        const u64 expectedSizeBytes = expectedSliceSizeBytes * expectedSliceCount;

        if(
            level != mipIndex
            || mipWidth != expectedWidth
            || mipHeight != expectedHeight
            || sliceCount != expectedSliceCount
            || blockCountX != expectedBlockCountX
            || blockCountY != expectedBlockCountY
            || offsetBytes != expectedOffsetBytes
            || sizeBytes != expectedSizeBytes
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] does not match the required contiguous texture mip chain")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        if(expectedSizeBytes > Limit<u64>::s_Max - expectedOffsetBytes){
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

        expectedOffsetBytes += expectedSizeBytes;
        expectedWidth = expectedWidth > 1u ? expectedWidth >> 1u : 1u;
        expectedHeight = expectedHeight > 1u ? expectedHeight >> 1u : 1u;
        if(dimension == TextureDimension::Texture3D)
            expectedDepth = expectedDepth > 1u ? expectedDepth >> 1u : 1u;
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

