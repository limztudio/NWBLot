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
    if(!::NWB::Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return false;
    if(!ReadTextureDimension(nwbFilePath, asset, outEntry.dimension))
        return false;

    AStringView format;
    if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_FormatField, true, format))
        return false;
    if(format == s_UastcLdr4x4Format)
        outEntry.payloadFormat = TexturePayloadFormat::UastcLdr4x4;
    else if(format == s_UastcHdr4x4Format)
        outEntry.payloadFormat = TexturePayloadFormat::UastcHdr4x4;
    else{
        NWB_LOGGER_ERROR(GLB_TEXT("{} '{}': field '{}' must be '{}' or '{}'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_FormatField)
            , StringConvert(s_UastcLdr4x4Format)
            , StringConvert(s_UastcHdr4x4Format)
        );
        return false;
    }

    const bool isHdr = IsHdrTexturePayloadFormat(outEntry.payloadFormat);
    if(isHdr){
        AStringView alphaModeText;
        if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_AlphaModeField, true, alphaModeText))
            return false;
        if(alphaModeText == s_AlphaOpaqueMode)
            outEntry.alphaMode = TextureAlphaMode::Opaque;
        else if(alphaModeText == s_AlphaConstantUnorm8Mode)
            outEntry.alphaMode = TextureAlphaMode::ConstantUnorm8;
        else if(alphaModeText == s_AlphaUastcLdr4x4Mode)
            outEntry.alphaMode = TextureAlphaMode::SeparateUastcLdr4x4;
        else{
            NWB_LOGGER_ERROR(GLB_TEXT("{} '{}': field '{}' has an unsupported HDR alpha mode")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_AlphaModeField)
            );
            return false;
        }
    }
    if(!::NWB::Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        [isHdr, dimension = outEntry.dimension, alphaMode = outEntry.alphaMode](const AStringView fieldName){
            return
                fieldName == s_FormatField
                || fieldName == s_DimensionField
                || fieldName == s_WidthField
                || fieldName == s_HeightField
                || fieldName == s_DataField
                || (dimension == TextureDimension::Texture3D && fieldName == s_DepthField)
                || (!isHdr && (fieldName == s_ColorSpaceField || fieldName == s_HasAlphaField))
                || (isHdr && fieldName == s_AlphaModeField)
                || (isHdr && alphaMode == TextureAlphaMode::ConstantUnorm8 && fieldName == s_AlphaConstantUnorm8Field)
            ;
        }
    ))
        return false;

    if(
        !ReadRequiredUnsignedField(nwbFilePath, asset, s_WidthField, 1u, Limit<u32>::s_Max, outEntry.width)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_HeightField, 1u, Limit<u32>::s_Max, outEntry.height)
    )
        return false;
    if(outEntry.dimension == TextureDimension::Texture3D){
        if(!ReadRequiredUnsignedField(nwbFilePath, asset, s_DepthField, 1u, Limit<u32>::s_Max, outEntry.depth))
            return false;
    }
    if(outEntry.dimension == TextureDimension::TextureCube && outEntry.width != outEntry.height){
        NWB_LOGGER_ERROR(GLB_TEXT("{} '{}': cubemap faces must be square")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    if(isHdr){
        outEntry.colorSpace = TextureColorSpace::Linear;
        outEntry.hasAlpha = outEntry.alphaMode != TextureAlphaMode::Opaque;
        if(outEntry.alphaMode == TextureAlphaMode::ConstantUnorm8){
            u32 alphaConstant = 0u;
            if(!ReadRequiredUnsignedField(nwbFilePath, asset, s_AlphaConstantUnorm8Field, 0u, s_MaxConstantAlphaUnorm8, alphaConstant))
                return false;
            outEntry.alphaConstantUnorm8 = static_cast<u8>(alphaConstant);
        }
    }
    else{
        AStringView colorSpace;
        if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_ColorSpaceField, true, colorSpace))
            return false;
        if(colorSpace == s_LinearColorSpace)
            outEntry.colorSpace = TextureColorSpace::Linear;
        else if(colorSpace == s_SrgbColorSpace)
            outEntry.colorSpace = TextureColorSpace::Srgb;
        else{
            NWB_LOGGER_ERROR(GLB_TEXT("{} '{}': field '{}' must be '{}' or '{}'")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_ColorSpaceField)
                , StringConvert(s_LinearColorSpace)
                , StringConvert(s_SrgbColorSpace)
            );
            return false;
        }
        u32 hasAlpha = 0u;
        if(!ReadRequiredUnsignedField(nwbFilePath, asset, s_HasAlphaField, 0u, 1u, hasAlpha))
            return false;
        outEntry.hasAlpha = hasAlpha != 0u;
        outEntry.alphaMode = outEntry.hasAlpha ? TextureAlphaMode::EmbeddedLdr : TextureAlphaMode::Opaque;
    }

    if(!::NWB::Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, outEntry.virtualPath, scratchArena))
        return false;

    u64 expectedPayloadBytes = 0u;
    if(!BuildMipLevels(
        nwbFilePath,
        outEntry.payloadFormat,
        outEntry.dimension,
        outEntry.width,
        outEntry.height,
        outEntry.depth,
        outEntry.mipLevels,
        expectedPayloadBytes
    ))
        return false;
    u64 expectedTotalPayloadBytes = expectedPayloadBytes;
    if(outEntry.alphaMode == TextureAlphaMode::SeparateUastcLdr4x4){
        if(expectedPayloadBytes > Limit<u64>::s_Max - expectedTotalPayloadBytes){
            NWB_LOGGER_ERROR(GLB_TEXT("{} '{}': separate HDR alpha payload size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        expectedTotalPayloadBytes += expectedPayloadBytes;
    }

    AStringView dataFileName;
    if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_DataField, true, dataFileName))
        return false;
    if(!ValidateTextureDataFileName(nwbFilePath, dataFileName, scratchArena))
        return false;

    Path dataPath(nwbFilePath.parentPath());
    dataPath /= dataFileName;
    ErrorCode errorCode;
    if(!ReadBinaryFile(dataPath, outEntry.payloadBytes, errorCode)){
        NWB_LOGGER_ERROR(GLB_TEXT("{} '{}': failed to read texture sidecar '{}': {}")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , PathToString<tchar>(dataPath)
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(expectedTotalPayloadBytes != static_cast<u64>(outEntry.payloadBytes.size())){
        NWB_LOGGER_ERROR(GLB_TEXT("{} '{}': texture sidecar size does not match the derived mip and alpha layout")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

