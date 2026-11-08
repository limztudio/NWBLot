// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_shadow_dispatch.h"
#include "cook_private.h"

#include <impl/assets_material/shadow_surface_source_builder.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ShadowSurfaceBindNamespaceLookup = HashMap<Name, u32, ScratchArena, Hasher<Name>, EqualTo<Name>>;

using ShadowSurfaceBindAliasVector = CookVector<CookString>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 AppendShadowSurfaceBindAliases(
    const MaterialBindEntry& bindEntry,
    const AStringView bindNamespace,
    ShadowSurfaceBindAliasVector& outAliasSymbols,
    CookHashSet<CookString>& inOutSeenSymbols,
    CookString& inOutSource
){
    return ShadowSurfaceSourceBuilder().appendBindAliases(
        AStringView(bindEntry.virtualPath),
        bindEntry.structs,
        bindEntry.instances,
        bindNamespace,
        outAliasSymbols,
        inOutSeenSymbols,
        inOutSource
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Path> ShadowSurfaceDispatchGenerator::emit(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const Span<const MaterialBindEntry> materialBindEntries,
    const Span<const MaterialCookEntry> materialEntries,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace MaterialCookDetail;
    Path includeRoot = cacheDirectory / configurationSafeName / s_ShadowSurfaceIncludeSubDir;
    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, "Shadow surface dispatch"))
        return MakeUnexpected(Failure{});

    // Each surface id maps to one source and .bind interface; shared surfaces require the same interface because their hooks use fixed accessor names.
    u32 maxId = 0u;
    b8 anySurface = false;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty())
            continue;
        anySurface = true;
        if(entry.surfaceDispatchId > maxId)
            maxId = entry.surfaceDispatchId;
    }

    Vector<AStringView, ScratchArena> surfaceById(scratchArena);
    Vector<AStringView, ScratchArena> interfaceById(scratchArena);
    if(anySurface){
        surfaceById.resize(static_cast<usize>(maxId) + 1u);
        interfaceById.resize(static_cast<usize>(maxId) + 1u);
    }
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty())
            continue;

        const AStringView surface(entry.surfaceSource);
        AStringView& surfaceSlot = surfaceById[entry.surfaceDispatchId];
        if(!surfaceSlot.empty() && surfaceSlot != surface){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: model id {} maps to multiple surface sources"), entry.surfaceDispatchId);
            return MakeUnexpected(Failure{});
        }
        surfaceSlot = surface;

        const AStringView interfaceName(entry.materialInterface);
        AStringView& interfaceSlot = interfaceById[entry.surfaceDispatchId];
        if(!interfaceSlot.empty() && interfaceSlot != interfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: model id {} maps to multiple material interfaces"), entry.surfaceDispatchId);
            return MakeUnexpected(Failure{});
        }
        interfaceSlot = interfaceName;
    }

    const auto bindEntryLookup = BuildMaterialBindInterfaceLookup(materialBindEntries, scratchArena);
    if(!bindEntryLookup)
        return MakeUnexpected(Failure{});

    Vector<const MaterialBindEntry*, ScratchArena> bindEntryById(scratchArena);
    Vector<u32, ScratchArena> bindNamespaceIdById(scratchArena);
    if(anySurface){
        bindEntryById.resize(surfaceById.size());
        bindNamespaceIdById.resize(surfaceById.size());
    }

    ShadowSurfaceBindNamespaceLookup bindNamespaceLookup(0, Hasher<Name>(), EqualTo<Name>(), scratchArena);
    bindNamespaceLookup.reserve(materialBindEntries.size());
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        const auto interfaceNameResult = Name::FromText(interfaceById[id]);
        if(!interfaceNameResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: model id {} references unknown material interface '{}'"), id, ConvertString(interfaceById[id]));
            return MakeUnexpected(Failure{});
        }
        const Name interfaceName = *interfaceNameResult;
        const auto bindEntryIt = bindEntryLookup->find(interfaceName);
        if(bindEntryIt == bindEntryLookup->end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: model id {} references unknown material interface '{}'"), id, ConvertString(interfaceById[id]));
            return MakeUnexpected(Failure{});
        }
        bindEntryById[id] = bindEntryIt.value().get();

        const auto namespaceIt = bindNamespaceLookup.find(interfaceName);
        if(namespaceIt == bindNamespaceLookup.end()){
            bindNamespaceIdById[id] = static_cast<u32>(id);
            if(!bindNamespaceLookup.emplace(interfaceName, static_cast<u32>(id)).second){
                NWB_ASSERT(false);
                return MakeUnexpected(Failure{});
            }
        }
        else
            bindNamespaceIdById[id] = namespaceIt.value();
    }

    const ShadowSurfaceSourceBuilder sourceBuilder;
    CookString source = sourceBuilder.buildDispatchHeader();

    // Context comes from the includer's shadow_surface.slangi; emitting here would force an unresolvable virtual include.

    // Emit each unique `.bind` once in a private namespace with scoped aliases so multiple interfaces do not collide.
    CookArena& cookArena = source.get_allocator().arena();
    CookHashSet<CookString> bindAliasSeenSymbols{ 0, Hasher<CookString>(), EqualTo<CookString>(), cookArena };
    ShadowSurfaceBindAliasVector bindAliasSymbols(cookArena);
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        const u32 bindNamespaceId = bindNamespaceIdById[id];
        const CookString bindNamespace = sourceBuilder.buildBindNamespace(bindNamespaceId);
        if(bindNamespaceId == static_cast<u32>(id))
            sourceBuilder.appendNamespaceInclude(AStringView(bindNamespace), interfaceById[id], source);
        if(!AppendShadowSurfaceBindAliases(*bindEntryById[id], AStringView(bindNamespace), bindAliasSymbols, bindAliasSeenSymbols, source))
            return MakeUnexpected(Failure{});
        sourceBuilder.appendInputDefines(source);
        sourceBuilder.appendHookInclude(static_cast<u32>(id), surfaceById[id], source);
        sourceBuilder.appendInputUndefines(source);
        sourceBuilder.appendBindAliasUndefines(bindAliasSymbols, source);
    }

    // Unknown id: no material surface is available (for example, an explicit opaque-stage material).
    // Shadow uses only neutral optical fields; GI may use baseColor, where fixed mid-grey is the no-surface fallback.
    sourceBuilder.appendDispatchFunction(surfaceById, source);

    const ScratchPath outputPath = ScratchPath(scratchArena, includeRoot) / s_ShadowSurfaceModuleSubPath;
    auto ensureDirectoriesResult2 = EnsureDirectories(outputPath.parentPath());
    if(!ensureDirectoriesResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult2.error().message()));
        return MakeUnexpected(Failure{});
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
        return MakeUnexpected(Failure{});
    }
    return includeRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


