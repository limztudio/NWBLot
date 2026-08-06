// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// deferred lighting BXDF dispatch (per-material shading model id + generated dispatch module)
static constexpr AStringView s_DeferredBxdfFunctionMacro = "NWB_DEFERRED_BXDF_FUNCTION";
static constexpr AStringView s_DeferredBxdfModelPrefix = "nwbDeferredBxdfModel";
static constexpr AStringView s_DeferredBxdfModuleSubPath = "deferred/generated/bxdf_dispatch.slangi";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Sentinel shadow-transmittance id for a material that contributes NO surface hook (it declares explicit opaque
// `shaders` instead of a `.surface`). The dense surface-authored ids start at 0, so a surface-less material must
// NOT reuse 0 (that aliases the first real surface hook). This reserved id is never emitted as a `case` in the
// generated dispatch switch. The shadow path never evaluates a surface for that opaque caster; GI reaches the
// dispatch's documented no-surface fallback instead of accidentally invoking model zero.
static constexpr u32 s_ShadowTransmittanceNoSurfaceModelId = Limit<u32>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssignMaterialShadingModelIdsImpl(
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.bxdfSource.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' is missing a deferred bxdf"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
    }

    Vector<AStringView, ScratchArena> uniqueSources(scratchArena);
    uniqueSources.reserve(materialEntries.size());
    for(const MaterialCookEntry& entry : materialEntries)
        uniqueSources.push_back(AStringView(entry.bxdfSource));
    Sort(uniqueSources.begin(), uniqueSources.end(), [](const AStringView lhs, const AStringView rhs){ return lhs < rhs; });

    usize uniqueCount = 0u;
    for(usize i = 0u; i < uniqueSources.size(); ++i){
        if(i == 0u || uniqueSources[i] != uniqueSources[uniqueCount - 1u])
            uniqueSources[uniqueCount++] = uniqueSources[i];
    }
    uniqueSources.resize(uniqueCount);
    if(uniqueCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: too many unique deferred bxdfs"));
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        const AStringView source(entry.bxdfSource);
        const auto sourceIt = LowerBound(uniqueSources.begin(), uniqueSources.end(), source);
        if(sourceIt == uniqueSources.end() || *sourceIt != source){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign shading model id for '{}'"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
        entry.shadingModelId = static_cast<u32>(static_cast<usize>(sourceIt - uniqueSources.begin()));
    }

    // Assign each material a separate shadow-transmittance id deduped over the unique `.surface` sources (the
    // surface hook supplies the per-hit optical values and GI base color). An opaque material that declares explicit
    // `shaders` instead gets the reserved no-surface sentinel id (NOT 0, which is the first real surface hook), so
    // GI reaches the generated dispatch's documented neutral fallback rather than evaluating an unrelated hook.
    Vector<AStringView, ScratchArena> uniqueSurfaces(scratchArena);
    uniqueSurfaces.reserve(materialEntries.size());
    for(const MaterialCookEntry& entry : materialEntries){
        if(!entry.surfaceSource.empty())
            uniqueSurfaces.push_back(AStringView(entry.surfaceSource));
    }
    Sort(uniqueSurfaces.begin(), uniqueSurfaces.end(), [](const AStringView lhs, const AStringView rhs){ return lhs < rhs; });

    usize uniqueSurfaceCount = 0u;
    for(usize i = 0u; i < uniqueSurfaces.size(); ++i){
        if(i == 0u || uniqueSurfaces[i] != uniqueSurfaces[uniqueSurfaceCount - 1u])
            uniqueSurfaces[uniqueSurfaceCount++] = uniqueSurfaces[i];
    }
    uniqueSurfaces.resize(uniqueSurfaceCount);
    if(uniqueSurfaceCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material cook: too many unique shadow transmittance surfaces"));
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty()){
            entry.shadowTransmittanceModelId = s_ShadowTransmittanceNoSurfaceModelId;
            continue;
        }

        const AStringView surface(entry.surfaceSource);
        const auto surfaceIt = LowerBound(uniqueSurfaces.begin(), uniqueSurfaces.end(), surface);
        if(surfaceIt == uniqueSurfaces.end() || *surfaceIt != surface){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign shadow transmittance id for '{}'"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
        entry.shadowTransmittanceModelId = static_cast<u32>(static_cast<usize>(surfaceIt - uniqueSurfaces.begin()));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Path BuildDeferredBxdfIncludeRoot(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena
){
    ScratchString configurationName(configurationSafeName, scratchArena);
    return cacheDirectory / configurationName.c_str() / "deferred_modules";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool PrepareGeneratedIncludeRoot(const Path& includeRoot, const AStringView generatorName){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to clear generated include directory '{}': {}")
            , StringConvert(generatorName)
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    if(!EnsureDirectories(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to create generated include directory '{}': {}")
            , StringConvert(generatorName)
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitDeferredBxdfDispatchModuleImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
){
    outIncludeRoot.clear();
    outIncludeRoot = BuildDeferredBxdfIncludeRoot(cacheDirectory, configurationSafeName, scratchArena);
    if(!PrepareGeneratedIncludeRoot(outIncludeRoot, "Deferred bxdf dispatch"))
        return false;

    // Build a dense id -> bxdf-source table from the (already assigned) materials. Each unique bxdf appears at
    // exactly one id; materials sharing a bxdf share the slot.
    u32 maxId = 0u;
    bool anyBxdf = false;
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
            return false;
        }
        slot = source;
    }

    CookArena& arena = materialEntries.get_allocator().arena();
    CookString source(arena);
    source += "// Generated by AssetVolumeCooker from material `bxdf` declarations. Do not edit.\n";
    source += "#ifndef NWB_GRAPHICS_DEFERRED_GENERATED_BXDF_DISPATCH_SLANGI\n";
    source += "#define NWB_GRAPHICS_DEFERRED_GENERATED_BXDF_DISPATCH_SLANGI\n\n";

    for(usize id = 0u; id < sourceById.size(); ++id){
        if(sourceById[id].empty())
            continue;

        char idText[TextDetail::s_DecimalTextBufferBytes] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "#define ";
        source += s_DeferredBxdfFunctionMacro;
        source += ' ';
        source += s_DeferredBxdfModelPrefix;
        source += idView;
        source += "\n#include \"";
        source += sourceById[id];
        source += "\"\n#undef ";
        source += s_DeferredBxdfFunctionMacro;
        source += "\n\n";
    }

    source += "half3 nwbDeferredDispatchBxdf(uint shadingModel, NwbBxdfSurface surface, int2 pixel){\n";
    source += "    switch(shadingModel){\n";
    for(usize id = 0u; id < sourceById.size(); ++id){
        if(sourceById[id].empty())
            continue;

        char idText[TextDetail::s_DecimalTextBufferBytes] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "    case ";
        source += idView;
        source += "u: return ";
        source += s_DeferredBxdfModelPrefix;
        source += idView;
        source += "(surface, pixel);\n";
    }
    source += "    default: return half3(1.0h, 0.0h, 1.0h); // no engine default BXDF: an unknown id shows magenta\n";
    source += "    }\n";
    source += "}\n\n#endif\n";

    const Path outputPath = outIncludeRoot / s_DeferredBxdfModuleSubPath.data();
    ErrorCode errorCode;
    if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to create generated include parent '{}': {}")
            , PathToString<tchar>(outputPath.parent_path())
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to write generated include '{}'")
            , PathToString<tchar>(outputPath)
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// shadow transmittance dispatch (per-material surface id + generated dispatch module)
static constexpr AStringView s_ShadowTransmittanceSurfaceMacro = "nwbMaterialSurface";
static constexpr AStringView s_ShadowTransmittanceModelPrefix = "nwbShadowSurfaceModel";
static constexpr AStringView s_ShadowTransmittanceWrapperPrefix = "nwbShadowTransmittanceModel";

// Per-interface Slang namespace that isolates each material `.bind` file-scope symbol in the dispatch module (the
// one TU that concatenates multiple `.bind` files). The project-owned `.surface` fragment stays global so its guarded
// helper includes retain their normal global ownership; generated aliases expose exactly that surface's bind API.
static constexpr AStringView s_ShadowTransmittanceBindNamespacePrefix = "nwbShadowBindModel";

static constexpr AStringView s_ShadowTransmittanceModuleSubPath = "shadow/generated/transmittance_dispatch.slangi";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Path BuildShadowTransmittanceIncludeRoot(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena
){
    ScratchString configurationName(configurationSafeName, scratchArena);
    return cacheDirectory / configurationName.c_str() / "shadow_modules";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ShadowTransmittanceBindEntryLookup = HashMap<
    Name,
    const MaterialBindEntry*,
    Hasher<Name>,
    EqualTo<Name>,
    ScratchArena
>;

using ShadowTransmittanceBindNamespaceLookup = HashMap<
    Name,
    u32,
    Hasher<Name>,
    EqualTo<Name>,
    ScratchArena
>;

using ShadowTransmittanceBindAliasVector = Vector<ScratchString, ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendShadowTransmittanceBindAlias(
    const AStringView symbol,
    const AStringView bindNamespace,
    ScratchHashSet<ScratchString>& inOutSeenSymbols,
    ShadowTransmittanceBindAliasVector& outAliasSymbols,
    CookString& inOutSource,
    ScratchArena& scratchArena
){
    ScratchString seenSymbol(symbol, scratchArena);
    if(!inOutSeenSymbols.insert(Move(seenSymbol)).second)
        return;

    inOutSource += "#define ";
    inOutSource += symbol;
    inOutSource += ' ';
    inOutSource += bindNamespace;
    inOutSource += "::";
    inOutSource += symbol;
    inOutSource += '\n';

    ScratchString aliasSymbol(symbol, scratchArena);
    outAliasSymbols.push_back(Move(aliasSymbol));
}

static bool AppendShadowTransmittanceBindAliases(
    const MaterialBindEntry& bindEntry,
    const AStringView bindNamespace,
    ScratchHashSet<ScratchString>& inOutSeenSymbols,
    ShadowTransmittanceBindAliasVector& outAliasSymbols,
    CookString& inOutSource,
    ScratchArena& scratchArena
){
    inOutSeenSymbols.clear();
    outAliasSymbols.clear();
    outAliasSymbols.reserve(
        static_cast<usize>(NameDetail::s_HashLaneCount) + 7u + bindEntry.structs.size() + bindEntry.instances.size() * 4u
    );

    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        char laneDigits[TextDetail::s_DecimalTextBufferBytes] = {};
        ScratchString suffix("INTERFACE_HASH_", scratchArena);
        suffix += FormatDecimal(static_cast<usize>(lane), laneDigits);
        const ScratchString symbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, AStringView(suffix));
        AppendShadowTransmittanceBindAlias(
            AStringView(symbol),
            bindNamespace,
            inOutSeenSymbols,
            outAliasSymbols,
            inOutSource,
            scratchArena
        );
    }

    const AStringView layoutSymbols[] = {
        "LAYOUT_HASH",
        "BLOCK_COUNT",
        "FIELD_COUNT",
        "STORAGE_CONSTANT",
        "STORAGE_MUTABLE",
        "CONSTANT_BYTE_SIZE",
        "MUTABLE_BYTE_SIZE"
    };
    for(const AStringView suffix : layoutSymbols){
        const ScratchString symbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, suffix);
        AppendShadowTransmittanceBindAlias(
            AStringView(symbol),
            bindNamespace,
            inOutSeenSymbols,
            outAliasSymbols,
            inOutSource,
            scratchArena
        );
    }

    for(const MaterialBindStruct& bindStruct : bindEntry.structs){
        AppendShadowTransmittanceBindAlias(
            AStringView(bindStruct.name),
            bindNamespace,
            inOutSeenSymbols,
            outAliasSymbols,
            inOutSource,
            scratchArena
        );
    }

    for(const MaterialBindInstance& instance : bindEntry.instances){
        const MaterialBindStruct* bindStruct = bindEntry.findStruct(AStringView(instance.type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: interface '{}' instance '{}' has unknown type '{}'")
                , StringConvert(bindEntry.virtualPath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
            );
            return false;
        }

        const AStringView instanceName(instance.name);
        const AStringView blockSuffixes[] = { "_STORAGE", "_BYTE_OFFSET", "_BYTE_SIZE" };
        for(const AStringView suffix : blockSuffixes){
            const ScratchString symbol = BuildMaterialBindGeneratedSymbol(
                scratchArena,
                { instanceName },
                suffix
            );
            AppendShadowTransmittanceBindAlias(
                AStringView(symbol),
                bindNamespace,
                inOutSeenSymbols,
                outAliasSymbols,
                inOutSource,
                scratchArena
            );
        }

        const ScratchString blockAccessor = BuildMaterialBindAccessorName(scratchArena, { instanceName });
        AppendShadowTransmittanceBindAlias(
            AStringView(blockAccessor),
            bindNamespace,
            inOutSeenSymbols,
            outAliasSymbols,
            inOutSource,
            scratchArena
        );

        for(const MaterialBindField& field : bindStruct->fields){
            const AStringView fieldName(field.name);
            const AStringView fieldSuffixes[] = { "_BYTE_OFFSET", "_KEY", "_DEFAULT" };
            for(const AStringView suffix : fieldSuffixes){
                const ScratchString symbol = BuildMaterialBindGeneratedSymbol(
                    scratchArena,
                    { instanceName, fieldName },
                    suffix
                );
                AppendShadowTransmittanceBindAlias(
                    AStringView(symbol),
                    bindNamespace,
                    inOutSeenSymbols,
                    outAliasSymbols,
                    inOutSource,
                    scratchArena
                );
            }

            const ScratchString fieldAccessor = BuildMaterialBindAccessorName(
                scratchArena,
                { instanceName, fieldName }
            );
            AppendShadowTransmittanceBindAlias(
                AStringView(fieldAccessor),
                bindNamespace,
                inOutSeenSymbols,
                outAliasSymbols,
                inOutSource,
                scratchArena
            );
        }
    }

    return true;
}

static void AppendShadowTransmittanceBindAliasUndefines(
    const ShadowTransmittanceBindAliasVector& aliasSymbols,
    CookString& inOutSource
){
    for(usize index = aliasSymbols.size(); index > 0u; ){
        --index;
        const ScratchString& symbol = aliasSymbols[index];
        inOutSource += "#undef ";
        inOutSource += AStringView(symbol);
        inOutSource += '\n';
    }
}

static bool BuildShadowTransmittanceBindEntryLookup(
    const CookVector<MaterialBindEntry>& materialBindEntries,
    ShadowTransmittanceBindEntryLookup& outLookup
){
    outLookup.clear();
    outLookup.reserve(materialBindEntries.size());
    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        const Name interfaceName(AStringView(bindEntry.virtualPath));
        if(!interfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: material bind interface path '{}' is invalid")
                , StringConvert(bindEntry.virtualPath)
            );
            return false;
        }
        if(!outLookup.emplace(interfaceName, &bindEntry).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: duplicate material bind interface '{}'"), StringConvert(bindEntry.virtualPath));
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitShadowTransmittanceDispatchModuleImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialBindEntry>& materialBindEntries,
    const CookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
){
    outIncludeRoot.clear();
    outIncludeRoot = BuildShadowTransmittanceIncludeRoot(cacheDirectory, configurationSafeName, scratchArena);
    if(!PrepareGeneratedIncludeRoot(outIncludeRoot, "Shadow transmittance dispatch"))
        return false;

    // Build a dense shadowTransmittanceModelId -> (surface source, .bind interface) table from the (already
    // assigned) materials. Each unique surface appears at exactly one id; materials sharing a surface share the
    // slot. The interface is carried alongside because the surface hook reads its typed `.bind` accessors by
    // fixed name -- materials sharing a surface therefore share the interface.
    u32 maxId = 0u;
    bool anySurface = false;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.surfaceSource.empty())
            continue;
        anySurface = true;
        if(entry.shadowTransmittanceModelId > maxId)
            maxId = entry.shadowTransmittanceModelId;
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
        AStringView& surfaceSlot = surfaceById[entry.shadowTransmittanceModelId];
        if(!surfaceSlot.empty() && surfaceSlot != surface){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: model id {} maps to multiple surface sources"), entry.shadowTransmittanceModelId);
            return false;
        }
        surfaceSlot = surface;

        const AStringView interfaceName(entry.materialInterface);
        AStringView& interfaceSlot = interfaceById[entry.shadowTransmittanceModelId];
        if(!interfaceSlot.empty() && interfaceSlot != interfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: model id {} maps to multiple material interfaces"), entry.shadowTransmittanceModelId);
            return false;
        }
        interfaceSlot = interfaceName;
    }

    ShadowTransmittanceBindEntryLookup bindEntryLookup(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    if(!BuildShadowTransmittanceBindEntryLookup(materialBindEntries, bindEntryLookup))
        return false;

    Vector<const MaterialBindEntry*, ScratchArena> bindEntryById(scratchArena);
    Vector<u32, ScratchArena> bindNamespaceIdById(scratchArena);
    if(anySurface){
        bindEntryById.resize(surfaceById.size());
        bindNamespaceIdById.resize(surfaceById.size());
    }

    ShadowTransmittanceBindNamespaceLookup bindNamespaceLookup(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    bindNamespaceLookup.reserve(materialBindEntries.size());
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        const Name interfaceName(interfaceById[id]);
        const auto bindEntryIt = bindEntryLookup.find(interfaceName);
        if(bindEntryIt == bindEntryLookup.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: model id {} references unknown material interface '{}'")
                , id
                , StringConvert(interfaceById[id])
            );
            return false;
        }
        bindEntryById[id] = bindEntryIt.value();

        const auto namespaceIt = bindNamespaceLookup.find(interfaceName);
        if(namespaceIt == bindNamespaceLookup.end()){
            bindNamespaceIdById[id] = static_cast<u32>(id);
            bindNamespaceLookup.emplace(interfaceName, static_cast<u32>(id));
        }
        else
            bindNamespaceIdById[id] = namespaceIt.value();
    }

    CookArena& arena = materialEntries.get_allocator().arena();
    CookString source(arena);
    source += "// Generated by AssetVolumeCooker from material `surface` declarations. Do not edit.\n";
    source += "#ifndef NWB_GRAPHICS_SHADOW_GENERATED_TRANSMITTANCE_DISPATCH_SLANGI\n";
    source += "#define NWB_GRAPHICS_SHADOW_GENERATED_TRANSMITTANCE_DISPATCH_SLANGI\n\n";

    // The trace material-constants context (NwbShadowHit + the per-invocation accessors the surface hooks read
    // -- nwbMeshLoadInstance / nwbMeshMaterialConstantByteOffset / ... -- + the surface contract) is supplied by
    // the includer BEFORE this module, exactly as the deferred BXDF dispatch relies on lighting_cs to bring in the
    // framework first. The includer (each shadow trace shader) #includes shadow/shadow_surface.slangi -- where it
    // also points the material-constants buffers at its heap-selected context -- then this module; emitting the framework
    // include here instead would force a virtual engine/ path that the shader -I roots do not resolve.

    // Per-id surface hook. The dispatch is the one shader TU that needs multiple `.bind` interfaces. Each unique
    // interface is emitted once in a private namespace; before a project-owned global surface fragment is included,
    // aliases map its fixed generated bind API to that namespace, then are immediately removed. A global `using`
    // directive cannot be used here: later interfaces may provide the same accessor names, while wrapping a surface
    // in a namespace would move its guarded project helper includes away from their normal global scope.
    ScratchHashSet<ScratchString> bindAliasSeenSymbols{
        0,
        Hasher<ScratchString>(),
        EqualTo<ScratchString>(),
        scratchArena
    };
    ShadowTransmittanceBindAliasVector bindAliasSymbols(scratchArena);
    ScratchString bindNamespace(scratchArena);
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        char idText[TextDetail::s_DecimalTextBufferBytes] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        const u32 bindNamespaceId = bindNamespaceIdById[id];
        char bindNamespaceIdText[TextDetail::s_DecimalTextBufferBytes] = {};
        const AStringView bindNamespaceIdView = FormatDecimal(bindNamespaceId, bindNamespaceIdText);
        bindNamespace.clear();
        bindNamespace += s_ShadowTransmittanceBindNamespacePrefix;
        bindNamespace += bindNamespaceIdView;
        if(bindNamespaceId == static_cast<u32>(id)){
            source += "namespace ";
            source += AStringView(bindNamespace);
            source += "{\n#include \"";
            source += interfaceById[id];
            source += ".bind\"\n}\n";
        }
        if(!AppendShadowTransmittanceBindAliases(
            *bindEntryById[id],
            AStringView(bindNamespace),
            bindAliasSeenSymbols,
            bindAliasSymbols,
            source,
            scratchArena
        ))
            return false;
        source += "#define ";
        source += s_ShadowTransmittanceSurfaceMacro;
        source += ' ';
        source += s_ShadowTransmittanceModelPrefix;
        source += idView;
        source += "\n#include \"";
        source += surfaceById[id];
        source += "\"\n#undef ";
        source += s_ShadowTransmittanceSurfaceMacro;
        source += "\n";
        AppendShadowTransmittanceBindAliasUndefines(bindAliasSymbols, source);
        source += "NwbMeshSurface ";
        source += s_ShadowTransmittanceWrapperPrefix;
        source += idView;
        source += "(NwbShadowHit hit){\n";
        source += "    nwbShadowSetMaterialContext(hit);\n";
        source += "    NwbMeshSurfaceInputs in = nwbShadowBuildSurfaceInputs(hit);\n";
        source += "    nwbLoadMeshSurfaceInputs(in);\n";
        source += "    return ";
        source += s_ShadowTransmittanceModelPrefix;
        source += idView;
        source += "();\n";
        source += "}\n\n";
    }

    source += "NwbMeshSurface nwbShadowDispatchSurface(uint shadingModel, NwbShadowHit hit){\n";
    source += "    switch(shadingModel){\n";
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        char idText[TextDetail::s_DecimalTextBufferBytes] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "    case ";
        source += idView;
        source += "u: return ";
        source += s_ShadowTransmittanceWrapperPrefix;
        source += idView;
        source += "(hit);\n";
    }
    // Unknown id: no material surface is available (for example, an explicit opaque-stage material). Shadow consumes
    // only the neutral optical fields; GI may consume baseColor, where the fixed mid-grey is an explicit no-surface
    // fallback rather than an inferred project-material albedo.
    source += "    default: return nwbMakeMeshSurface(half3(0.5h, 0.5h, 0.5h), hit.worldNormal, half(0.0), half(0.0));\n";
    source += "    }\n";
    source += "}\n\n#endif\n";

    const Path outputPath = outIncludeRoot / s_ShadowTransmittanceModuleSubPath.data();
    ErrorCode errorCode;
    if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: failed to create generated include parent '{}': {}")
            , PathToString<tchar>(outputPath.parent_path())
            , StringConvert(errorCode.message())
        );
        return false;
    }
    if(!WriteTextFile(outputPath, AStringView(source))){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: failed to write generated include '{}'")
            , PathToString<tchar>(outputPath)
        );
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

