// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/filesystem.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Metascript::Value;

static Core::Assets::AssetPairedSidecarRuleAutoRegistrar s_TexturePairedSidecarRegistrar(Core::Assets::AssetPairedSidecarRule{
    .firstToken = "texture",
    .sidecarExtension = TextureFormat::s_TextureDataExtension,
    .bunchScanKeyword = {},
});


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<u32> ReadUnsignedField(const Path& nwbFilePath, const Value& asset, const AStringView fieldName, const u32 minimum, const u32 maximum){
    const Value* const field = asset.findField(fieldName);
    if(!field){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), ConvertString(s_TextureDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }
    if(!field->isInteger() || field->asInteger() < 0 || static_cast<u64>(field->asInteger()) < minimum || static_cast<u64>(field->asInteger()) > maximum){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be an integer in [{}, {}]"), ConvertString(s_TextureDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName), minimum, maximum);
        return MakeUnexpected(Failure{});
    }
    return static_cast<u32>(field->asInteger());
}

[[nodiscard]] static Expected<TextureResolution> ReadResolution(const Path& nwbFilePath, const Value& asset, const TextureDimension::Enum dimension){
    const Value* const field = asset.findField(s_TextureResolutionField);
    const usize componentCount = dimension == TextureDimension::Texture3D ? s_VolumeResolutionComponentCount : s_PlanarResolutionComponentCount;
    if(!field || !field->isList() || field->asList().size() != componentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': resolution must contain exactly {} positive integer dimensions"), ConvertString(s_TextureDiagnosticPrefix), PathToString<tchar>(nwbFilePath), componentCount);
        return MakeUnexpected(Failure{});
    }
    Array<u32, s_VolumeResolutionComponentCount> dimensions{ 0u, 0u, 1u };
    for(usize component = 0u; component < componentCount; ++component){
        const Value& value = field->asList()[component];
        if(!value.isInteger() || value.asInteger() <= 0 || static_cast<u64>(value.asInteger()) > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': resolution component {} must be a positive u32 integer"), ConvertString(s_TextureDiagnosticPrefix), PathToString<tchar>(nwbFilePath), component);
            return MakeUnexpected(Failure{});
        }
        dimensions[component] = static_cast<u32>(value.asInteger());
    }
    if(dimension == TextureDimension::TextureCube && dimensions[0u] != dimensions[1u]){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': cube resolution must be square"), ConvertString(s_TextureDiagnosticPrefix), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    return TextureResolution(dimensions[0u], dimensions[1u], dimensions[2u]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TextureCookEntry> ParseTextureCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_texture_cook_metadata;

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_TextureDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    const auto alphaMode = Core::Assets::ParseNamedMetadataEnumField<TextureAlphaMode::Enum>(
        nwbFilePath,
        asset,
        s_TextureDiagnosticPrefix,
        s_TextureAlphaModeField,
        TextureFormat::s_AlphaModeCases,
        "has unsupported alpha_mode"
    );
    if(!alphaMode)
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_TextureDiagnosticPrefix, [allowAlphaConstant = *alphaMode == TextureAlphaMode::ConstantUnorm8](const AStringView fieldName)noexcept{
        return IsAllowedTextureMetadataField(fieldName, allowAlphaConstant);
    }))
        return MakeUnexpected(Failure{});

    const auto format = Core::Assets::ParseNamedMetadataEnumField<TexturePayloadFormat::Enum>(
        nwbFilePath,
        asset,
        s_TextureDiagnosticPrefix,
        s_TextureFormatField,
        TextureFormat::s_PayloadFormatCases,
        "has unsupported texture format"
    );
    const auto dimension = Core::Assets::ParseNamedMetadataEnumField<TextureDimension::Enum>(
        nwbFilePath,
        asset,
        s_TextureDiagnosticPrefix,
        s_TextureDimensionField,
        TextureFormat::s_DimensionCases,
        "has unsupported texture dimension"
    );
    if(!format || !dimension)
        return MakeUnexpected(Failure{});
    const auto resolution = ReadResolution(nwbFilePath, asset, *dimension);
    if(!resolution)
        return MakeUnexpected(Failure{});
    const auto completeMipCount = TextureFormat::ComputeCompleteMipCount(*dimension, resolution->width, resolution->height, resolution->depth);
    if(!completeMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': resolution cannot describe a complete mip chain"), ConvertString(s_TextureDiagnosticPrefix), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    TextureCookEntry entry(arena);
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_TextureDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    entry.width = resolution->width;
    entry.height = resolution->height;
    entry.depth = resolution->depth;
    entry.dimension = *dimension;
    entry.payloadFormat = *format;
    entry.alphaMode = *alphaMode;
    if(entry.alphaMode == TextureAlphaMode::ConstantUnorm8){
        const auto constant = ReadUnsignedField(nwbFilePath, asset, s_TextureAlphaConstantField, 0u, TextureFormat::s_MaxConstantAlphaUnorm8);
        if(!constant)
            return MakeUnexpected(Failure{});
        entry.alphaConstantUnorm8 = static_cast<u8>(*constant);
    }

    ::Path<Core::Alloc::ScratchArena> dataPath(scratchArena, nwbFilePath);
    dataPath.replaceExtension(TextureFormat::s_TextureDataExtension);
    const auto readResult = ReadBinaryFile(dataPath, entry.payloadBytes);
    if(!readResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to read raw texture sidecar '{}': {}"), ConvertString(s_TextureDiagnosticPrefix), PathToString<tchar>(nwbFilePath), PathToString<tchar>(dataPath), ConvertString(readResult.error().message()));
        return MakeUnexpected(Failure{});
    }
    return entry;
}

Expected<TextureCookEntry> ParseTextureCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    return Core::Assets::ParseCookMetadataDocument<TextureCookEntry, &ParseTextureCookMetadata>(assetRoot, virtualRoot, nwbFilePath, doc, s_TextureDiagnosticPrefix, arena, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

