// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ShadowSurfaceBindNamespaceLookup = HashMap<Name, u32, ScratchArena, Hasher<Name>, EqualTo<Name>>;

using ShadowSurfaceBindAliasVector = Vector<ScratchString, ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendShadowSurfaceBindAlias(const AStringView symbol, const AStringView bindNamespace, ScratchArena& scratchArena, ShadowSurfaceBindAliasVector& outAliasSymbols, ScratchHashSet<ScratchString>& inOutSeenSymbols, CookString& inOutSource){
    ScratchString seenSymbol(symbol, scratchArena);
    if(!inOutSeenSymbols.insert(Move(seenSymbol)).second)
        return;

    AppendShadowSurfaceBindAliasDefinition(symbol, bindNamespace, inOutSource);

    ScratchString aliasSymbol(symbol, scratchArena);
    outAliasSymbols.push_back(Move(aliasSymbol));
}

static b8 AppendShadowSurfaceBindAliases(
    const MaterialBindEntry& bindEntry,
    const AStringView bindNamespace,
    ScratchArena& scratchArena,
    ShadowSurfaceBindAliasVector& outAliasSymbols,
    ScratchHashSet<ScratchString>& inOutSeenSymbols,
    CookString& inOutSource
){
    inOutSeenSymbols.clear();
    outAliasSymbols.clear();
    outAliasSymbols.reserve(static_cast<usize>(s_NameHashLaneCount) + s_ShadowSurfaceBindLayoutAliasCount + bindEntry.structs.size() + bindEntry.instances.size() * s_ShadowSurfaceBindInstanceAliasReserveCount);

    for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane){
        Array<char, s_DecimalTextBufferBytes> laneDigits{};
        ScratchString suffix(s_MaterialBindInterfaceHashSymbolPrefix, scratchArena);
        suffix += FormatDecimal(static_cast<usize>(lane), laneDigits);
        const ScratchString symbol = BuildMaterialBindGeneratedSymbol({}, AStringView(suffix), scratchArena);
        AppendShadowSurfaceBindAlias(AStringView(symbol), bindNamespace, scratchArena, outAliasSymbols, inOutSeenSymbols, inOutSource);
    }

    static constexpr Array<AStringView, s_ShadowSurfaceBindLayoutAliasCount> s_LayoutSymbols = {
        s_MaterialBindLayoutHashSymbol,
        s_MaterialBindBlockCountSymbol,
        s_MaterialBindFieldCountSymbol,
        s_MaterialBindStorageConstantSymbol,
        s_MaterialBindStorageMutableSymbol,
        s_MaterialBindConstantByteSizeSymbol,
        s_MaterialBindMutableByteSizeSymbol,
    };
    for(const AStringView suffix : s_LayoutSymbols){
        const ScratchString symbol = BuildMaterialBindGeneratedSymbol({}, suffix, scratchArena);
        AppendShadowSurfaceBindAlias(AStringView(symbol), bindNamespace, scratchArena, outAliasSymbols, inOutSeenSymbols, inOutSource);
    }

    for(const MaterialBindStruct& bindStruct : bindEntry.structs){
        AppendShadowSurfaceBindAlias(AStringView(bindStruct.name), bindNamespace, scratchArena, outAliasSymbols, inOutSeenSymbols, inOutSource);
    }

    for(const MaterialBindInstance& instance : bindEntry.instances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance.type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow surface dispatch: interface '{}' instance '{}' has unknown type '{}'"), ConvertString(bindEntry.virtualPath), ConvertString(instance.name), ConvertString(instance.type));
            return false;
        }

        const AStringView instanceName(instance.name);
        static constexpr Array<AStringView, 3u> s_BlockSuffixes = { s_MaterialBindStorageSymbolSuffix, s_MaterialBindByteOffsetSymbolSuffix, s_MaterialBindByteSizeSymbolSuffix };
        for(const AStringView suffix : s_BlockSuffixes){
            const ScratchString symbol = BuildMaterialBindGeneratedSymbol({ instanceName }, suffix, scratchArena);
            AppendShadowSurfaceBindAlias(AStringView(symbol), bindNamespace, scratchArena, outAliasSymbols, inOutSeenSymbols, inOutSource);
        }

        const ScratchString blockAccessor = BuildMaterialBindAccessorName({ instanceName }, scratchArena);
        AppendShadowSurfaceBindAlias(AStringView(blockAccessor), bindNamespace, scratchArena, outAliasSymbols, inOutSeenSymbols, inOutSource);

        for(const MaterialBindField& field : bindStruct->fields){
            const AStringView fieldName(field.name);
            static constexpr Array<AStringView, 3u> s_FieldSuffixes = { s_MaterialBindByteOffsetSymbolSuffix, s_MaterialBindKeySymbolSuffix, s_MaterialBindDefaultSymbolSuffix };
            for(const AStringView suffix : s_FieldSuffixes){
                const ScratchString symbol = BuildMaterialBindGeneratedSymbol({ instanceName, fieldName }, suffix, scratchArena);
                AppendShadowSurfaceBindAlias(AStringView(symbol), bindNamespace, scratchArena, outAliasSymbols, inOutSeenSymbols, inOutSource);
            }

            const ScratchString fieldAccessor = BuildMaterialBindAccessorName({ instanceName, fieldName }, scratchArena);
            AppendShadowSurfaceBindAlias(AStringView(fieldAccessor), bindNamespace, scratchArena, outAliasSymbols, inOutSeenSymbols, inOutSource);
        }
    }

    return true;
}

static void AppendShadowSurfaceBindAliasUndefines(const Span<const ScratchString> aliasSymbols, CookString& inOutSource){
    for(usize index = aliasSymbols.size(); index > 0u; ){
        --index;
        const ScratchString& symbol = aliasSymbols[index];
        inOutSource += s_GeneratedUndefDirective;
        inOutSource += AStringView(symbol);
        inOutSource += '\n';
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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


Expected<Path> EmitShadowSurfaceDispatchModule(
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

    CookString source = BuildShadowSurfaceDispatchHeader();

    // Context comes from the includer's shadow_surface.slangi; emitting here would force an unresolvable virtual include.

    // Emit each unique `.bind` once in a private namespace with scoped aliases so multiple interfaces do not collide.
    ScratchHashSet<ScratchString> bindAliasSeenSymbols{ 0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena };
    ShadowSurfaceBindAliasVector bindAliasSymbols(scratchArena);
    ScratchString bindNamespace(scratchArena);
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        const u32 bindNamespaceId = bindNamespaceIdById[id];
        Array<char, s_DecimalTextBufferBytes> bindNamespaceIdText{};
        const AStringView bindNamespaceIdView = FormatDecimal(bindNamespaceId, bindNamespaceIdText);
        bindNamespace.clear();
        bindNamespace += s_ShadowSurfaceBindNamespacePrefix;
        bindNamespace += bindNamespaceIdView;
        if(bindNamespaceId == static_cast<u32>(id))
            AppendShadowSurfaceNamespaceInclude(AStringView(bindNamespace), interfaceById[id], source);
        if(!AppendShadowSurfaceBindAliases(*bindEntryById[id], AStringView(bindNamespace), scratchArena, bindAliasSymbols, bindAliasSeenSymbols, source))
            return MakeUnexpected(Failure{});
        AppendShadowSurfaceInputDefines(source);
        AppendShadowSurfaceHookInclude(static_cast<u32>(id), surfaceById[id], source);
        AppendShadowSurfaceInputUndefines(source);
        AppendShadowSurfaceBindAliasUndefines(bindAliasSymbols, source);
    }

    // Unknown id: no material surface is available (for example, an explicit opaque-stage material).
    // Shadow uses only neutral optical fields; GI may use baseColor, where fixed mid-grey is the no-surface fallback.
    AppendShadowSurfaceDispatchFunction(surfaceById, source);

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

