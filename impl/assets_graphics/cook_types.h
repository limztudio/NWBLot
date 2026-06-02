// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_stage_key.h"

#include <impl/assets_mesh/cook.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_shader/cook.h>

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_IncludeAssetTypeName("include");

using CookString = ShaderCook::CookString;
template<typename T>
using CookVector = ShaderCook::CookVector<T>;
using ScratchArena = Core::Alloc::ScratchArena;
using ScratchString = AString<ScratchArena>;
using IncludeMetadataMap = ShaderCook::CookMap<CookString, ShaderCook::IncludeEntry>;
using ShaderEntryVector = CookVector<ShaderCook::ShaderEntry>;
using VirtualPathHashSet = ShaderCook::CookHashSet<NameHash>;
using PreparedShaderKey = ShaderStageKey;
using PreparedShaderKeyHasher = ShaderStageKeyHasher;
using MeshCookEntryVector = CookVector<MeshCookEntry>;
using SkinnedMeshCookEntryVector = CookVector<SkinnedMeshCookEntry>;
using MaterialCookEntryVector = CookVector<MaterialCookEntry>;
using MaterialBindEntryVector = CookVector<MaterialBindEntry>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolvedCookPaths{
    Path repoRoot;
    ShaderCook::CookVector<Path> assetRoots;
    Path outputDirectory;
    Path cacheDirectory;

    explicit ResolvedCookPaths(ShaderCook::CookArena& arena)
        : assetRoots(arena)
    {}
};

struct DiscoveredNwbFile{
    Path assetRoot;
    Path filePath;
    CookString normalizedPathText;
    ACompactString virtualRoot;

    DiscoveredNwbFile(ShaderCook::CookArena& arena, const Path& inAssetRoot, const Path& inFilePath, AStringView inNormalizedPathText, ACompactString inVirtualRoot)
        : assetRoot(inAssetRoot)
        , filePath(inFilePath)
        , normalizedPathText(inNormalizedPathText, arena)
        , virtualRoot(inVirtualRoot)
    {}
};

using DiscoveredNwbFileVector = CookVector<DiscoveredNwbFile>;
using DiscoveredBindFileVector = CookVector<DiscoveredNwbFile>;

struct PreparedShaderEntry{
    ShaderCook::ShaderEntry entry;
    Path sourcePath;
    ShaderCook::CookVector<Path> includeDirectories;
    ShaderCook::CookVector<Path> dependencies;
    u64 dependencyChecksum = 0;
    u64 variantCount = 0;
    CookString materialTypedBindingInterfacePath;
    Name materialTypedBindingInterface = NAME_NONE;
    bool usesMaterialTypedBinding = false;
    bool supportsCsgClipVariant = false;

    explicit PreparedShaderEntry(ShaderCook::CookArena& arena)
        : entry(arena)
        , includeDirectories(arena)
        , dependencies(arena)
        , materialTypedBindingInterfacePath(arena)
    {}
};

using PreparedShaderVector = Vector<PreparedShaderEntry, ShaderCook::CookArena>;

struct ParsedAssetMetadata{
    IncludeMetadataMap includeMetadata;
    ShaderEntryVector shaderEntries;
    MaterialBindEntryVector materialBindEntries;
    MeshCookEntryVector meshEntries;
    SkinnedMeshCookEntryVector skinnedMeshEntries;
    MaterialCookEntryVector materialEntries;

    explicit ParsedAssetMetadata(ShaderCook::CookArena& arena)
        : includeMetadata(arena)
        , shaderEntries(arena)
        , materialBindEntries(arena)
        , meshEntries(arena)
        , skinnedMeshEntries(arena)
        , materialEntries(arena)
    {}
};

struct PreparedShaderPlan{
    PreparedShaderVector preparedEntries;
    u64 plannedFileCount = 1; // shader archive index

    explicit PreparedShaderPlan(ShaderCook::CookArena& arena)
        : preparedEntries(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

