// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

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
static constexpr AStringView s_DiagnosticPrefix = "Texture meta";
static constexpr AStringView s_FormatField = "format";
static constexpr AStringView s_DimensionField = "dimension";
static constexpr AStringView s_ResolutionField = "resolution";
static constexpr AStringView s_MipCountField = "mip_count";
static constexpr AStringView s_AlphaModeField = "alpha_mode";
static constexpr AStringView s_AlphaConstantField = "alpha_constant_unorm8";

struct Resolution{
    u32 width;
    u32 height;
    u32 depth;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<u32> ReadUnsignedField(const Path& nwbFilePath, const Value& asset, const AStringView fieldName, const u32 minimum, const u32 maximum){
    const Value* const field = Core::Metascript::FindField(asset, fieldName);
    if(!field){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName));
        return MakeUnexpected(Failure{});
    }
    if(!field->isInteger() || field->asInteger() < 0 || static_cast<u64>(field->asInteger()) < minimum || static_cast<u64>(field->asInteger()) > maximum){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be an integer in [{}, {}]"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName), minimum, maximum);
        return MakeUnexpected(Failure{});
    }
    return static_cast<u32>(field->asInteger());
}

[[nodiscard]] static Expected<TexturePayloadFormat::Enum> ReadFormat(const Path& nwbFilePath, const Value& asset){
    const auto text = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_FormatField, true);
    if(!text)
        return MakeUnexpected(Failure{});
    if(text->text == TextureFormat::s_UastcLdr4x4Format)
        return TexturePayloadFormat::UastcLdr4x4;
    if(text->text == TextureFormat::s_UastcHdr4x4Format)
        return TexturePayloadFormat::UastcHdr4x4;
    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': unsupported texture format '{}'"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(text->text));
    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<TextureDimension::Enum> ReadDimension(const Path& nwbFilePath, const Value& asset){
    const auto text = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_DimensionField, true);
    if(!text)
        return MakeUnexpected(Failure{});
    if(text->text == TextureFormat::s_Texture2DDimension)
        return TextureDimension::Texture2D;
    if(text->text == TextureFormat::s_TextureCubeDimension)
        return TextureDimension::TextureCube;
    if(text->text == TextureFormat::s_Texture3DDimension)
        return TextureDimension::Texture3D;
    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': unsupported texture dimension '{}'"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(text->text));
    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<TextureAlphaMode::Enum> ReadAlphaMode(const Path& nwbFilePath, const Value& asset){
    const auto text = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_AlphaModeField, true);
    if(!text)
        return MakeUnexpected(Failure{});
    if(text->text == TextureFormat::s_OpaqueAlphaMode)
        return TextureAlphaMode::Opaque;
    if(text->text == TextureFormat::s_EmbeddedLdrAlphaMode)
        return TextureAlphaMode::EmbeddedLdr;
    if(text->text == TextureFormat::s_ConstantUnorm8AlphaMode)
        return TextureAlphaMode::ConstantUnorm8;
    if(text->text == TextureFormat::s_SeparateUastcLdr4x4AlphaMode)
        return TextureAlphaMode::SeparateUastcLdr4x4;
    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': unsupported alpha_mode '{}'"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(text->text));
    return MakeUnexpected(Failure{});
}

[[nodiscard]] static Expected<Resolution> ReadResolution(const Path& nwbFilePath, const Value& asset, const TextureDimension::Enum dimension){
    const Value* const field = Core::Metascript::FindField(asset, s_ResolutionField);
    const usize componentCount = dimension == TextureDimension::Texture3D ? 3u : 2u;
    if(!field || !field->isList() || field->asList().size() != componentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': resolution must contain exactly {} positive integer dimensions"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), componentCount);
        return MakeUnexpected(Failure{});
    }
    Array<u32, 3> dimensions{ 0u, 0u, 1u };
    for(usize component = 0u; component < componentCount; ++component){
        const Value& value = field->asList()[component];
        if(!value.isInteger() || value.asInteger() <= 0 || static_cast<u64>(value.asInteger()) > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': resolution component {} must be a positive u32 integer"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), component);
            return MakeUnexpected(Failure{});
        }
        dimensions[component] = static_cast<u32>(value.asInteger());
    }
    if(dimension == TextureDimension::TextureCube && dimensions[0u] != dimensions[1u]){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': cube resolution must be square"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }
    return Resolution{ dimensions[0u], dimensions[1u], dimensions[2u] };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TextureCookEntry> ParseTextureCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_texture_cook_metadata;

    const Value& asset = doc.asset();
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});
    const auto alphaMode = ReadAlphaMode(nwbFilePath, asset);
    if(!alphaMode)
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_DiagnosticPrefix, [alphaMode = *alphaMode](const AStringView fieldName)noexcept{
        return fieldName == s_FormatField || fieldName == s_DimensionField || fieldName == s_ResolutionField || fieldName == s_MipCountField || fieldName == s_AlphaModeField
            || (alphaMode == TextureAlphaMode::ConstantUnorm8 && fieldName == s_AlphaConstantField);
    }))
        return MakeUnexpected(Failure{});

    const auto format = ReadFormat(nwbFilePath, asset);
    const auto dimension = ReadDimension(nwbFilePath, asset);
    if(!format || !dimension)
        return MakeUnexpected(Failure{});
    const auto resolution = ReadResolution(nwbFilePath, asset, *dimension);
    const auto mipCount = ReadUnsignedField(nwbFilePath, asset, s_MipCountField, 1u, Limit<u32>::s_Max);
    if(!resolution || !mipCount)
        return MakeUnexpected(Failure{});
    const auto completeMipCount = TextureFormat::ComputeCompleteMipCount(*dimension, resolution->width, resolution->height, resolution->depth);
    if(!completeMipCount || *mipCount != *completeMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip_count must describe a complete mip chain"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    TextureCookEntry entry(arena);
    entry.width = resolution->width;
    entry.height = resolution->height;
    entry.depth = resolution->depth;
    entry.dimension = *dimension;
    entry.payloadFormat = *format;
    entry.mipCount = *mipCount;
    entry.alphaMode = *alphaMode;
    if(entry.alphaMode == TextureAlphaMode::ConstantUnorm8){
        const auto constant = ReadUnsignedField(nwbFilePath, asset, s_AlphaConstantField, 0u, TextureFormat::s_MaxConstantAlphaUnorm8);
        if(!constant)
            return MakeUnexpected(Failure{});
        entry.alphaConstantUnorm8 = static_cast<u8>(*constant);
    }

    const auto virtualPath = Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPath)
        return MakeUnexpected(Failure{});
    entry.virtualPath = *virtualPath;
    Path dataPath(nwbFilePath);
    dataPath.replaceExtension(TextureFormat::s_TextureDataExtension);
    const auto readResult = ReadBinaryFile(dataPath, entry.payloadBytes);
    if(!readResult){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to read raw texture sidecar '{}': {}"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), PathToString<tchar>(dataPath), StringConvert(readResult.error().message()));
        return MakeUnexpected(Failure{});
    }
    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

