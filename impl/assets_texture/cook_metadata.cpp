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


bool ParseTextureCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    TextureCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace TextureCookDetail;

    outEntry = TextureCookEntry(outEntry.mipLevels.get_allocator().arena());

    const Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset is not a map")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(
        !::NWB::Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath,
            asset,
            s_DiagnosticPrefix,
            {
                s_VersionField,
                s_FormatField,
                s_UastcSpecificationRevisionField,
                s_UastcHdrSpecificationRevisionField,
                s_ColorSpaceField,
                s_DimensionField,
                s_WidthField,
                s_HeightField,
                s_DepthField,
                s_BlockWidthField,
                s_BlockHeightField,
                s_BytesPerBlockField,
                s_PayloadLayoutField,
                s_MipAddressModeField,
                s_HasAlphaField,
                s_AlphaModeField,
                s_AlphaConstantUnorm8Field,
                s_AlphaPayloadOffsetBytesField,
                s_AlphaPayloadByteCountField,
                s_AlphaUastcSpecificationRevisionField,
                s_MipCountField,
                s_DataField,
                s_MipsField,
            }
        )
        || !ReadTextureDimension(nwbFilePath, asset, outEntry.dimension)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_DepthField, 1u, Limit<u32>::s_Max, outEntry.depth)
    )
        return false;

    if(!::NWB::Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, outEntry.virtualPath, scratchArena))
        return false;

    u32 metadataVersion = 0u;
    u32 hasAlpha = 0u;
    u32 mipCount = 0u;
    u64 alphaPayloadOffsetBytes = 0u;
    u64 alphaPayloadByteCount = 0u;
    AStringView format;
    if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_FormatField, true, format))
        return false;

    u32 expectedBlockWidth = 0u;
    u32 expectedBlockHeight = 0u;
    u32 expectedBytesPerBlock = 0u;
    if(format == s_UastcLdr4x4Format){
        outEntry.payloadFormat = TexturePayloadFormat::UastcLdr4x4;
        if(
            !ReadRequiredUnsignedField(
                nwbFilePath,
                asset,
                s_VersionField,
                s_UastcLdrTextureMetadataVersion,
                s_UastcLdrTextureMetadataVersion,
                metadataVersion
            )
            || !ReadExactStringField(nwbFilePath, asset, s_UastcSpecificationRevisionField, s_UastcSpecificationRevision)
        )
            return false;
        if(
            FindField(asset, s_UastcHdrSpecificationRevisionField)
            || FindField(asset, s_AlphaModeField)
            || FindField(asset, s_AlphaConstantUnorm8Field)
            || FindField(asset, s_AlphaPayloadOffsetBytesField)
            || FindField(asset, s_AlphaPayloadByteCountField)
            || FindField(asset, s_AlphaUastcSpecificationRevisionField)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': HDR alpha fields are not valid for '{}' textures")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_UastcLdr4x4Format)
            );
            return false;
        }
    }
    else if(format == s_UastcHdr4x4Format){
        outEntry.payloadFormat = TexturePayloadFormat::UastcHdr4x4;
        if(
            !ReadRequiredUnsignedField(
                nwbFilePath,
                asset,
                s_VersionField,
                s_UastcHdrTextureMetadataVersion,
                s_UastcHdrTextureMetadataVersion,
                metadataVersion
            )
            || !ReadExactStringField(nwbFilePath, asset, s_UastcHdrSpecificationRevisionField, s_UastcSpecificationRevision)
        )
            return false;
        if(FindField(asset, s_UastcSpecificationRevisionField)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is only valid for '{}' textures")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_UastcSpecificationRevisionField)
                , StringConvert(s_UastcLdr4x4Format)
            );
            return false;
        }
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be '{}' or '{}'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_FormatField)
            , StringConvert(s_UastcLdr4x4Format)
            , StringConvert(s_UastcHdr4x4Format)
        );
        return false;
    }
    if(!GetTexturePayloadBlockLayout(
        outEntry.payloadFormat,
        expectedBlockWidth,
        expectedBlockHeight,
        expectedBytesPerBlock
    ))
        return false;

    if(
        !ReadRequiredUnsignedField(nwbFilePath, asset, s_WidthField, 1u, Limit<u32>::s_Max, outEntry.width)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_HeightField, 1u, Limit<u32>::s_Max, outEntry.height)
        || !ReadExactUnsignedField(nwbFilePath, asset, s_BlockWidthField, expectedBlockWidth)
        || !ReadExactUnsignedField(nwbFilePath, asset, s_BlockHeightField, expectedBlockHeight)
        || !ReadExactUnsignedField(nwbFilePath, asset, s_BytesPerBlockField, expectedBytesPerBlock)
        || !ReadExactStringField(
            nwbFilePath,
            asset,
            s_PayloadLayoutField,
            s_MipMajorSliceMajorBlocksPayloadLayout
        )
        || !ReadExactStringField(nwbFilePath, asset, s_MipAddressModeField, s_ClampMipAddressMode)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_HasAlphaField, 0u, 1u, hasAlpha)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_MipCountField, 1u, Limit<u32>::s_Max, mipCount)
    )
        return false;

    AStringView colorSpace;
    if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_ColorSpaceField, true, colorSpace))
        return false;
    if(colorSpace == s_LinearColorSpace)
        outEntry.colorSpace = TextureColorSpace::Linear;
    else if(colorSpace == s_SrgbColorSpace)
        outEntry.colorSpace = TextureColorSpace::Srgb;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be '{}' or '{}'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_ColorSpaceField)
            , StringConvert(s_LinearColorSpace)
            , StringConvert(s_SrgbColorSpace)
        );
        return false;
    }
    if(IsHdrTexturePayloadFormat(outEntry.payloadFormat) && outEntry.colorSpace != TextureColorSpace::Linear){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' textures must use '{}' color space")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_UastcHdr4x4Format)
            , StringConvert(s_LinearColorSpace)
        );
        return false;
    }

    if(outEntry.dimension != TextureDimension::Texture3D && outEntry.depth != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': non-volume textures must have depth 1")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(outEntry.dimension == TextureDimension::TextureCube && outEntry.width != outEntry.height){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': cubemap faces must be square")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    u32 expectedMipCount = 0u;
    if(!ComputeCompleteMipCount(outEntry.dimension, outEntry.width, outEntry.height, outEntry.depth, expectedMipCount) || mipCount != expectedMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must describe a complete mip chain")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_MipCountField)
        );
        return false;
    }
    if(!ParseMipLevels(
        nwbFilePath,
        asset,
        outEntry.payloadFormat,
        outEntry.dimension,
        outEntry.width,
        outEntry.height,
        outEntry.depth,
        expectedMipCount,
        outEntry.mipLevels
    ))
        return false;

    AStringView dataFileName;
    if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_DataField, true, dataFileName))
        return false;
    if(!ValidateTextureDataFileName(nwbFilePath, dataFileName, scratchArena))
        return false;

    Path dataPath(nwbFilePath.parent_path());
    dataPath /= dataFileName;
    ErrorCode errorCode;
    if(!ReadBinaryFile(dataPath, outEntry.payloadBytes, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to read texture sidecar '{}': {}")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , PathToString<tchar>(dataPath)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    u64 expectedPayloadBytes = 0u;
    for(const TextureMipLevel& mip : outEntry.mipLevels){
        if(mip.sizeBytes > Limit<u64>::s_Max - expectedPayloadBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': texture payload size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        expectedPayloadBytes += mip.sizeBytes;
    }
    u64 expectedTotalPayloadBytes = expectedPayloadBytes;
    if(outEntry.payloadFormat == TexturePayloadFormat::UastcLdr4x4){
        outEntry.alphaMode = hasAlpha != 0u ? TextureAlphaMode::EmbeddedLdr : TextureAlphaMode::Opaque;
        outEntry.alphaConstantUnorm8 = s_OpaqueAlphaUnorm8;
    }
    else{
        AStringView alphaModeText;
        if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_AlphaModeField, true, alphaModeText))
            return false;
        if(alphaModeText == s_AlphaOpaqueMode){
            outEntry.alphaMode = TextureAlphaMode::Opaque;
            outEntry.alphaConstantUnorm8 = s_OpaqueAlphaUnorm8;
            if(
                hasAlpha != 0u
                || FindField(asset, s_AlphaConstantUnorm8Field)
                || FindField(asset, s_AlphaPayloadOffsetBytesField)
                || FindField(asset, s_AlphaPayloadByteCountField)
                || FindField(asset, s_AlphaUastcSpecificationRevisionField)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': opaque HDR alpha metadata must not carry an alpha payload")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
        }
        else if(alphaModeText == s_AlphaConstantUnorm8Mode){
            u32 alphaConstant = 0u;
            if(
                hasAlpha != 1u
                || !ReadRequiredUnsignedField(
                    nwbFilePath,
                    asset,
                    s_AlphaConstantUnorm8Field,
                    0u,
                    s_MaxConstantAlphaUnorm8,
                    alphaConstant
                )
                || FindField(asset, s_AlphaPayloadOffsetBytesField)
                || FindField(asset, s_AlphaPayloadByteCountField)
                || FindField(asset, s_AlphaUastcSpecificationRevisionField)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': constant HDR alpha metadata is invalid")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            outEntry.alphaMode = TextureAlphaMode::ConstantUnorm8;
            outEntry.alphaConstantUnorm8 = static_cast<u8>(alphaConstant);
        }
        else if(alphaModeText == s_AlphaUastcLdr4x4Mode){
            if(
                hasAlpha != 1u
                || !ReadRequiredUnsignedField(
                    nwbFilePath,
                    asset,
                    s_AlphaPayloadOffsetBytesField,
                    static_cast<u64>(0u),
                    Limit<u64>::s_Max,
                    alphaPayloadOffsetBytes
                )
                || !ReadRequiredUnsignedField(
                    nwbFilePath,
                    asset,
                    s_AlphaPayloadByteCountField,
                    static_cast<u64>(0u),
                    Limit<u64>::s_Max,
                    alphaPayloadByteCount
                )
                || !ReadExactStringField(
                    nwbFilePath,
                    asset,
                    s_AlphaUastcSpecificationRevisionField,
                    s_UastcSpecificationRevision
                )
                || FindField(asset, s_AlphaConstantUnorm8Field)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': separate HDR alpha metadata is invalid")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            if(
                alphaPayloadOffsetBytes != expectedPayloadBytes
                || alphaPayloadByteCount != expectedPayloadBytes
                || expectedPayloadBytes > Limit<u64>::s_Max - expectedTotalPayloadBytes
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': separate HDR alpha payload must mirror the RGB UASTC mip layout")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            expectedTotalPayloadBytes += expectedPayloadBytes;
            outEntry.alphaMode = TextureAlphaMode::SeparateUastcLdr4x4;
            outEntry.alphaConstantUnorm8 = s_OpaqueAlphaUnorm8;
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' has an unsupported HDR alpha mode")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_AlphaModeField)
            );
            return false;
        }
    }

    if(expectedTotalPayloadBytes != static_cast<u64>(outEntry.payloadBytes.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': texture sidecar size does not match the mip and alpha metadata")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outEntry.hasAlpha = hasAlpha != 0u;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

