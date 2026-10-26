// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/binary.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SubsystemAssetSerialization::Serialize(const ISubsystem& subsystem, Core::Assets::AssetBytes& outBinary){
    Core::Alloc::ScratchArena scratchArena(AssetsSubsystemArenaScope::s_SerializeArena);
    if(!subsystem.validatePayload(scratchArena))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);

    subsystemHashes.reserve(subsystem.subsystems().size());
    for(const ISubsystem::SubsystemRef& child : subsystem.subsystems())
        subsystemHashes.push_back(child.name().hash());

    const usize fieldCount = subsystem.fieldCount();
    assetHashes.reserve(fieldCount);
    for(usize i = 0u; i < fieldCount; ++i)
        assetHashes.push_back(subsystem.fieldValue(i).hash());

    usize reserveBytes = sizeof(SubsystemBinaryPayload::SubsystemHeaderBinary);
    const bool canReserve =
        AddBinaryVectorReserveBytes(reserveBytes, subsystemHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, assetHashes)
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SubsystemBinaryPayload::SubsystemHeaderBinary header;
    header.subsystemTypeHash = subsystem.assetType().hash();
    header.subsystemCount = static_cast<u64>(subsystemHashes.size());
    header.assetCount = static_cast<u64>(assetHashes.size());
    AppendPOD(outBinary, header);

    return
        Core::Assets::AppendVectorPayload(outBinary, subsystemHashes, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_SubsystemsPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, assetHashes, SubsystemBinaryPayload::s_SubsystemSerializeContext, SubsystemBinaryPayload::s_AssetsPayloadLabel)
    ;
}

bool SubsystemAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    const ISubsystem* const subsystem = CastSubsystem(&asset);
    if(!subsystem){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{} failed: invalid asset type '{}'"),
            SubsystemBinaryPayload::s_SubsystemSerializeContext,
            ConvertString(asset.assetType().resolvedText())
        );
        return false;
    }

    return SubsystemAssetSerialization::Serialize(*subsystem, outBinary);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_subsystem_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_SubsystemsField = "subsystems";
static constexpr AStringView s_DiagnosticPrefix = "Subsystem meta";


template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetRef<TAsset>> ParseAssetRefString(
    const Path& nwbFilePath,
    const Value& itemValue,
    const AStringView fieldLabel,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!itemValue.isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' must be a string"), ConvertString(s_DiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
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

template<typename SubsystemT>
[[nodiscard]] static bool ValidateSubsystemMetadataFields(const Path& nwbFilePath, const Value& asset){
    const auto descriptors = TypedSubsystem<SubsystemT>::FieldDescriptors();
    return Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_DiagnosticPrefix, [descriptors](const AStringView fieldName)noexcept{
        if constexpr(SubsystemT::s_SupportsChildSubsystems){
            if(fieldName == s_SubsystemsField)
                return true;
        }
        for(const SubsystemFieldDescriptor<SubsystemT>& descriptor : descriptors){
            if(fieldName == descriptor.fieldName)
                return true;
        }
        return false;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename SubsystemT>
Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_subsystem_cook;

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});

    SubsystemCookEntry<SubsystemT> entry(arena, virtualPath);
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_DiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!ValidateSubsystemMetadataFields<SubsystemT>(nwbFilePath, asset))
        return MakeUnexpected(Failure{});

    if constexpr(SubsystemT::s_SupportsChildSubsystems){
        if(const Core::Metascript::Value* const subsystemsValue = asset.findField(s_SubsystemsField)){
            auto subsystems = ParseAssetRefList<ISubsystem>(nwbFilePath, *subsystemsValue, s_SubsystemsField, arena, scratchArena);
            if(!subsystems)
                return MakeUnexpected(Failure{});
            entry.subsystem.subsystems() = Move(*subsystems);
        }
    }

    const auto descriptors = TypedSubsystem<SubsystemT>::FieldDescriptors();
    for(const SubsystemFieldDescriptor<SubsystemT>& descriptor : descriptors){
        const Core::Metascript::Value* const fieldValue = asset.findField(descriptor.fieldName);
        if(!fieldValue){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{} '{}': required field '{}' is missing for '{}'"),
                ConvertString(s_DiagnosticPrefix),
                PathToString<tchar>(nwbFilePath),
                ConvertString(descriptor.fieldName),
                ConvertString(SubsystemT::s_AssetTypeText)
            );
            return MakeUnexpected(Failure{});
        }
        const auto parsedRef = ParseAssetRefString<Core::Assets::IAsset>(nwbFilePath, *fieldValue, descriptor.fieldName, scratchArena);
        if(!parsedRef)
            return MakeUnexpected(Failure{});
        descriptor.set(entry.subsystem, parsedRef->name());
    }

    if(!entry.subsystem.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});

    return entry;
}

template<typename SubsystemT>
Expected<SubsystemCookEntry<SubsystemT>> ParseSubsystemCookMetadata(
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
    return ParseSubsystemCookMetadata<SubsystemT>(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

template<typename SubsystemT>
Expected<SubsystemT> BuildSubsystemAsset(
    SubsystemCookEntry<SubsystemT>& entry,
    Core::Assets::AssetArena&,
    Core::Alloc::ScratchArena& scratchArena
){
    SubsystemT asset = Move(entry.subsystem);
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}

void CollectCookedSubsystemVirtualPaths(
    const Core::Assets::CookEntryRegistry& entryRegistry,
    Vector<ISubsystem::SubsystemRef, Core::Alloc::ScratchArena>& inOutSubsystems
){
#define NWB_COLLECT_COOKED_SUBSYSTEM_PATHS(Type) \
    if(entryRegistry.has(Type::AssetTypeName())){ \
        for(const SubsystemCookEntry<Type>& entry : entryRegistry.entries<SubsystemCookEntry<Type>>(Type::AssetTypeName())) \
            inOutSubsystems.push_back(ISubsystem::SubsystemRef(entry.virtualPath)); \
    }
    NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(NWB_COLLECT_COOKED_SUBSYSTEM_PATHS)
#undef NWB_COLLECT_COOKED_SUBSYSTEM_PATHS
}


#define NWB_INSTANTIATE_SUBSYSTEM_COOK_FUNCTIONS(Type) \
    template Expected<SubsystemCookEntry<Type>> ParseSubsystemCookMetadata<Type>( \
        Name virtualPath, \
        const Path& nwbFilePath, \
        const Core::Metascript::Value& asset, \
        Core::Assets::AssetArena& arena, \
        Core::Alloc::ScratchArena& scratchArena \
    ); \
    template Expected<SubsystemCookEntry<Type>> ParseSubsystemCookMetadata<Type>( \
        const Path& assetRoot, \
        AStringView virtualRoot, \
        const Path& nwbFilePath, \
        const Core::Metascript::Document& doc, \
        Core::Assets::AssetArena& arena, \
        Core::Alloc::ScratchArena& scratchArena \
    ); \
    template Expected<Type> BuildSubsystemAsset<Type>( \
        SubsystemCookEntry<Type>& entry, \
        Core::Assets::AssetArena& arena, \
        Core::Alloc::ScratchArena& scratchArena \
    );
NWB_SUBSYSTEM_ASSET_TYPE_ENTRIES(NWB_INSTANTIATE_SUBSYSTEM_COOK_FUNCTIONS)
#undef NWB_INSTANTIATE_SUBSYSTEM_COOK_FUNCTIONS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

