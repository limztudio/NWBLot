
#if defined(NWB_COOK)


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook{


// deferred lighting BXDF dispatch (per-material shading model id + generated dispatch module)


static constexpr AStringView s_DeferredBxdfFunctionMacro = "NWB_DEFERRED_BXDF_FUNCTION";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_DeferredBxdfModelPrefix = "nwbDeferredBxdfModel";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_DeferredBxdfModuleSubPath = "deferred/generated/bxdf_dispatch.slangi";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Sentinel shadow-transmittance id for a material that contributes NO surface hook (it declares explicit
// `shaders` instead of a `.surface`). The dense surface-authored ids start at 0, so a surface-less material must
// NOT reuse 0 (that aliases the first real surface hook). This reserved id is never emitted as a `case` in the
// generated dispatch switch, so a hit on such an occluder falls through to `default: return half3(1)` -- the
// occluder passes all light untinted, the only correct behavior for a material with no transmittance hook.
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
        bool assigned = false;
        for(usize id = 0u; id < uniqueSources.size(); ++id){
            if(uniqueSources[id] == source){
                entry.shadingModelId = static_cast<u32>(id);
                assigned = true;
                break;
            }
        }
        if(!assigned){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign shading model id for '{}'"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
    }

    // Assign each material a separate shadow-transmittance id deduped over the unique `.surface` sources (the
    // surface hook computes the per-hit transmittance the shadow trace returns). A material that declares
    // explicit `shaders` instead of a `surface` gets the reserved no-surface sentinel id (NOT 0, which is the
    // first real surface hook); the generated dispatch routes that sentinel to its `default` -> float3(1), so a
    // transparent surface-less occluder passes all light untinted instead of evaluating an unrelated material.
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
        bool assigned = false;
        for(usize id = 0u; id < uniqueSurfaces.size(); ++id){
            if(uniqueSurfaces[id] == surface){
                entry.shadowTransmittanceModelId = static_cast<u32>(id);
                assigned = true;
                break;
            }
        }
        if(!assigned){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign shadow transmittance id for '{}'"), StringConvert(entry.virtualPath.c_str()));
            return false;
        }
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


static bool PrepareDeferredBxdfIncludeRoot(const Path& includeRoot){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to clear generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    if(!EnsureDirectories(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Deferred bxdf dispatch: failed to create generated include directory '{}': {}")
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
    if(!PrepareDeferredBxdfIncludeRoot(outIncludeRoot))
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

        char idText[32] = {};
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

        char idText[32] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "    case ";
        source += idView;
        source += "u: return ";
        source += s_DeferredBxdfModelPrefix;
        source += idView;
        source += "(surface, pixel);\n";
    }
    source += "    default: return half3(1.0, 0.0, 1.0); // no engine default BXDF: an unknown id shows magenta\n";
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ShadowTransmittanceModelPrefix = "nwbShadowSurfaceModel";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ShadowTransmittanceWrapperPrefix = "nwbShadowTransmittanceModel";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-id Slang namespace that isolates each material's `.bind` file-scope symbols in the dispatch module (the one
// TU that concatenates multiple `.bind` files) so distinct interfaces' fixed-named layout constants/structs/
// accessors do not collide; a `using namespace` then exposes them to the global-scope surface hook.
static constexpr AStringView s_ShadowTransmittanceBindNamespacePrefix = "nwbShadowBindModel";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


static bool PrepareShadowTransmittanceIncludeRoot(const Path& includeRoot){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: failed to clear generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    if(!EnsureDirectories(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Shadow transmittance dispatch: failed to create generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitShadowTransmittanceDispatchModuleImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialCookEntry>& materialEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
){
    outIncludeRoot.clear();
    outIncludeRoot = BuildShadowTransmittanceIncludeRoot(cacheDirectory, configurationSafeName, scratchArena);
    if(!PrepareShadowTransmittanceIncludeRoot(outIncludeRoot))
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

    CookArena& arena = materialEntries.get_allocator().arena();
    CookString source(arena);
    source += "// Generated by AssetVolumeCooker from material `surface` declarations. Do not edit.\n";
    source += "#ifndef NWB_GRAPHICS_SHADOW_GENERATED_TRANSMITTANCE_DISPATCH_SLANGI\n";
    source += "#define NWB_GRAPHICS_SHADOW_GENERATED_TRANSMITTANCE_DISPATCH_SLANGI\n\n";

    // The trace material-constants context (NwbShadowHit + the per-invocation accessors the surface hooks read
    // -- nwbMeshLoadInstance / nwbMeshMaterialConstantByteOffset / ... -- + the surface contract) is supplied by
    // the includer BEFORE this module, exactly as the deferred BXDF dispatch relies on lighting_ps to bring in the
    // framework first. The includer (each shadow trace shader) #includes shadow/shadow_surface.slangi -- where it
    // also points the material-constants buffers at its own binding set -- then this module; emitting the framework
    // include here instead would force a virtual engine/ path that the shader -I roots do not resolve.

    // Per-id surface hook. This is the ONE translation unit that pulls MULTIPLE materials' `.bind` files together
    // (the per-material G-buffer PS pulls only its own one `.bind`), and a generated `.bind` emits file-scope
    // symbols with FIXED, non-interface-qualified names -- the layout constants (NWB_MATERIAL_BIND_LAYOUT_HASH /
    // _BLOCK_COUNT / _FIELD_COUNT / _INTERFACE_HASH_n / per-field _KEY/_DEFAULT/_BYTE_OFFSET) are byte-identical
    // names across ANY two distinct `.bind` files, so concatenating two distinct interfaces would redefine them.
    // Isolation: wrap each id's `.bind` body in its own Slang namespace (nwbShadowBindModel<id>) so its file-scope
    // symbols + structs + accessors are namespace-qualified and never collide across interfaces, then bring that
    // namespace into scope with `using namespace` so the (global-scope) surface hook -- which references the bind
    // accessors/structs by their fixed unqualified names -- still resolves them. The `.surface` is included at
    // global scope (after the using) so any shared helper headers it pulls in stay global + guarded (one copy
    // across ids); the surface's lookups for global framework symbols (nwbMeshLoadInstance / nwbMakeMeshSurface /
    // inNormal) are unaffected. Two ids that share an interface collapse to one `.bind` body via its per-path
    // include guard (the second namespace is empty); the shared interface's symbols stay reachable through the
    // first id's `using`, so the shared-interface case keeps working. The surface hook itself is still renamed to
    // a unique per-id name (nwbShadowSurfaceModel<id>) so multiple hooks coexist; the wrapper sets the trace
    // material context from the hit + loads the surface-input statics (inlining nwbMaterialSurfaceAt) before
    // invoking the renamed hook, then returns the hook's whole NwbMeshSurface (its optical params -- refractionIor /
    // shadowAbsorptionTint -- plus base color / normal). The hook supplies only the params; the ENGINE integrates the shadow
    // visibility over the true entry->exit volume path (per-crossing Fresnel + signed Beer-Lambert optical depth),
    // since only the trace sees both faces of an occluder. There is no per-hit transmittance for the hook to own.
    for(usize id = 0u; id < surfaceById.size(); ++id){
        if(surfaceById[id].empty())
            continue;

        char idText[32] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "namespace ";
        source += s_ShadowTransmittanceBindNamespacePrefix;
        source += idView;
        source += "{\n#include \"";
        source += interfaceById[id];
        source += ".bind\"\n}\n";
        source += "using namespace ";
        source += s_ShadowTransmittanceBindNamespacePrefix;
        source += idView;
        source += ";\n";
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

        char idText[32] = {};
        const AStringView idView = FormatDecimal(static_cast<u32>(id), idText);
        source += "    case ";
        source += idView;
        source += "u: return ";
        source += s_ShadowTransmittanceWrapperPrefix;
        source += idView;
        source += "(hit);\n";
    }
    // Unknown id: a neutral surface (ior = 1, transmission = white -> no Fresnel attenuation, no absorption).
    source += "    default: return nwbMakeMeshSurface(half3(0.0, 0.0, 0.0), hit.worldNormal, half(0.0), half(0.0));\n";
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

