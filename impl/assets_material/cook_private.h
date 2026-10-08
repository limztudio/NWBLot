// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "metadata.h"
#include "binary_payload.h"

#include <core/alloc/scratch.h>
#include <core/assets/cook_paths.h>
#include <core/assets/paths.h>
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


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CookArena = MaterialCookArena;
using CookString = MaterialCookString;
using ScratchArena = Core::Alloc::ScratchArena;
using ScratchString = AString<ScratchArena>;

template<typename T>
using CookVector = MaterialCookVector<T>;

template<typename T>
using ScratchHashSet = HashSet<T, ScratchArena, Hasher<T>, EqualTo<T>>;

template<typename T>
using CookHashSet = MaterialCookHashSet<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
void AppendMaterialBindGeneratedUpperIdentifier(const AStringView text, StringT& inOutText){
    const usize beginSize = inOutText.size();
    for(const char ch : text)
        inOutText += IsAsciiAlphaNumeric(ch) ? ToAsciiUpper(ch) : '_';
    if(inOutText.size() == beginSize)
        inOutText += "VALUE";
}

template<typename StringT>
void AppendMaterialBindGeneratedPascalIdentifier(const AStringView text, StringT& inOutText){
    const usize beginSize = inOutText.size();
    bool upperNext = true;
    for(const char ch : text){
        if(ch == '_'){
            upperNext = true;
            continue;
        }

        if(upperNext)
            inOutText += ToAsciiUpper(ch);
        else
            inOutText += ch;
        upperNext = false;
    }
    if(inOutText.size() == beginSize)
        inOutText += "Value";
}

template<typename ArenaT>
[[nodiscard]] AString<ArenaT> BuildMaterialBindGeneratedSymbol(
    ArenaT& arena,
    const InitializerList<AStringView> nameSegments,
    const AStringView suffix
){
    AString<ArenaT> symbol("NWB_MATERIAL_BIND_", arena);
    bool firstSegment = true;
    for(const AStringView nameSegment : nameSegments){
        if(!firstSegment)
            symbol += '_';
        AppendMaterialBindGeneratedUpperIdentifier(nameSegment, symbol);
        firstSegment = false;
    }
    symbol += suffix;
    return symbol;
}

template<typename ArenaT>
[[nodiscard]] AString<ArenaT> BuildMaterialBindAccessorName(
    ArenaT& arena,
    const InitializerList<AStringView> nameSegments
){
    AString<ArenaT> functionName("nwbMaterialBindLoad", arena);
    for(const AStringView nameSegment : nameSegments)
        AppendMaterialBindGeneratedPascalIdentifier(nameSegment, functionName);
    return functionName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindDependency> ResolveMaterialBindDependencyInterface(
    CookArena& arena,
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const CookVector<Path>& dependencies,
    ScratchArena& scratchArena
);

Expected<CookString> BuildMaterialBindIncludeSourceImpl(
    CookArena& arena,
    const MaterialBindEntry& entry,
    ScratchArena& scratchArena
);

Expected<Path> EmitMaterialBindIncludes(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialBindEntry>& materialBindEntries,
    ScratchArena& scratchArena
);

bool ValidateMaterialCookInterfaces(
    const CookVector<MaterialBindEntry>& materialBindEntries,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);

Expected<MaterialCookEntry> ParseMaterialMeta(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    ScratchArena& scratchArena
);

bool AssignMaterialShadingModelIdsImpl(
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);

Expected<Path> EmitDeferredBxdfDispatchModuleImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);

Expected<Path> EmitShadowSurfaceDispatchModuleImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialBindEntry>& materialBindEntries,
    const CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);

Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView sharedMeshShaderName,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);

Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitAccumulatePixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);

Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitOccupancyPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);

Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitExtinctionPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

