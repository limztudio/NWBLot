// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/volume/asset_volume_writer.h>
#include <core/assets/volume/built_asset.h>
#include <core/assets/volume/cooked_object_cache.h>
#include <core/assets/volume/volume_prepare_registry.h>
#include <core/assets/gather_merge_registry.h>
#include <core/common/log.h>
#include <pipeline/cook_workspace.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GatherMergeCallback final{
public:
    GatherMergeCallback() = default;
    GatherMergeCallback(Core::Assets::AssetGatherMergeFunction fn)noexcept : m_function(fn){}

    [[nodiscard]] b8 apply(const Name path, const Span<const u8> bytes, AssetCookWorkspace& workspace, Core::Assets::AssetBytes& existing)const{
        return m_function && m_function(path, bytes, workspace.scratch, existing);
    }

private:
    Core::Assets::AssetGatherMergeFunction m_function = nullptr;
};

[[nodiscard]] inline GatherMergeCallback RegisteredGatherMerge(AssetCookWorkspace& workspace){
    return Core::Assets::QueryAutoCollectedAssetGatherMerge(workspace.scratch);
}

class CookVolumeManifest final : NoCopy{
public:
    explicit CookVolumeManifest(AssetCookWorkspace& workspace)
        : m_workspace(workspace), manifest(workspace.arena), m_source(workspace.arena), m_builtEntries(workspace.arena){}

    [[nodiscard]] b8 reserve(const u64 count){ return Core::Assets::AssetsVolumeCookDetail::ReserveAssetVolumePackManifest(count, manifest); }
    [[nodiscard]] b8 append(const Name path, const Span<const u8> bytes){ return Core::Assets::AssetsVolumeCookDetail::AppendPayloadBytesToManifest(path, bytes, manifest); }
    template<typename A>
    [[nodiscard]] Expected<usize> readBuiltAsset(const ::Path<A>& file){
        const auto payloads = Core::Assets::BuiltAssetDetail::ReadBuiltAsset(Path(m_workspace.arena, file), m_source, m_workspace.scratch);
        if(!payloads)
            return MakeUnexpected(payloads.error());
        m_builtEntries.assign(payloads->begin(), payloads->end());
        return m_builtEntries.size();
    }
    [[nodiscard]] Core::Assets::BuiltAssetDetail::BuiltAssetPayload builtEntry(const usize index)const noexcept{
        return m_builtEntries[index];
    }
    [[nodiscard]] Span<const u8> sourceBytes(const usize offset, const usize size)const{
        return Span<const u8>(m_source).subspan(offset, size);
    }
    template<typename A>
    [[nodiscard]] Expected<Core::Assets::AssetsVolumeCookDetail::AssetVolumeWriteResult> writeVolume(const ::Path<A>& output, const AStringView configuration)const{
        Core::Assets::ResolvedCookPaths paths(m_workspace.arena);
        paths.outputDirectory = Path(m_workspace.arena, output);
        return Core::Assets::AssetsVolumeCookDetail::WriteAssetVolume(paths, configuration, manifest, m_workspace.arena, m_workspace.scratch);
    }
    [[nodiscard]] b8 writeBuiltAssets(const Path& output)const{
        return Core::Assets::BuiltAssetDetail::WriteBuiltAssets(output, manifest, m_workspace.scratch);
    }

private:
    AssetCookWorkspace& m_workspace;
public:
    Core::Assets::AssetsVolumeCookDetail::AssetVolumePackManifest manifest;
private:
    Core::Assets::AssetBytes m_source;
    Core::Assets::AssetVector<Core::Assets::BuiltAssetDetail::BuiltAssetPayload> m_builtEntries;
};

class CookVolumePreparation final : NoCopy{
public:
    CookVolumePreparation(AssetCookWorkspace& workspace, const CookFileCatalog& catalog)
        : m_workspace(workspace), m_catalog(catalog), m_parsed(workspace.arena), m_configuration(workspace.arena), m_writers(workspace.arena), m_seen(workspace.scratch),
          m_context{workspace.arena, catalog.paths, m_configuration, m_parsed, plannedFileCount, m_writers, workspace.scratch}{}

    [[nodiscard]] b8 registerEntryTypes(){ return Core::Assets::RegisterAutoCollectedCookEntryTypes(m_parsed.entryRegistry, m_workspace.scratch); }
    [[nodiscard]] b8 parse(Core::CpuTaskScheduler& scheduler){
        return Core::Assets::ParseAssetMetadata(m_catalog.files, m_catalog.paths.cacheDirectory, m_workspace.arena, scheduler, m_workspace.scratch, m_parsed);
    }
    void setConfiguration(const AStringView value){ m_configuration.assign(value); }
    [[nodiscard]] b8 registerPreparers(){ return Core::Assets::AssetsVolumeCookDetail::RegisterAutoCollectedAssetVolumePreparers(m_context); }
    [[nodiscard]] usize entryCount()const{ return m_parsed.entryRegistry.entryCount(); }
    [[nodiscard]] u64 cachedEntryCount()const noexcept{
        u64 count = 0u;
        for(const Core::Assets::NwbFileAssetCacheRecord& record : m_parsed.fileCacheRecords){
            if(record.loadedFromCache)
                count += static_cast<u64>(record.cachedVirtualPaths.size());
        }
        return count;
    }
    void reservePaths(const usize count){ m_seen.reserve(count); }
    [[nodiscard]] b8 seedCachedPaths(){
        for(const Core::Assets::NwbFileAssetCacheRecord& record : m_parsed.fileCacheRecords){
            if(!record.loadedFromCache)
                continue;
            for(const Name& virtualPath : record.cachedVirtualPaths){
                if(!m_seen.insert(virtualPath.hash()).second){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetCook: duplicate cached virtual path '{}'"), ConvertString(virtualPath.resolvedText()));
                    return false;
                }
            }
        }
        return true;
    }
    [[nodiscard]] usize writerCount()const{ return m_writers.size(); }
    [[nodiscard]] b8 writeManifest(const usize index, CookVolumeManifest& volume){
        return m_writers[index](volume.manifest, m_seen, m_workspace.scratch);
    }
    [[nodiscard]] b8 writeRegistryObjects(Core::CpuTaskScheduler& scheduler, CookVolumeManifest& volume){
        return Core::Assets::AssetsVolumeCookDetail::BuildRegistryObjectManifestEntries(m_configuration, m_workspace.arena, scheduler, m_workspace.scratch, m_parsed, volume.manifest, m_seen);
    }
    [[nodiscard]] b8 writeBuiltAssets(const Path& output, const CookVolumeManifest& volume)const{
        return Core::Assets::BuiltAssetDetail::WriteBuiltAssets(output, m_parsed.fileCacheRecords, volume.manifest, m_workspace.scratch);
    }

    u64 plannedFileCount = 0u;

private:
    AssetCookWorkspace& m_workspace;
    const CookFileCatalog& m_catalog;
    Core::Assets::ParsedAssetMetadata m_parsed;
    Core::Assets::CookString m_configuration;
    Core::Assets::AssetsVolumeCookDetail::AssetVolumeManifestCookerVector m_writers;
    Core::Assets::CookEntryPathHashSet m_seen;
    Core::Assets::AssetsVolumeCookDetail::AssetVolumePrepareContext m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

