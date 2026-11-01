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


struct MaterialAvboitPassPixelShaderSpec{
    AStringView generatedDirectoryLeaf;
    AStringView authoringHeaderInclude;
    AStringView passLabel;
    AStringView generatedNamePrefix;
    MaterialCookString MaterialCookEntry::* entryShaderNameField = nullptr;
};


static constexpr MaterialAvboitPassPixelShaderSpec s_AvboitAccumulatePassSpec = {
    AStringView("material_avboit_accumulate_pixel_shaders"),
    AStringView("avboit/accumulate_ps_authoring.slangi"),
    s_MaterialAvboitAccumulatePassLabel,
    s_AvboitAccumulatePixelShaderGeneratedPrefix,
    &MaterialCookEntry::avboitAccumulatePixelShaderName,
};

static constexpr MaterialAvboitPassPixelShaderSpec s_AvboitOccupancyPassSpec = {
    AStringView("material_avboit_occupancy_pixel_shaders"),
    AStringView("avboit/occupancy_ps_authoring.slangi"),
    s_MaterialAvboitOccupancyPassLabel,
    s_AvboitOccupancyPixelShaderGeneratedPrefix,
    &MaterialCookEntry::avboitOccupancyPixelShaderName,
};

static constexpr MaterialAvboitPassPixelShaderSpec s_AvboitExtinctionPassSpec = {
    AStringView("material_avboit_extinction_pixel_shaders"),
    AStringView("avboit/extinction_ps_authoring.slangi"),
    s_MaterialAvboitExtinctionPassLabel,
    s_AvboitExtinctionPixelShaderGeneratedPrefix,
    &MaterialCookEntry::avboitExtinctionPixelShaderName,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendGeneratedMaterialShaderIncludes(const AStringView authoringHeaderInclude, const MaterialCookEntry& entry, ScratchString& inOutSource){
    inOutSource += s_GeneratedIncludeDirective;
    inOutSource += authoringHeaderInclude;
    inOutSource += s_GeneratedIncludeDirectiveEnd;
    inOutSource += s_GeneratedIncludeDirective;
    inOutSource += entry.materialInterface;
    inOutSource += ".bind\"\n";
    inOutSource += s_GeneratedIncludeDirective;
    inOutSource += entry.surfaceSource;
    inOutSource += s_GeneratedIncludeDirectiveEnd;
}

static void AssignGeneratedMaterialShaderSource(const ScratchPath& sourcePath, ScratchArena& scratchArena, CookString& inOutSource){
    ScratchString sourceText = PathToString(scratchArena, sourcePath);
    for(char& ch : sourceText){
        if(ch == '\\')
            ch = '/';
    }
    inOutSource += sourceText;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared body for the three AVBOIT pass-PS generators; opaque materials are skipped.
static Expected<CookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitPassPixelShadersImpl(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const MaterialAvboitPassPixelShaderSpec& spec,
    CookArena& arena,
    ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    CookVector<GeneratedMaterialPixelShader> generatedShaders(arena);
    usize generatedShaderCount = 0u;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.transparent && !entry.surfaceSource.empty())
            ++generatedShaderCount;
    }
    generatedShaders.reserve(generatedShaderCount);

    const ScratchPath generatedRoot = ScratchPath(scratchArena, cacheDirectory) / configurationSafeName / s_GeneratedCacheDirectory / spec.generatedDirectoryLeaf;
    auto removeAllIfExistsResult2 = RemoveAllIfExists(generatedRoot);
    if(!removeAllIfExistsResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to clear generated directory '{}': {}"), ConvertString(spec.passLabel), PathToString<tchar>(generatedRoot), ConvertString(removeAllIfExistsResult2.error().message()));
        return MakeUnexpected(Failure{});
    }

    for(MaterialCookEntry& entry : materialEntries){
        if(!entry.transparent)
            continue;
        if(entry.surfaceSource.empty())
            continue;
        if(entry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: transparent material '{}' needs an interface to generate its AVBOIT {} pixel shader"), ConvertString(AStringView(entry.virtualPath)), ConvertString(spec.passLabel));
            return MakeUnexpected(Failure{});
        }

        // Same .bind + .surface pair as the G-buffer PS.
        ScratchString generatedSource(scratchArena);
        generatedSource += "// Generated per-material AVBOIT pass pixel shader: engine AVBOIT pass authoring + this material's\n";
        generatedSource += s_GeneratedPixelShaderBindHookLine;
        generatedSource += s_GeneratedPixelShaderCookCacheLine;
        AppendGeneratedMaterialShaderIncludes(spec.authoringHeaderInclude, entry, generatedSource);

        ScratchString relativeFile(scratchArena);
        relativeFile += entry.virtualPath;
        relativeFile += s_GeneratedShaderSourceExtension;
        const ScratchPath outputPath = generatedRoot / AStringView(relativeFile);
        auto ensureDirectoriesResult2 = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult2){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to create generated parent '{}': {}"),
                ConvertString(spec.passLabel),
                PathToString<tchar>(outputPath.parentPathView()),
                ConvertString(ensureDirectoriesResult2.error().message())
            );
            return MakeUnexpected(Failure{});
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to write generated pixel shader '{}'"), ConvertString(spec.passLabel), PathToString<tchar>(outputPath));
            return MakeUnexpected(Failure{});
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += spec.generatedNamePrefix;
        generated.name += entry.virtualPath;
        AssignGeneratedMaterialShaderSource(outputPath, scratchArena, generated.source);

        const auto pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName || !*pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated AVBOIT {} pixel shader name is invalid for material '{}'"), ConvertString(spec.passLabel), ConvertString(AStringView(entry.virtualPath)));
            return MakeUnexpected(Failure{});
        }

        // Record the PS identity so the clip collector and the transparent draw share the SAME key.
        (entry.*(spec.entryShaderNameField)).assign(AStringView(generated.name));

        generatedShaders.push_back(Move(generated));
    }

    return generatedShaders;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialPixelShaders(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const Name& sharedMeshShaderName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialCookDetail;
    CookVector<GeneratedMaterialPixelShader> generatedShaders(arena);
    usize generatedShaderCount = 0u;
    for(const MaterialCookEntry& entry : materialEntries){
        if(entry.stageShaders.empty() && !entry.surfaceSource.empty())
            ++generatedShaderCount;
    }
    generatedShaders.reserve(generatedShaderCount);

    if(generatedShaderCount > 0u && !sharedMeshShaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: missing shared mesh shader name"));
        return MakeUnexpected(Failure{});
    }

    const ScratchPath generatedRoot = ScratchPath(scratchArena, cacheDirectory) / configurationSafeName / s_GeneratedCacheDirectory / "material_pixel_shaders";
    auto removeAllIfExistsResult = RemoveAllIfExists(generatedRoot);
    if(!removeAllIfExistsResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to clear generated directory '{}': {}"), PathToString<tchar>(generatedRoot), ConvertString(removeAllIfExistsResult.error().message()));
        return MakeUnexpected(Failure{});
    }

    for(MaterialCookEntry& entry : materialEntries){
        const b8 hasExplicitShaders = !entry.stageShaders.empty();
        const b8 hasSurface = !entry.surfaceSource.empty();
        if(hasExplicitShaders){
            if(hasSurface){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' declares both 'surface' and 'shaders'; declare exactly one"), ConvertString(AStringView(entry.virtualPath)));
                return MakeUnexpected(Failure{});
            }
            continue;
        }
        if(!hasSurface){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' declares neither 'surface' nor 'shaders'"), ConvertString(AStringView(entry.virtualPath)));
            return MakeUnexpected(Failure{});
        }
        if(entry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' needs an interface to generate its pixel shader"), ConvertString(AStringView(entry.virtualPath)));
            return MakeUnexpected(Failure{});
        }

        // Engine authoring + .bind + surface hook; mesh stage is shared.
        ScratchString generatedSource(scratchArena);
        generatedSource += "// Generated per-material G-buffer pixel shader: engine pixel-shader authoring + this material's\n";
        generatedSource += s_GeneratedPixelShaderBindHookLine;
        generatedSource += s_GeneratedPixelShaderCookCacheLine;
        AppendGeneratedMaterialShaderIncludes("mesh/material_ps_authoring.slangi", entry, generatedSource);

        ScratchString relativeFile(scratchArena);
        relativeFile += entry.virtualPath;
        relativeFile += s_GeneratedShaderSourceExtension;
        const ScratchPath outputPath = generatedRoot / AStringView(relativeFile);
        auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to create generated parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult.error().message()));
            return MakeUnexpected(Failure{});
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to write generated pixel shader '{}'"), PathToString<tchar>(outputPath));
            return MakeUnexpected(Failure{});
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += "generated/material_ps/";
        generated.name += entry.virtualPath;
        AssignGeneratedMaterialShaderSource(outputPath, scratchArena, generated.source);

        const auto pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName || !*pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated pixel shader name is invalid for material '{}'"), ConvertString(AStringView(entry.virtualPath)));
            return MakeUnexpected(Failure{});
        }

        Core::Assets::AssetRef<IShader> pixelShaderRef;
        pixelShaderRef.virtualPath = **pixelShaderName;
        Core::Assets::AssetRef<IShader> meshShaderRef;
        meshShaderRef.virtualPath = sharedMeshShaderName;
        if(!entry.stageShaders.emplace(Core::ShaderType::PixelStage, pixelShaderRef).second || !entry.stageShaders.emplace(Core::ShaderType::MeshStage, meshShaderRef).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign generated stage shaders for material '{}'"), ConvertString(AStringView(entry.virtualPath)));
            return MakeUnexpected(Failure{});
        }

        generatedShaders.push_back(Move(generated));
    }

    return generatedShaders;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitAccumulatePixelShaders(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialCookDetail;
    return EmitMaterialAvboitPassPixelShadersImpl(cacheDirectory, configurationSafeName, s_AvboitAccumulatePassSpec, arena, scratchArena, materialEntries);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitOccupancyPixelShaders(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialCookDetail;
    return EmitMaterialAvboitPassPixelShadersImpl(cacheDirectory, configurationSafeName, s_AvboitOccupancyPassSpec, arena, scratchArena, materialEntries);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitExtinctionPixelShaders(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialCookDetail;
    return EmitMaterialAvboitPassPixelShadersImpl(cacheDirectory, configurationSafeName, s_AvboitExtinctionPassSpec, arena, scratchArena, materialEntries);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

