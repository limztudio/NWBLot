// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "shader_stage_key.h"

#include <impl/assets_material/cook.h>
#include <impl/assets_shader/cook.h>

#include <core/assets/cook_metadata.h>
#include <core/assets/cook_paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = Core::Assets::CookString;
template<typename T>
using CookVector = Core::Assets::CookVector<T>;
using ResolvedCookPaths = Core::Assets::ResolvedCookPaths;
using ScratchArena = Core::Assets::ScratchArena;
using ScratchString = Core::Assets::ScratchString;
using VirtualPathHashSet = Core::Assets::CookEntryPathHashSet;
using IncludeMetadataMap = ShaderCook::CookMap<CookString, ShaderCook::IncludeEntry>;
using ShaderEntryVector = CookVector<ShaderCook::ShaderEntry>;

inline constexpr AStringView s_DependencyRepoRootAlias = "repo";
inline constexpr AStringView s_DependencyGeneratedCacheRootAlias = "generated_cache";

struct PreparedShaderEntry{
    ShaderCook::ShaderEntry entry;
    Path sourcePath;
    ShaderCook::CookVector<Path> includeDirectories;
    ShaderCook::CookVector<Path> dependencies;
    u64 dependencyChecksum = 0;
    u64 variantCount = 0;
    CookString materialTypedBindingInterfacePath;
    Name materialTypedBindingInterface = s_NameNone;
    bool compilerInputsHaveBom = false;
    bool usesMaterialTypedBinding = false;
    bool supportsCsgClipVariant = false;
    bool supportsAvboitCsgClipVariant = false;


public:
    explicit PreparedShaderEntry(ShaderCook::CookArena& arena)
        : entry(arena)
        , sourcePath(arena)
        , includeDirectories(arena)
        , dependencies(arena)
        , materialTypedBindingInterfacePath(arena)
    {}
};

using PreparedShaderVector = Vector<PreparedShaderEntry, ShaderCook::CookArena>;

struct PreparedShaderPlan{
    PreparedShaderVector preparedEntries;
    u64 plannedFileCount = 0;


public:
    explicit PreparedShaderPlan(ShaderCook::CookArena& arena)noexcept
        : preparedEntries(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<PreparedShaderPlan> PrepareShaderEntriesForCook(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const ResolvedCookPaths& resolvedPaths,
    const Path& materialBindIncludeRoot,
    const Path& csgShapeIncludeRoot,
    const Path& deferredBxdfIncludeRoot,
    const Path& shadowSurfaceIncludeRoot,
    const IncludeMetadataMap& includeMetadata,
    ShaderEntryVector& inOutShaderEntries,
    const ShaderCook::CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

