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

    const Subsystem& subsystem = *MakeNotNull(checked_cast<const Subsystem*>(&asset));
    Core::Alloc::ScratchArena scratchArena(AssetsSubsystemArenaScope::s_SerializeArena);
    if(!subsystem.validatePayload(scratchArena))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<SubsystemBinaryPayload::SubsystemAssetEntryBinary, Core::Alloc::ScratchArena> assetEntries(scratchArena);

    subsystemHashes.reserve(subsystem.subsystems().size());
    for(const Subsystem::SubsystemRef& child : subsystem.subsystems())
        subsystemHashes.push_back(child.name().hash());

    assetEntries.reserve(subsystem.assets().size());
    for(const auto& [nickname, item] : subsystem.assets()){
        SubsystemBinaryPayload::SubsystemAssetEntryBinary entry;
        entry.nicknameHash = nickname.hash();
        entry.assetHash = item.name().hash();
        assetEntries.push_back(entry);
    }
    Sort(assetEntries.begin(), assetEntries.end(), [](const SubsystemBinaryPayload::SubsystemAssetEntryBinary& lhs, const SubsystemBinaryPayload::SubsystemAssetEntryBinary& rhs)noexcept{
        return LessNameHash(lhs.nicknameHash, rhs.nicknameHash);
    });

    usize reserveBytes = sizeof(SubsystemBinaryPayload::SubsystemHeaderBinary);
    const bool canReserve =
        AddBinaryVectorReserveBytes(reserveBytes, subsystemHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, assetEntries)
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SubsystemBinaryPayload::SubsystemHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystemHashes.size());
    header.assetCount = static_cast<u64>(assetEntries.size());
    AppendPOD(outBinary, header);

    return
        Core::Assets::AppendVectorPayload(outBinary, subsystemHashes, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_SubsystemsPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, assetEntries, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_AssetsPayloadLabel)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_subsystem_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_SubsystemsField = "subsystems";
static constexpr AStringView s_AssetsField = "assets";
static constexpr AStringView s_DiagnosticPrefix = "Subsystem meta";


template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetRef<TAsset>> ParseAssetRefString(
    const Path& nwbFilePath,
    const Value& itemValue,
    const AStringView fieldLabel,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!itemValue.isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entries must be strings"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const AStringView text(itemValue.asString().data(), itemValue.asString().size());
    if(TrimView(text).empty() || text != TrimView(text)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be a non-empty trimmed virtual path"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    if(text.find('\\') != AStringView::npos || !Core::Assets::HasReservedAssetVirtualRoot(text, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be an engine/- or project/-rooted virtual path"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    const auto ref = Core::Assets::AssetRef<TAsset>::FromText(text);
    if(!ref || !ref->valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' is invalid"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
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
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
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

[[nodiscard]] static Expected<Subsystem::AssetMap> ParseAssetsMap(
    const Path& nwbFilePath,
    const Value& mapValue,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!mapValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a map"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_AssetsField));
        return MakeUnexpected(Failure{});
    }

    const auto& map = mapValue.asMap();
    Subsystem::AssetMap assets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    assets.reserve(map.size());
    for(const auto& [nicknameKey, assetValue] : map){
        const AStringView nicknameView(nicknameKey.data(), nicknameKey.size());
        if(TrimView(nicknameView).empty() || nicknameView != TrimView(nicknameView)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset nickname '{}' must not be empty or padded"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
        const auto nickname = Name::FromText(nicknameView);
        if(!nickname || !*nickname){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset nickname '{}' contains an embedded null"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }

        const auto parsedRef = ParseAssetRefString<Core::Assets::IAsset>(nwbFilePath, assetValue, nicknameView, scratchArena);
        if(!parsedRef)
            return MakeUnexpected(Failure{});
        if(!assets.emplace(*nickname, *parsedRef).second){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': duplicate asset nickname '{}'"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
    }
    return assets;
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
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_DiagnosticPrefix, { s_SubsystemsField, s_AssetsField }))
        return MakeUnexpected(Failure{});

    if(const Core::Metascript::Value* subsystemsValue = asset.findField(s_SubsystemsField)){
        auto subsystems = ParseAssetRefList<Subsystem>(nwbFilePath, *subsystemsValue, s_SubsystemsField, arena, scratchArena);
        if(!subsystems)
            return MakeUnexpected(Failure{});
        entry.subsystems = Move(*subsystems);
    }

    if(const Core::Metascript::Value* assetsValue = asset.findField(s_AssetsField)){
        auto assets = ParseAssetsMap(nwbFilePath, *assetsValue, arena, scratchArena);
        if(!assets)
            return MakeUnexpected(Failure{});
        entry.assets = Move(*assets);
    }

    Subsystem candidate(arena, entry.virtualPath);
    candidate.setContents(Subsystem::SubsystemRefVector(entry.subsystems), Subsystem::AssetMap(entry.assets));
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
    asset.setContents(Move(entry.subsystems), Move(entry.assets));
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

