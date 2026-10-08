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

static constexpr ::NamedEnumCase<bool> s_FilterCases[] = {
    { "nearest", false },
    { "linear", true },
};

[[nodiscard]] static Expected<bool> ParseFilter(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName
){
    return Core::Assets::ParseNamedMetadataEnumField<bool>(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        fieldName,
        s_FilterCases,
        LengthOf(s_FilterCases),
        "must be 'nearest' or 'linear'"
    );
}

static constexpr ::NamedEnumCase<Core::SamplerAddressMode::Enum> s_AddressModeCases[] = {
    { "clamp", Core::SamplerAddressMode::Clamp },
    { "wrap", Core::SamplerAddressMode::Wrap },
    { "border", Core::SamplerAddressMode::Border },
    { "mirror", Core::SamplerAddressMode::Mirror },
    { "mirror_once", Core::SamplerAddressMode::MirrorOnce },
};

static constexpr ::NamedEnumCase<Core::SamplerReductionType::Enum> s_ReductionTypeCases[] = {
    { "standard", Core::SamplerReductionType::Standard },
    { "comparison", Core::SamplerReductionType::Comparison },
};

[[nodiscard]] static Expected<Core::SamplerAddressMode::Enum> ParseAddressMode(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName
){
    return Core::Assets::ParseNamedMetadataEnumField<Core::SamplerAddressMode::Enum>(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        fieldName,
        s_AddressModeCases,
        LengthOf(s_AddressModeCases),
        "has an unsupported address mode"
    );
}

[[nodiscard]] static Expected<Core::SamplerReductionType::Enum> ParseReductionType(
    const Path& nwbFilePath,
    const Value& asset
){
    return Core::Assets::ParseNamedMetadataEnumField<Core::SamplerReductionType::Enum>(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        s_ReductionField,
        s_ReductionTypeCases,
        LengthOf(s_ReductionTypeCases),
        "has an unsupported reduction type"
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SamplerAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("SamplerAssetCodec::serialize")))
        return false;

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

Expected<SamplerCookEntry> ParseSamplerCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_sampler_cook;

    SamplerCookEntry entry(arena);
    const Value& asset = doc.asset();
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});
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
        }
    ))
        return MakeUnexpected(Failure{});

    auto virtualPathResult = Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    entry.virtualPath = *virtualPathResult;

    Core::SamplerDesc description;
    description.borderColor = Core::Color(0.0f, 0.0f, 0.0f, 0.0f);
    auto minFilterResult = ParseFilter(nwbFilePath, asset, s_MinFilterField);
    if(!minFilterResult)
        return MakeUnexpected(Failure{});
    description.minFilter = *minFilterResult;
    auto magFilterResult = ParseFilter(nwbFilePath, asset, s_MagFilterField);
    if(!magFilterResult)
        return MakeUnexpected(Failure{});
    description.magFilter = *magFilterResult;
    auto mipFilterResult = ParseFilter(nwbFilePath, asset, s_MipFilterField);
    if(!mipFilterResult)
        return MakeUnexpected(Failure{});
    description.mipFilter = *mipFilterResult;
    auto addressUResult = ParseAddressMode(nwbFilePath, asset, s_AddressUField);
    if(!addressUResult)
        return MakeUnexpected(Failure{});
    description.addressU = *addressUResult;
    auto addressVResult = ParseAddressMode(nwbFilePath, asset, s_AddressVField);
    if(!addressVResult)
        return MakeUnexpected(Failure{});
    description.addressV = *addressVResult;
    auto addressWResult = ParseAddressMode(nwbFilePath, asset, s_AddressWField);
    if(!addressWResult)
        return MakeUnexpected(Failure{});
    description.addressW = *addressWResult;
    auto reductionTypeResult = ParseReductionType(nwbFilePath, asset);
    if(!reductionTypeResult)
        return MakeUnexpected(Failure{});
    description.reductionType = *reductionTypeResult;
    auto maxAnisotropyResult = Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_DiagnosticPrefix, s_MaxAnisotropyField, true);
    if(!maxAnisotropyResult)
        return MakeUnexpected(Failure{});
    description.maxAnisotropy = *maxAnisotropyResult;
    auto mipBiasResult = Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_DiagnosticPrefix, s_MipBiasField, true);
    if(!mipBiasResult)
        return MakeUnexpected(Failure{});
    description.mipBias = *mipBiasResult;
    if(!IsValidSamplerDescription(description)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': sampler description is invalid")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }

    entry.description = description;
    return entry;
}

Expected<Sampler> BuildSamplerAsset(const SamplerCookEntry& samplerEntry, Core::Assets::AssetArena& arena){
    if(!samplerEntry.arena || !samplerEntry.virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Sampler cook: sampler entry is invalid"));
        return MakeUnexpected(Failure{});
    }

    Sampler asset(arena, samplerEntry.virtualPath);
    asset.setDescription(samplerEntry.description);
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

