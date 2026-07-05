// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#if defined(NWB_COOK)


#include "cook.h"
#include "metadata.h"
#include "binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/paths.h>
#include <core/filesystem/module.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>
#include <core/metascript/parser.h>
#include <global/hash_utils.h>
#include <global/text_utils.h>
#include <global/math/convert.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook{


using CookArena = MaterialCookArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookString = MaterialCookString;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchString = AString<ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using CookVector = MaterialCookVector<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using ScratchHashSet = HashSet<T, Hasher<T>, EqualTo<T>, ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using CookHashSet = MaterialCookHashSet<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cross-TU helper declarations (definitions de-static'd in their domain .cpp).

bool ResolveMaterialBindDependencyInterface(
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const CookVector<Path>& dependencies,
    CookString& outInterfacePath,
    Name& outInterfaceName,
    bool& outDependsOnMaterialBind,
    ScratchArena& scratchArena
);

bool BuildMaterialBindIncludeSourceImpl(
    CookArena& arena,
    const MaterialBindEntry& entry,
    CookString& outSource,
    ScratchArena& scratchArena
);

bool EmitMaterialBindIncludes(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
);

bool ValidateMaterialCookInterfaces(
    const CookVector<MaterialBindEntry>& materialBindEntries,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);

bool ParseMaterialMeta(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MaterialCookEntry& outEntry,
    ScratchArena& scratchArena
);

bool AssignMaterialShadingModelIdsImpl(
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);

bool EmitDeferredBxdfDispatchModuleImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
);

bool EmitShadowTransmittanceDispatchModuleImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
);

bool EmitMaterialPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView sharedMeshShaderName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
);

bool EmitMaterialAvboitAccumulatePixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
);

bool EmitMaterialAvboitOccupancyPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
);

bool EmitMaterialAvboitExtinctionPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

