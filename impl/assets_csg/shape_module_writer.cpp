// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shape_module_writer.h"

#include <impl/assets_csg/shape_module_source.adhd.h>

#include <core/assets/cook_paths.h>
#include <core/common/log.h>

#include <global/allocation_size.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsCsgCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Core::Assets::ScratchString;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Path> CsgShapeModuleWriter::emit(const Path& cacheDirectory, const AStringView configurationSafeName, const Span<const CsgShapeCookEntry> csgShapeEntries, ScratchArena& scratchArena)const{
    Path includeRoot = cacheDirectory / configurationSafeName / s_GeneratedIncludeCacheDirectory;
    if(csgShapeEntries.empty()){
        if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, s_CsgShapeIncludeGenerationLabel))
            return MakeUnexpected(Failure{});
        if(!WriteEmptyDefaultModuleInclude(includeRoot, scratchArena))
            return MakeUnexpected(Failure{});
        return includeRoot;
    }

    HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>> seenShapeNames(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    seenShapeNames.reserve(csgShapeEntries.size());
    for(const CsgShapeCookEntry& entry : csgShapeEntries){
        if(!seenShapeNames.insert(entry.shapeName.hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: duplicate shape '{}'"), ConvertString(entry.shapeName.resolvedText()));
            return MakeUnexpected(Failure{});
        }
    }

    HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>> validatedModules(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    validatedModules.reserve(csgShapeEntries.size());

    using GeneratedIncludeOwnerMap = HashMap<ScratchString, Name, ScratchArena, Hasher<ScratchString>, EqualTo<ScratchString>>;
    GeneratedIncludeOwnerMap generatedIncludeOwners(0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena);
    generatedIncludeOwners.reserve(csgShapeEntries.size());

    usize moduleSourceCapacity = 0u;

    // Preflight every output path before clearing or writing the generated include tree.
    // A later module must never be able to overwrite a generated include emitted for an earlier, unrelated module.
    for(const CsgShapeCookEntry& moduleEntry : csgShapeEntries){
        if(!validatedModules.insert(moduleEntry.shaderModule.hash()).second)
            continue;

        usize sourceBytes = s_CsgModuleSourceHeader.size();
        for(const CsgShapeCookEntry& entry : csgShapeEntries){
            if(entry.shaderModule != moduleEntry.shaderModule)
                continue;

            sourceBytes = AddSize(sourceBytes, AddSize(s_FixedShapeSourceBytes, entry.evalInclude.size()));
            if(entry.moduleInclude == moduleEntry.moduleInclude)
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: module '{}' uses multiple generated include paths ('{}' and '{}')")
                , ConvertString(moduleEntry.shaderModule.resolvedText())
                , ConvertString(moduleEntry.moduleInclude)
                , ConvertString(entry.moduleInclude)
            );
            return MakeUnexpected(Failure{});
        }

        moduleSourceCapacity = Max(moduleSourceCapacity, sourceBytes);

        ScratchString canonicalModuleInclude(moduleEntry.moduleInclude, scratchArena);
        CanonicalizeTextInPlace(canonicalModuleInclude);
        const auto generatedIncludeOwner = generatedIncludeOwners.try_emplace(Move(canonicalModuleInclude), moduleEntry.shaderModule);
        if(!generatedIncludeOwner.second && generatedIncludeOwner.first.value() != moduleEntry.shaderModule){
            NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: generated include '{}' is shared by modules '{}' and '{}'")
                , ConvertString(moduleEntry.moduleInclude)
                , ConvertString(generatedIncludeOwner.first.value().resolvedText())
                , ConvertString(moduleEntry.shaderModule.resolvedText())
            );
            return MakeUnexpected(Failure{});
        }
    }

    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, s_CsgShapeIncludeGenerationLabel))
        return MakeUnexpected(Failure{});

    HashSet<NameHash, ScratchArena, Hasher<NameHash>, EqualTo<NameHash>> emittedModules(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    emittedModules.reserve(csgShapeEntries.size());

    CookString source(csgShapeEntries[0u].evalInclude.get_allocator().arena());
    source.reserve(moduleSourceCapacity);
    for(const CsgShapeCookEntry& moduleEntry : csgShapeEntries){
        if(!emittedModules.insert(moduleEntry.shaderModule.hash()).second)
            continue;

        if(!WriteModuleInclude(csgShapeEntries, moduleEntry.shaderModule, moduleEntry.moduleInclude, includeRoot, scratchArena, source))
            return MakeUnexpected(Failure{});
    }

    return includeRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgShapeModuleWriter::WriteModuleInclude(const Span<const CsgShapeCookEntry> csgShapeEntries, const Name shaderModule, const AStringView moduleInclude, const Path& includeRoot, ScratchArena& scratchArena, CookString& source){
    CsgShapeModuleSource().begin(source);

    for(const CsgShapeCookEntry& entry : csgShapeEntries){
        if(entry.shaderModule != shaderModule)
            continue;
        CsgShapeModuleSource().appendEval(entry.shapeTypeId, AStringView(entry.evalInclude), source);
    }

    const ::Path<ScratchArena> outputPath = ::Path<ScratchArena>(scratchArena, includeRoot) / moduleInclude;
    auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult.error().message()));
        return false;
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}

b8 CsgShapeModuleWriter::WriteEmptyDefaultModuleInclude(const Path& includeRoot, ScratchArena& scratchArena){
    const ::Path<ScratchArena> outputPath = ::Path<ScratchArena>(scratchArena, includeRoot) / s_DefaultGeneratedIncludeDirectory / s_CsgBuiltInModuleIncludeFileName;

    auto ensureDirectoriesResult2 = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult2.error().message()));
        return false;
    }

    if(!WriteTextFile(outputPath, s_CsgEmptyGeneratedModule)){
        NWB_LOGGER_ERROR(NWB_TEXT("CSG shape include generation: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

