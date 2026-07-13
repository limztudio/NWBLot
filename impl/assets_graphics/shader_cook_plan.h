
#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_stage_key.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/cook_paths.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_shader/cook.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = Core::Assets::CookString;
template<typename T>
using CookVector = Core::Assets::CookVector<T>;
template<typename T, typename V>
using CookMap = Core::Assets::CookMap<T, V>;
using ResolvedCookPaths = Core::Assets::ResolvedCookPaths;
using ScratchArena = Core::Assets::ScratchArena;
using ScratchString = Core::Assets::ScratchString;
using VirtualPathHashSet = Core::Assets::CookEntryPathHashSet;
using IncludeMetadataMap = CookMap<CookString, ShaderCook::IncludeEntry>;
using ShaderEntryVector = CookVector<ShaderCook::ShaderEntry>;
using PreparedShaderKey = ShaderStageKey;
using PreparedShaderKeyHasher = ShaderStageKeyHasher;

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
    bool supportsAvboitCsgClipVariant = false;

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

    explicit PreparedShaderPlan(ShaderCook::CookArena& arena)
        : preparedEntries(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool PrepareShaderEntriesForCook(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const ResolvedCookPaths& resolvedPaths,
    const Path& materialBindIncludeRoot,
    const Path& csgShapeIncludeRoot,
    const Path& deferredBxdfIncludeRoot,
    const Path& shadowTransmittanceIncludeRoot,
    const IncludeMetadataMap& includeMetadata,
    ShaderEntryVector& inOutShaderEntries,
    const ShaderCook::CookVector<MaterialCookEntry>& materialEntries,
    PreparedShaderPlan& outPreparedPlan,
    ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

