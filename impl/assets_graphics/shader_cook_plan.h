// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_graphics/components.adhd.h>
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
    b8 compilerInputsHaveBom = false;
    b8 usesMaterialTypedBinding = false;
    b8 supportsCsgClipVariant = false;
    b8 supportsAvboitCsgClipVariant = false;


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
    const ResolvedCookPaths& resolvedPaths,
    const Path& materialBindIncludeRoot,
    const Path& csgShapeIncludeRoot,
    const Path& deferredBxdfIncludeRoot,
    const Path& shadowSurfaceIncludeRoot,
    const IncludeMetadataMap& includeMetadata,
    Span<const MaterialBindEntry> materialBindEntries,
    Span<const MaterialCookEntry> materialEntries,
    ShaderCook::CookArena& cookArena,
    ScratchArena& scratchArena,
    ShaderCook& shaderCook,
    Span<ShaderCook::ShaderEntry> inOutShaderEntries
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

