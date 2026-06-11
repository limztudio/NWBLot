// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_stage_key.h"

#include <impl/assets_volume/cook_types.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_shader/cook.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = AssetsVolumeCookDetail::CookString;
template<typename T>
using CookVector = AssetsVolumeCookDetail::CookVector<T>;
template<typename T, typename V>
using CookMap = AssetsVolumeCookDetail::CookMap<T, V>;
using ResolvedCookPaths = AssetsVolumeCookDetail::ResolvedCookPaths;
using ScratchArena = AssetsVolumeCookDetail::ScratchArena;
using VirtualPathHashSet = AssetsVolumeCookDetail::VirtualPathHashSet;
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

