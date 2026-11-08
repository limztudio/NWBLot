// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_private.h"



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AssignMaterialShadingModelIds(const Span<MaterialCookEntry> materialEntries, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.bxdfSource.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' is missing a deferred bxdf"), ConvertString(AStringView(entry.virtualPath)));
            return false;
        }
    }

    Vector<AStringView, ScratchArena> uniqueSources(scratchArena);
    uniqueSources.reserve(materialEntries.size());
    for(const MaterialCookEntry& entry : materialEntries)
        uniqueSources.push_back(AStringView(entry.bxdfSource));
    Sort(uniqueSources.begin(), uniqueSources.end());

    uniqueSources.erase(Unique(uniqueSources.begin(), uniqueSources.end()), uniqueSources.end());
    if(uniqueSources.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: too many unique deferred bxdfs"));
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        const AStringView source(entry.bxdfSource);
        const auto sourceIt = LowerBound(uniqueSources.begin(), uniqueSources.end(), source);
        NWB_ASSERT(sourceIt != uniqueSources.end() && *sourceIt == source);
        entry.shadingModelId = static_cast<u32>(static_cast<usize>(sourceIt - uniqueSources.begin()));
    }

    // Opaque with explicit `shaders` takes the no-surface sentinel (never 0), so GI hits neutral, never a hook.
    Vector<AStringView, ScratchArena> uniqueSurfaces(scratchArena);
    uniqueSurfaces.reserve(materialEntries.size());
    for(const MaterialCookEntry& entry : materialEntries){
        if(!entry.surfaceSource.empty())
            uniqueSurfaces.push_back(AStringView(entry.surfaceSource));
    }
    Sort(uniqueSurfaces.begin(), uniqueSurfaces.end());

    uniqueSurfaces.erase(Unique(uniqueSurfaces.begin(), uniqueSurfaces.end()), uniqueSurfaces.end());
    if(uniqueSurfaces.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: too many unique material surfaces"));
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty()){
            entry.surfaceDispatchId = s_NoSurfaceDispatchId;
            continue;
        }

        const AStringView surface(entry.surfaceSource);
        const auto surfaceIt = LowerBound(uniqueSurfaces.begin(), uniqueSurfaces.end(), surface);
        NWB_ASSERT(surfaceIt != uniqueSurfaces.end() && *surfaceIt == surface);
        entry.surfaceDispatchId = static_cast<u32>(static_cast<usize>(surfaceIt - uniqueSurfaces.begin()));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Path> EmitDeferredBxdfDispatchModule(const Path& cacheDirectory, const AStringView configurationSafeName, const Span<const MaterialCookEntry> materialEntries, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    Path includeRoot = cacheDirectory / configurationSafeName / s_DeferredBxdfIncludeSubDir;
    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, "Deferred bxdf dispatch"))
        return MakeUnexpected(Failure{});

    // Each assigned BXDF id maps to one source; materials sharing that source share the slot.
    u32 maxId = 0u;
    b8 anyBxdf = false;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.bxdfSource.empty())
            continue;
        anyBxdf = true;
        if(entry.shadingModelId > maxId)
            maxId = entry.shadingModelId;
    }

    Vector<AStringView, ScratchArena> sourceById(scratchArena);
    if(anyBxdf)
        sourceById.resize(static_cast<usize>(maxId) + 1u);
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.bxdfSource.empty())
            continue;

        const AStringView source(entry.bxdfSource);
        AStringView& slot = sourceById[entry.shadingModelId];
        if(!slot.empty() && slot != source){
            NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: shading model id {} maps to multiple bxdf sources"), entry.shadingModelId);
            return MakeUnexpected(Failure{});
        }
        slot = source;
    }

    const CookString source = BuildDeferredBxdfDispatchModuleSource(sourceById);

    const ScratchPath outputPath = ScratchPath(scratchArena, includeRoot) / s_DeferredBxdfModuleSubPath;
    auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
        return MakeUnexpected(Failure{});
    }
    return includeRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

