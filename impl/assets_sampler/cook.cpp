// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_sampler_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_DiagnosticPrefix = "Sampler meta";
static constexpr AStringView s_MinFilterField = "min_filter";
static constexpr AStringView s_MagFilterField = "mag_filter";
static constexpr AStringView s_MipFilterField = "mip_filter";
static constexpr AStringView s_AddressUField = "address_u";
static constexpr AStringView s_AddressVField = "address_v";
static constexpr AStringView s_AddressWField = "address_w";
static constexpr AStringView s_ReductionField = "reduction";
static constexpr AStringView s_MaxAnisotropyField = "max_anisotropy";
static constexpr AStringView s_MipBiasField = "mip_bias";
static constexpr AStringView s_BorderColorField = "border_color";

[[nodiscard]] static bool ReadFiniteF32Value(
    const Path& nwbFilePath,
    const Value& value,
    const AStringView fieldName,
    f32& outValue
){
    outValue = 0.0f;
    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be numeric")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const f64 numericValue = value.toDouble();
    if(
        !IsFinite(numericValue)
        || numericValue < static_cast<f64>(Limit<f32>::s_Min)
        || numericValue > static_cast<f64>(Limit<f32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is outside the supported float range")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    outValue = static_cast<f32>(numericValue);
    return true;
}

[[nodiscard]] static bool ReadRequiredFiniteF32Field(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    f32& outValue
){
    const Value* const field = asset.findField(fieldName);
    if(!field){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return ReadFiniteF32Value(nwbFilePath, *field, fieldName, outValue);
}

[[nodiscard]] static bool ParseFilter(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    bool& outLinear
){
    AStringView value;
    if(!Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, fieldName, true, value))
        return false;
    if(value == "nearest"){
        outLinear = false;
        return true;
    }
    if(value == "linear"){
        outLinear = true;
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be 'nearest' or 'linear'")
        , StringConvert(s_DiagnosticPrefix)
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
    );
    return false;
}

[[nodiscard]] static bool ParseAddressMode(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    Core::SamplerAddressMode::Enum& outAddressMode
){
    AStringView value;
    if(!Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, fieldName, true, value))
        return false;

    if(value == "clamp")
        outAddressMode = Core::SamplerAddressMode::Clamp;
    else if(value == "wrap")
        outAddressMode = Core::SamplerAddressMode::Wrap;
    else if(value == "border")
        outAddressMode = Core::SamplerAddressMode::Border;
    else if(value == "mirror")
        outAddressMode = Core::SamplerAddressMode::Mirror;
    else if(value == "mirror_once")
        outAddressMode = Core::SamplerAddressMode::MirrorOnce;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' has an unsupported address mode")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

[[nodiscard]] static bool ParseReductionType(
    const Path& nwbFilePath,
    const Value& asset,
    Core::SamplerReductionType::Enum& outReductionType
){
    AStringView value;
    if(!Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_ReductionField, true, value))
        return false;

    if(value == "standard")
        outReductionType = Core::SamplerReductionType::Standard;
    else if(value == "comparison")
        outReductionType = Core::SamplerReductionType::Comparison;
    else if(value == "minimum")
        outReductionType = Core::SamplerReductionType::Minimum;
    else if(value == "maximum")
        outReductionType = Core::SamplerReductionType::Maximum;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' has an unsupported reduction type")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_ReductionField)
        );
        return false;
    }
    return true;
}

[[nodiscard]] static bool ParseBorderColor(
    const Path& nwbFilePath,
    const Value& asset,
    Core::Color& outColor
){
    const Value* const field = asset.findField(s_BorderColorField);
    if(!field){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_BorderColorField)
        );
        return false;
    }
    if(!field->isList() || field->asList().size() != 4u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a four-component numeric list")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_BorderColorField)
        );
        return false;
    }

    f32 components[4u] = {};
    const auto& values = field->asList();
    for(usize index = 0u; index < LengthOf(components); ++index){
        if(!ReadFiniteF32Value(nwbFilePath, values[index], s_BorderColorField, components[index]))
            return false;
    }
    outColor = Core::Color(components[0u], components[1u], components[2u], components[3u]);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SamplerAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("SamplerAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Sampler::s_AssetTypeText)
        );
        return false;
    }

    const Sampler& sampler = static_cast<const Sampler&>(asset);
    if(!sampler.validatePayload())
        return false;

    const Core::SamplerDesc& description = sampler.description();
    SamplerBinaryPayload::HeaderBinary header;
    header.borderColorR = description.borderColor.r;
    header.borderColorG = description.borderColor.g;
    header.borderColorB = description.borderColor.b;
    header.borderColorA = description.borderColor.a;
    header.maxAnisotropy = description.maxAnisotropy;
    header.mipBias = description.mipBias;
    header.minFilter = description.minFilter ? 1u : 0u;
    header.magFilter = description.magFilter ? 1u : 0u;
    header.mipFilter = description.mipFilter ? 1u : 0u;
    header.addressU = static_cast<u32>(description.addressU);
    header.addressV = static_cast<u32>(description.addressV);
    header.addressW = static_cast<u32>(description.addressW);
    header.reductionType = static_cast<u32>(description.reductionType);

    outBinary.clear();
    outBinary.reserve(sizeof(header));
    AppendPOD(outBinary, header);
    return true;
}

bool ParseSamplerCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SamplerCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_sampler_cook;

    outEntry = SamplerCookEntry(*outEntry.arena);
    const Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset is not a map")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        {
            s_MinFilterField,
            s_MagFilterField,
            s_MipFilterField,
            s_AddressUField,
            s_AddressVField,
            s_AddressWField,
            s_ReductionField,
            s_MaxAnisotropyField,
            s_MipBiasField,
            s_BorderColorField,
        }
    ))
        return false;

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, outEntry.virtualPath, scratchArena))
        return false;

    Core::SamplerDesc description;
    if(
        !ParseFilter(nwbFilePath, asset, s_MinFilterField, description.minFilter)
        || !ParseFilter(nwbFilePath, asset, s_MagFilterField, description.magFilter)
        || !ParseFilter(nwbFilePath, asset, s_MipFilterField, description.mipFilter)
        || !ParseAddressMode(nwbFilePath, asset, s_AddressUField, description.addressU)
        || !ParseAddressMode(nwbFilePath, asset, s_AddressVField, description.addressV)
        || !ParseAddressMode(nwbFilePath, asset, s_AddressWField, description.addressW)
        || !ParseReductionType(nwbFilePath, asset, description.reductionType)
        || !ReadRequiredFiniteF32Field(nwbFilePath, asset, s_MaxAnisotropyField, description.maxAnisotropy)
        || !ReadRequiredFiniteF32Field(nwbFilePath, asset, s_MipBiasField, description.mipBias)
        || !ParseBorderColor(nwbFilePath, asset, description.borderColor)
    )
        return false;
    if(!IsValidSamplerDescription(description)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': sampler description is invalid")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outEntry.description = description;
    return true;
}

bool BuildSamplerAsset(const SamplerCookEntry& samplerEntry, Sampler& outSampler){
    if(!samplerEntry.arena || !samplerEntry.virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Sampler cook: sampler entry is invalid"));
        return false;
    }

    outSampler = Sampler(*samplerEntry.arena, samplerEntry.virtualPath);
    outSampler.setDescription(samplerEntry.description);
    return outSampler.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
