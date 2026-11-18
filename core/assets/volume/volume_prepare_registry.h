// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_types.h"
#include "pack_manifest.h"
#include <core/assets/asset_file_discovery.h>
#include <core/common/binary_update.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetVolumePrepareContext{
    Core::Alloc::GlobalArena& arena;
    const ResolvedCookPaths& resolvedPaths;
    CookString& configurationSafeName;
    ParsedAssetMetadata& parsedMetadata;
    u64& plannedFileCount;
    AssetVolumeManifestCookerVector& manifestCookers;
    ScratchArena& scratchArena;
};

struct CookVolumeWriteContext{
    AssetVolumePrepareContext& preparation;
    AssetVolumePackManifest& manifest;
    CookEntryPathHashSet& seenPaths;
    ScratchArena& scratch;

    [[nodiscard]] b8 claim(const Name& path){ return seenPaths.insert(path.hash()).second; }
    [[nodiscard]] b8 appendPayload(const Name& path, const Span<const u8> bytes, const u64 cookKey){
        return AppendPayloadBytesToManifest(path, bytes, manifest, cookKey);
    }
    template<typename Encoder>
    [[nodiscard]] b8 appendEncodedPayload(const Name& path, Encoder&& encode){
        AssetBytes bytes(preparation.arena);
        BinaryUpdateContext context{bytes, scratch};
        return encode(context) && appendPayload(path, bytes, 0u);
    }
};

template<typename T, typename Sequence>
[[nodiscard]] inline T& AppendCookRecord(CookVolumeWriteContext& context, Sequence& records){
    return records.emplace_back(context.preparation.arena);
}

template<typename T>
[[nodiscard]] inline T& GetCookMetadata(const Name& name, ParsedAssetMetadata& parsed){
    return RequireParsedMetadataExtension<T>(name, parsed, parsed.arena);
}

template<typename T>
[[nodiscard]] inline b8 HasCookMetadata(const Name& name, const ParsedAssetMetadata& parsed){
    return FindParsedMetadataExtension<T>(name, parsed) != nullptr;
}

[[nodiscard]] inline usize CookBucketSize(const ParsedAssetMetadata& parsed, const Name& type){
    const auto* bucket = parsed.entryRegistry.find(type);
    return bucket ? bucket->size() : 0u;
}

template<typename T, typename Visitor>
[[nodiscard]] inline b8 VisitCookEntries(ParsedAssetMetadata& parsed, const Name& type, Visitor&& visitor){
    return visitor(Span<T>(parsed.entryRegistry.entries<T>(type)));
}

template<typename Visitor>
[[nodiscard]] inline b8 VisitCookFiles(AssetVolumePrepareContext& context, const AStringView extension, Visitor&& visitor){
    const auto discovered = AssetFileDiscovery::Discover(context.resolvedPaths.assetRoots, extension, context.arena, context.scratchArena);
    if(!discovered)
        return false;
    for(const auto& file : *discovered){
        if(!visitor(file))
            return false;
    }
    return true;
}

template<typename Writer, typename Metadata>
inline void AddCookManifestWriter(AssetVolumePrepareContext& context, const Name& extension, const Metadata&){
    context.manifestCookers.emplace_back([&context, extension](AssetVolumePackManifest& manifest, CookEntryPathHashSet& seen, ScratchArena& scratch){
        CookVolumeWriteContext output{context, manifest, seen, scratch};
        Writer writer(context.arena);
        return writer.write(GetCookMetadata<Metadata>(extension, context.parsedMetadata), output);
    });
}

using AssetVolumePrepareFunction = b8 (*)(AssetVolumePrepareContext& context);

class AssetVolumePrepareAutoRegistrar final{
public:
    explicit AssetVolumePrepareAutoRegistrar(AssetVolumePrepareFunction function);
};

[[nodiscard]] b8 RegisterAutoCollectedAssetVolumePreparers(AssetVolumePrepareContext& context);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

