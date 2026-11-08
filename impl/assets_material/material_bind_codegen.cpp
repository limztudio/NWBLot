// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_private.h"

#include <impl/assets_material/bind_source_emitter.adhd.h>
#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ScratchString> BuildMaterialBindIncludeVirtualPathImpl(const MaterialBindEntry& entry, ScratchArena& arena){
    if(entry.virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation failed: virtual path is empty for '{}'"), ConvertString(entry.source));
        return MakeUnexpected(Failure{});
    }

    ScratchString includePath(AStringView(entry.virtualPath), arena);
    includePath += s_MaterialBindSourceExtensionText;
    return includePath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookString> BuildMaterialBindIncludeSource(const MaterialBindEntry& entry, MaterialCookArena& arena, Core::Alloc::ScratchArena&){
    MaterialCookString source(arena);
    if(!MaterialBindSourceEmitter{}.buildMaterialBindIncludeSourceFromData(
        AStringView(entry.source),
        AStringView(entry.virtualPath),
        entry.structs,
        entry.instances,
        source
    ))
        return MakeUnexpected(Failure{});
    return source;
}


Expected<Path> EmitMaterialBindIncludes(const Path& cacheDirectory, const AStringView configurationSafeName, const Span<const MaterialBindEntry> materialBindEntries, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    Path includeRoot = cacheDirectory / configurationSafeName / s_MaterialBindGeneratedIncludeCacheDirectoryText;
    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, s_MaterialBindIncludeGenerationLabel))
        return MakeUnexpected(Failure{});
    if(materialBindEntries.empty())
        return includeRoot;

    ScratchHashSet<ScratchString> seenIncludePaths{0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena};
    seenIncludePaths.reserve(materialBindEntries.size());

    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        auto includePathResult = BuildMaterialBindIncludeVirtualPathImpl(bindEntry, scratchArena);
        if(!includePathResult)
            return MakeUnexpected(Failure{});
        ScratchString includePath = Move(*includePathResult);
        if(!seenIncludePaths.insert(includePath).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: duplicate material bind include path '{}'"), ConvertString(includePath));
            return MakeUnexpected(Failure{});
        }

        auto generatedSourceResult = BuildMaterialBindIncludeSource(bindEntry, arena, scratchArena);
        if(!generatedSourceResult)
            return MakeUnexpected(Failure{});
        const CookString& generatedSource = *generatedSourceResult;

        const ScratchPath outputPath = ScratchPath(scratchArena, includeRoot) / AStringView(includePath);
        auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult.error().message()));
            return MakeUnexpected(Failure{});
        }

        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
            return MakeUnexpected(Failure{});
        }
    }

    return includeRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

