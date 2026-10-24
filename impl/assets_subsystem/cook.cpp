// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/binary.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SubsystemAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, SubsystemBinaryPayload::s_SubsystemSerializeContext))
        return false;

    const Subsystem& subsystem = static_cast<const Subsystem&>(asset);
    Core::Alloc::ScratchArena scratchArena(AssetsSubsystemArenaScope::s_SerializeArena);
    if(!subsystem.validatePayload(scratchArena))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);
    Vector<SubsystemBinaryPayload::SubsystemFeatureBinary, Core::Alloc::ScratchArena> featureBinaries(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> featureAssetHashes(scratchArena);

    subsystemHashes.reserve(subsystem.subsystems().size());
    for(const Subsystem::SubsystemRef& child : subsystem.subsystems())
        subsystemHashes.push_back(child.name().hash());

    assetHashes.reserve(subsystem.assets().size());
    for(const Subsystem::AssetRef& item : subsystem.assets())
        assetHashes.push_back(item.name().hash());

    usize totalFeatureAssets = 0u;
    for(const SubsystemFeature& feature : subsystem.features()){
        if(feature.assets.size() > Limit<usize>::s_Max - totalFeatureAssets){
            NWB_LOGGER_ERROR(NWB_TEXT("SubsystemAssetCodec::serialize failed: total feature asset count overflows"));
            return false;
        }
        totalFeatureAssets += feature.assets.size();
    }

    featureBinaries.reserve(subsystem.features().size());
    featureAssetHashes.reserve(totalFeatureAssets);
    for(const SubsystemFeature& feature : subsystem.features()){
        SubsystemBinaryPayload::SubsystemFeatureBinary featureBinary;
        featureBinary.featureNameHash = feature.name.hash();
        featureBinary.assetOffset = static_cast<u64>(featureAssetHashes.size());
        featureBinary.assetCount = static_cast<u64>(feature.assets.size());
        for(const Subsystem::AssetRef& item : feature.assets)
            featureAssetHashes.push_back(item.name().hash());
        featureBinaries.push_back(featureBinary);
    }

    usize reserveBytes = sizeof(SubsystemBinaryPayload::SubsystemHeaderBinary);
    const bool canReserve =
        AddBinaryVectorReserveBytes(reserveBytes, subsystemHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, assetHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, featureBinaries)
        && AddBinaryVectorReserveBytes(reserveBytes, featureAssetHashes)
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SubsystemBinaryPayload::SubsystemHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystemHashes.size());
    header.assetCount = static_cast<u64>(assetHashes.size());
    header.featureCount = static_cast<u64>(featureBinaries.size());
    header.featureAssetCount = static_cast<u64>(featureAssetHashes.size());
    AppendPOD(outBinary, header);

    return
        Core::Assets::AppendVectorPayload(outBinary, subsystemHashes, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_SubsystemsPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, assetHashes, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_AssetsPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, featureBinaries, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_FeaturesPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, featureAssetHashes, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_FeatureAssetsPayloadLabel)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_subsystem_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_SubsystemsField = "subsystems";
static constexpr AStringView s_AssetsField = "assets";
static constexpr AStringView s_FeaturesField = "features";
static constexpr AStringView s_DiagnosticPrefix = "Subsystem meta";


template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetRef<TAsset>> ParseAssetRefString(
    const Path& nwbFilePath,
    const Value& itemValue,
    const AStringView fieldLabel,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!itemValue.isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entries must be strings"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const AStringView text(itemValue.asString().data(), itemValue.asString().size());
    if(TrimView(text).empty() || text != TrimView(text)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be a non-empty trimmed virtual path"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldLabel), StringConvert(text));
        return MakeUnexpected(Failure{});
    }
    if(text.find('\\') != AStringView::npos || !Core::Assets::HasReservedAssetVirtualRoot(text, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be an engine/- or project/-rooted virtual path"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldLabel), StringConvert(text));
        return MakeUnexpected(Failure{});
    }
    const auto ref = Core::Assets::AssetRef<TAsset>::FromText(text);
    if(!ref || !ref->valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' is invalid"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldLabel), StringConvert(text));
        return MakeUnexpected(Failure{});
    }
    return *ref;
}

template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>>> ParseAssetRefList(
    const Path& nwbFilePath,
    const Value& listValue,
    const AStringView fieldLabel,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!listValue.isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const auto& items = listValue.asList();
    Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>> refs(arena);
    refs.reserve(items.size());
    for(const Value& item : items){
        const auto parsedRef = ParseAssetRefString<TAsset>(nwbFilePath, item, fieldLabel, scratchArena);
        if(!parsedRef)
            return MakeUnexpected(Failure{});
        refs.push_back(*parsedRef);
    }
    return refs;
}

[[nodiscard]] static Expected<Subsystem::FeatureVector> ParseFeaturesMap(
    const Path& nwbFilePath,
    const Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    Subsystem::FeatureVector features(arena);
    const Value* fieldValue = asset.findField(s_FeaturesField);
    if(!fieldValue)
        return features;
    if(!fieldValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a map"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(s_FeaturesField));
        return MakeUnexpected(Failure{});
    }

    const auto& map = fieldValue->asMap();
    features.reserve(map.size());
    for(const auto& [featureKey, featureListValue] : map){
        const AStringView featureKeyView(featureKey.data(), featureKey.size());
        if(TrimView(featureKeyView).empty() || featureKeyView != TrimView(featureKeyView)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': feature name '{}' must not be empty or padded"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(featureKeyView));
            return MakeUnexpected(Failure{});
        }
        const auto featureName = Name::FromText(featureKeyView);
        if(!featureName || !*featureName){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': feature name '{}' contains an embedded null"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(featureKeyView));
            return MakeUnexpected(Failure{});
        }

        auto featureAssets = ParseAssetRefList<Core::Assets::IAsset>(nwbFilePath, featureListValue, featureKeyView, arena, scratchArena);
        if(!featureAssets)
            return MakeUnexpected(Failure{});
        if(featureAssets->empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': feature '{}' must declare at least one asset"), StringConvert(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(featureKeyView));
            return MakeUnexpected(Failure{});
        }
        features.push_back(SubsystemFeature(*featureName, Move(*featureAssets)));
    }
    return features;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SubsystemCookEntry> ParseSubsystemCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_subsystem_cook;

    SubsystemCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_DiagnosticPrefix, { s_SubsystemsField, s_AssetsField, s_FeaturesField }))
        return MakeUnexpected(Failure{});

    if(const Core::Metascript::Value* subsystemsValue = asset.findField(s_SubsystemsField)){
        auto subsystems = ParseAssetRefList<Subsystem>(nwbFilePath, *subsystemsValue, s_SubsystemsField, arena, scratchArena);
        if(!subsystems)
            return MakeUnexpected(Failure{});
        entry.subsystems = Move(*subsystems);
    }

    if(const Core::Metascript::Value* assetsValue = asset.findField(s_AssetsField)){
        auto assets = ParseAssetRefList<Core::Assets::IAsset>(nwbFilePath, *assetsValue, s_AssetsField, arena, scratchArena);
        if(!assets)
            return MakeUnexpected(Failure{});
        entry.assets = Move(*assets);
    }

    auto features = ParseFeaturesMap(nwbFilePath, asset, arena, scratchArena);
    if(!features)
        return MakeUnexpected(Failure{});
    entry.features = Move(*features);

    Subsystem candidate(arena, entry.virtualPath);
    Subsystem::FeatureVector candidateFeatures(arena);
    candidateFeatures.reserve(entry.features.size());
    for(const SubsystemFeature& feature : entry.features)
        candidateFeatures.push_back(SubsystemFeature(feature.name, Subsystem::AssetRefVector(feature.assets)));
    candidate.setContents(Subsystem::SubsystemRefVector(entry.subsystems), Subsystem::AssetRefVector(entry.assets), Move(candidateFeatures));
    if(!candidate.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<SubsystemCookEntry> ParseSubsystemCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, __hidden_subsystem_cook::s_DiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseSubsystemCookMetadata(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

Expected<Subsystem> BuildSubsystemAsset(
    SubsystemCookEntry& entry,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    Subsystem asset(arena, entry.virtualPath);
    asset.setContents(Move(entry.subsystems), Move(entry.assets), Move(entry.features));
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

