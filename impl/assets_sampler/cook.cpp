// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/cook_metadata.h>
#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_sampler_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr Array<::NamedEnumCase<b8>, 2u> s_FilterCases = {
    ::NamedEnumCase<b8>{ s_SamplerFilterNearest, false },
    ::NamedEnumCase<b8>{ s_SamplerFilterLinear, true },
};

static constexpr Array<::NamedEnumCase<Core::SamplerAddressMode::Enum>, 5u> s_AddressModeCases = {
    ::NamedEnumCase<Core::SamplerAddressMode::Enum>{ s_SamplerAddressClamp, Core::SamplerAddressMode::Clamp },
    ::NamedEnumCase<Core::SamplerAddressMode::Enum>{ s_SamplerAddressWrap, Core::SamplerAddressMode::Wrap },
    ::NamedEnumCase<Core::SamplerAddressMode::Enum>{ s_SamplerAddressBorder, Core::SamplerAddressMode::Border },
    ::NamedEnumCase<Core::SamplerAddressMode::Enum>{ s_SamplerAddressMirror, Core::SamplerAddressMode::Mirror },
    ::NamedEnumCase<Core::SamplerAddressMode::Enum>{ s_SamplerAddressMirrorOnce, Core::SamplerAddressMode::MirrorOnce },
};

static constexpr Array<::NamedEnumCase<Core::SamplerReductionType::Enum>, 2u> s_ReductionTypeCases = {
    ::NamedEnumCase<Core::SamplerReductionType::Enum>{ s_SamplerReductionStandard, Core::SamplerReductionType::Standard },
    ::NamedEnumCase<Core::SamplerReductionType::Enum>{ s_SamplerReductionComparison, Core::SamplerReductionType::Comparison },
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SamplerAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("SamplerAssetCodec::serialize")))
        return false;

    const Sampler& sampler = *MakeNotNull(checked_cast<const Sampler*>(&asset));
    if(!sampler.validatePayload())
        return false;

    const Core::SamplerDesc& description = sampler.description();
    SamplerBinaryHeader header;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SamplerCookEntry> ParseSamplerCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena&, Core::Alloc::ScratchArena&){
    using namespace __hidden_sampler_cook;

    SamplerCookEntry entry;
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_SamplerMetaDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, &IsAllowedSamplerMetadataField))
        return MakeUnexpected(Failure{});

    Core::SamplerDesc description;
    description.borderColor = Core::Color(0.0f, 0.0f, 0.0f, 0.0f);
    auto minFilterResult = Core::Assets::ParseNamedMetadataEnumField<b8>(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, s_SamplerMinFilterField, s_FilterCases, s_SamplerFilterHint);
    if(!minFilterResult)
        return MakeUnexpected(Failure{});
    description.minFilter = *minFilterResult;
    auto magFilterResult = Core::Assets::ParseNamedMetadataEnumField<b8>(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, s_SamplerMagFilterField, s_FilterCases, s_SamplerFilterHint);
    if(!magFilterResult)
        return MakeUnexpected(Failure{});
    description.magFilter = *magFilterResult;
    auto mipFilterResult = Core::Assets::ParseNamedMetadataEnumField<b8>(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, s_SamplerMipFilterField, s_FilterCases, s_SamplerFilterHint);
    if(!mipFilterResult)
        return MakeUnexpected(Failure{});
    description.mipFilter = *mipFilterResult;
    auto addressUResult = Core::Assets::ParseNamedMetadataEnumField<Core::SamplerAddressMode::Enum>(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, s_SamplerAddressUField, s_AddressModeCases, s_SamplerAddressModeHint);
    if(!addressUResult)
        return MakeUnexpected(Failure{});
    description.addressU = *addressUResult;
    auto addressVResult = Core::Assets::ParseNamedMetadataEnumField<Core::SamplerAddressMode::Enum>(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, s_SamplerAddressVField, s_AddressModeCases, s_SamplerAddressModeHint);
    if(!addressVResult)
        return MakeUnexpected(Failure{});
    description.addressV = *addressVResult;
    auto addressWResult = Core::Assets::ParseNamedMetadataEnumField<Core::SamplerAddressMode::Enum>(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, s_SamplerAddressWField, s_AddressModeCases, s_SamplerAddressModeHint);
    if(!addressWResult)
        return MakeUnexpected(Failure{});
    description.addressW = *addressWResult;
    auto reductionTypeResult = Core::Assets::ParseNamedMetadataEnumField<Core::SamplerReductionType::Enum>(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, s_SamplerReductionField, s_ReductionTypeCases, s_SamplerReductionTypeHint);
    if(!reductionTypeResult)
        return MakeUnexpected(Failure{});
    description.reductionType = *reductionTypeResult;
    auto maxAnisotropyResult = Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, s_SamplerMaxAnisotropyField, true);
    if(!maxAnisotropyResult)
        return MakeUnexpected(Failure{});
    description.maxAnisotropy = *maxAnisotropyResult;
    auto mipBiasResult = Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_SamplerMetaDiagnosticPrefix, s_SamplerMipBiasField, true);
    if(!mipBiasResult)
        return MakeUnexpected(Failure{});
    description.mipBias = *mipBiasResult;
    if(!IsValidSamplerDescription(description)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': sampler description is invalid"), ConvertString(s_SamplerMetaDiagnosticPrefix), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    entry.description = description;
    return entry;
}

Expected<SamplerCookEntry> ParseSamplerCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    return Core::Assets::ParseCookMetadataDocument<SamplerCookEntry, &ParseSamplerCookMetadata>(assetRoot, virtualRoot, nwbFilePath, doc, s_SamplerMetaDiagnosticPrefix, arena, scratchArena);
}

Expected<Sampler> BuildSamplerAsset(const SamplerCookEntry& samplerEntry, Core::Assets::AssetArena& arena){
    if(!samplerEntry.virtualPath){
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

