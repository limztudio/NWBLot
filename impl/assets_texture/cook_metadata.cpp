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


Expected<TextureCookEntry> ParseTextureCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace TextureCookDetail;

    TextureCookEntry entry(arena);

    const Value& asset = doc.asset();
    if(!::NWB::Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});
    auto dimensionResult = ReadTextureDimension(nwbFilePath, asset);
    if(!dimensionResult)
        return MakeUnexpected(Failure{});
    entry.dimension = *dimensionResult;

    AStringView format;
    auto formatResult = ::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_FormatField, true);
    if(!formatResult)
        return MakeUnexpected(Failure{});
    format = formatResult->text;
    if(format == s_UastcLdr4x4Format)
        entry.payloadFormat = TexturePayloadFormat::UastcLdr4x4;
    else if(format == s_UastcHdr4x4Format)
        entry.payloadFormat = TexturePayloadFormat::UastcHdr4x4;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be '{}' or '{}'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_FormatField)
            , StringConvert(s_UastcLdr4x4Format)
            , StringConvert(s_UastcHdr4x4Format)
        );
        return MakeUnexpected(Failure{});
    }

    const bool isHdr = IsHdrTexturePayloadFormat(entry.payloadFormat);
    if(isHdr){
        AStringView alphaModeText;
        auto alphaModeTextResult = ::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_AlphaModeField, true);
        if(!alphaModeTextResult)
            return MakeUnexpected(Failure{});
        alphaModeText = alphaModeTextResult->text;
        if(alphaModeText == s_AlphaOpaqueMode)
            entry.alphaMode = TextureAlphaMode::Opaque;
        else if(alphaModeText == s_AlphaConstantUnorm8Mode)
            entry.alphaMode = TextureAlphaMode::ConstantUnorm8;
        else if(alphaModeText == s_AlphaUastcLdr4x4Mode)
            entry.alphaMode = TextureAlphaMode::SeparateUastcLdr4x4;
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' has an unsupported HDR alpha mode")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_AlphaModeField)
            );
            return MakeUnexpected(Failure{});
        }
    }
    if(!::NWB::Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        [isHdr, dimension = entry.dimension, alphaMode = entry.alphaMode](const AStringView fieldName){
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
        return MakeUnexpected(Failure{});

    auto widthResult = ReadRequiredUnsignedField(nwbFilePath, asset, s_WidthField, 1u, Limit<u32>::s_Max);
    if(!widthResult)
        return MakeUnexpected(Failure{});
    entry.width = *widthResult;
    auto heightResult = ReadRequiredUnsignedField(nwbFilePath, asset, s_HeightField, 1u, Limit<u32>::s_Max);
    if(!heightResult)
        return MakeUnexpected(Failure{});
    entry.height = *heightResult;
    if(entry.dimension == TextureDimension::Texture3D){
        auto depthResult = ReadRequiredUnsignedField(nwbFilePath, asset, s_DepthField, 1u, Limit<u32>::s_Max);
        if(!depthResult)
            return MakeUnexpected(Failure{});
        entry.depth = *depthResult;
    }
    if(entry.dimension == TextureDimension::TextureCube && entry.width != entry.height){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': cubemap faces must be square")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }

    if(isHdr){
        entry.colorSpace = TextureColorSpace::Linear;
        entry.hasAlpha = entry.alphaMode != TextureAlphaMode::Opaque;
        if(entry.alphaMode == TextureAlphaMode::ConstantUnorm8){
            u32 alphaConstant = 0u;
            auto alphaConstantResult = ReadRequiredUnsignedField(nwbFilePath, asset, s_AlphaConstantUnorm8Field, 0u, s_MaxConstantAlphaUnorm8);
            if(!alphaConstantResult)
                return MakeUnexpected(Failure{});
            alphaConstant = *alphaConstantResult;
            entry.alphaConstantUnorm8 = static_cast<u8>(alphaConstant);
        }
    }
    else{
        AStringView colorSpace;
        auto colorSpaceResult = ::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_ColorSpaceField, true);
        if(!colorSpaceResult)
            return MakeUnexpected(Failure{});
        colorSpace = colorSpaceResult->text;
        if(colorSpace == s_LinearColorSpace)
            entry.colorSpace = TextureColorSpace::Linear;
        else if(colorSpace == s_SrgbColorSpace)
            entry.colorSpace = TextureColorSpace::Srgb;
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be '{}' or '{}'")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_ColorSpaceField)
                , StringConvert(s_LinearColorSpace)
                , StringConvert(s_SrgbColorSpace)
            );
            return MakeUnexpected(Failure{});
        }
        u32 hasAlpha = 0u;
        auto hasAlphaResult = ReadRequiredUnsignedField(nwbFilePath, asset, s_HasAlphaField, 0u, 1u);
        if(!hasAlphaResult)
            return MakeUnexpected(Failure{});
        hasAlpha = *hasAlphaResult;
        entry.hasAlpha = hasAlpha != 0u;
        entry.alphaMode = entry.hasAlpha ? TextureAlphaMode::EmbeddedLdr : TextureAlphaMode::Opaque;
    }

    auto virtualPathResult = ::NWB::Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    entry.virtualPath = *virtualPathResult;

    const auto payloadByteCountResult = BuildMipLevels(
        nwbFilePath, entry.payloadFormat, entry.dimension, entry.width, entry.height, entry.depth, entry.mipLevels
    );
    if(!payloadByteCountResult)
        return MakeUnexpected(Failure{});
    const u64 expectedPayloadBytes = *payloadByteCountResult;
    u64 expectedTotalPayloadBytes = expectedPayloadBytes;
    if(entry.alphaMode == TextureAlphaMode::SeparateUastcLdr4x4){
        if(expectedPayloadBytes > Limit<u64>::s_Max - expectedTotalPayloadBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': separate HDR alpha payload size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
            );
            return MakeUnexpected(Failure{});
        }
        expectedTotalPayloadBytes += expectedPayloadBytes;
    }

    AStringView dataFileName;
    auto dataFileNameResult = ::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_DataField, true);
    if(!dataFileNameResult)
        return MakeUnexpected(Failure{});
    dataFileName = dataFileNameResult->text;
    if(!ValidateTextureDataFileName(nwbFilePath, dataFileName, scratchArena))
        return MakeUnexpected(Failure{});

    Path dataPath(nwbFilePath.parentPath());
    dataPath /= dataFileName;
    auto readBinaryFileResult = ReadBinaryFile(dataPath, entry.payloadBytes);
    if(!readBinaryFileResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to read texture sidecar '{}': {}")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , PathToString<tchar>(dataPath)
            , StringConvert(readBinaryFileResult.error().message())
        );
        return MakeUnexpected(Failure{});
    }
    if(expectedTotalPayloadBytes != static_cast<u64>(entry.payloadBytes.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': texture sidecar size does not match the derived mip and alpha layout")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }
    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

