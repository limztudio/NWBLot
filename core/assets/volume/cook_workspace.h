// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "asset_volume_writer.h"
#include "built_asset.h"
#include "cooked_object_cache.h"
#include "volume_prepare_registry.h"
#include <core/assets/gather_merge_registry.h>
#include <core/assets/cook_workspace.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GatherMergeCallback final{
public:
    GatherMergeCallback() = default;
    GatherMergeCallback(AssetGatherMergeFunction fn)noexcept : m_function(fn){}

    [[nodiscard]] b8 apply(const Name path, const Span<const u8> bytes, AssetCookWorkspace& workspace, AssetBytes& existing)const{
        return m_function && m_function(path, bytes, workspace.scratch, existing);
    }

private:
    AssetGatherMergeFunction m_function = nullptr;
};

[[nodiscard]] inline GatherMergeCallback RegisteredGatherMerge(AssetCookWorkspace& workspace){
    return QueryAutoCollectedAssetGatherMerge(workspace.scratch);
}

class CookVolumeManifest final : NoCopy{
public:
    explicit CookVolumeManifest(AssetCookWorkspace& workspace)
        : m_workspace(workspace), manifest(workspace.arena), m_source(workspace.arena){}

    [[nodiscard]] b8 reserve(const u64 count){ return AssetsVolumeCookDetail::ReserveAssetVolumePackManifest(count, manifest); }
    [[nodiscard]] b8 append(const Name path, const Span<const u8> bytes){ return AssetsVolumeCookDetail::AppendPayloadBytesToManifest(path, bytes, manifest); }
    template<typename A>
    [[nodiscard]] Expected<BuiltAssetDetail::BuiltAssetPayload> readBuiltAsset(const ::Path<A>& file){
        return BuiltAssetDetail::ReadBuiltAsset(Path(m_workspace.arena, file), m_source);
    }
    [[nodiscard]] Span<const u8> sourceBytes(const usize offset)const{ return Span<const u8>(m_source).subspan(offset); }
    template<typename A>
    [[nodiscard]] Expected<AssetsVolumeCookDetail::AssetVolumeWriteResult> writeVolume(const ::Path<A>& output, const AStringView configuration)const{
        ResolvedCookPaths paths(m_workspace.arena);
        paths.outputDirectory = Path(m_workspace.arena, output);
        return AssetsVolumeCookDetail::WriteAssetVolume(paths, configuration, manifest, m_workspace.arena, m_workspace.scratch);
    }
    [[nodiscard]] b8 writeBuiltAssets(const Path& output)const{
        return BuiltAssetDetail::WriteBuiltAssets(output, manifest, m_workspace.scratch);
    }

private:
    AssetCookWorkspace& m_workspace;
public:
    AssetsVolumeCookDetail::AssetVolumePackManifest manifest;
private:
    AssetBytes m_source;
};

class CookVolumePreparation final : NoCopy{
public:
    CookVolumePreparation(AssetCookWorkspace& workspace, const CookFileCatalog& catalog)
        : m_workspace(workspace), m_catalog(catalog), m_parsed(workspace.arena), m_configuration(workspace.arena), m_writers(workspace.arena), m_seen(workspace.scratch),
          m_context{workspace.arena, catalog.paths, m_configuration, m_parsed, plannedFileCount, m_writers, workspace.scratch}{}

    [[nodiscard]] b8 registerEntryTypes(){ return RegisterAutoCollectedCookEntryTypes(m_parsed.entryRegistry, m_workspace.scratch); }
    [[nodiscard]] b8 parse(Core::CpuTaskScheduler& scheduler){
        return ParseAssetMetadata(m_catalog.files, m_catalog.paths.repoRoot, m_catalog.paths.cacheDirectory, m_workspace.arena, scheduler, m_workspace.scratch, m_parsed);
    }
    void setConfiguration(const AStringView value){ m_configuration.assign(value); }
    [[nodiscard]] b8 registerPreparers(){ return AssetsVolumeCookDetail::RegisterAutoCollectedAssetVolumePreparers(m_context); }
    [[nodiscard]] usize entryCount()const{ return m_parsed.entryRegistry.entryCount(); }
    void reservePaths(const usize count){ m_seen.reserve(count); }
    [[nodiscard]] usize writerCount()const{ return m_writers.size(); }
    [[nodiscard]] b8 writeManifest(const usize index, CookVolumeManifest& volume){
        return m_writers[index](volume.manifest, m_seen, m_workspace.scratch);
    }
    [[nodiscard]] b8 writeRegistryObjects(Core::CpuTaskScheduler& scheduler, CookVolumeManifest& volume){
        return AssetsVolumeCookDetail::BuildRegistryObjectManifestEntries(m_configuration, m_workspace.arena, scheduler, m_workspace.scratch, m_parsed, volume.manifest, m_seen);
    }

    u64 plannedFileCount = 0u;

private:
    AssetCookWorkspace& m_workspace;
    const CookFileCatalog& m_catalog;
    ParsedAssetMetadata m_parsed;
    CookString m_configuration;
    AssetsVolumeCookDetail::AssetVolumeManifestCookerVector m_writers;
    CookEntryPathHashSet m_seen;
    AssetsVolumeCookDetail::AssetVolumePrepareContext m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

