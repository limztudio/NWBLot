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


// Cook-private AVBOIT PS prefixes; the renderer binds the stored Name.
static constexpr AStringView s_AvboitAccumulatePixelShaderGeneratedPrefix("generated/avboit_accumulate_ps/");
static constexpr AStringView s_AvboitOccupancyPixelShaderGeneratedPrefix("generated/avboit_occupancy_ps/");
static constexpr AStringView s_AvboitExtinctionPixelShaderGeneratedPrefix("generated/avboit_extinction_ps/");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendGeneratedMaterialShaderIncludes(
    CookString& outSource,
    const AStringView authoringHeaderInclude,
    const MaterialCookEntry& entry
){
    outSource += "#include \"";
    outSource += authoringHeaderInclude;
    outSource += "\"\n";
    outSource += "#include \"";
    outSource += entry.materialInterface;
    outSource += ".bind\"\n";
    outSource += "#include \"";
    outSource += entry.surfaceSource;
    outSource += "\"\n";
}

static void AssignGeneratedMaterialShaderSource(
    CookString& outSource,
    ScratchArena& scratchArena,
    const Path& sourcePath
){
    ScratchString sourceText = PathToString(scratchArena, sourcePath);
    for(char& ch : sourceText){
        if(ch == '\\')
            ch = '/';
    }
    outSource += sourceText;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CookVector<GeneratedMaterialPixelShader>> EmitMaterialPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView sharedMeshShaderName,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    CookVector<GeneratedMaterialPixelShader> generatedShaders(arena);

    const Name sharedMeshShaderNameId = ToName(sharedMeshShaderName);
    if(!sharedMeshShaderNameId){
        NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: invalid shared mesh shader name '{}'")
            , StringConvert(sharedMeshShaderName)
        );
        return MakeUnexpected(Failure{});
    }

    const Path generatedRoot = cacheDirectory / configurationSafeName / "generated" / "material_pixel_shaders";
    auto removeAllIfExistsResult = RemoveAllIfExists(generatedRoot);
    if(!removeAllIfExistsResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to clear generated directory '{}': {}")
            , PathToString<tchar>(generatedRoot)
            , StringConvert(removeAllIfExistsResult.error().message())
        );
        return MakeUnexpected(Failure{});
    }

    for(MaterialCookEntry& entry : materialEntries){
        const bool hasExplicitShaders = !entry.stageShaders.empty();
        const bool hasSurface = !entry.surfaceSource.empty();
        if(hasExplicitShaders){
            if(hasSurface){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' declares both 'surface' and 'shaders'; declare exactly one")
                    , StringConvert(AStringView(entry.virtualPath))
                );
                return MakeUnexpected(Failure{});
            }
            continue;
        }
        if(!hasSurface){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' declares neither 'surface' nor 'shaders'")
                , StringConvert(AStringView(entry.virtualPath))
            );
            return MakeUnexpected(Failure{});
        }
        if(entry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' needs an interface to generate its pixel shader")
                , StringConvert(AStringView(entry.virtualPath))
            );
            return MakeUnexpected(Failure{});
        }

        // Engine authoring + .bind + surface hook; mesh stage is shared.
        CookString generatedSource(arena);
        generatedSource += "// Generated per-material G-buffer pixel shader: engine pixel-shader authoring + this material's\n";
        generatedSource += "// typed .bind + its surface hook. The material declares only its 'surface' fragment; the cook\n";
        generatedSource += "// assembles this here, in the cook cache. Do not edit -- regenerated every cook.\n";
        AppendGeneratedMaterialShaderIncludes(generatedSource, "mesh/material_ps_authoring.slangi", entry);

        CookString relativeFile(arena);
        relativeFile += entry.virtualPath;
        relativeFile += ".slang";
        const Path outputPath = generatedRoot / AStringView(relativeFile);
        auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to create generated parent '{}': {}")
                , PathToString<tchar>(outputPath.parentPath())
                , StringConvert(ensureDirectoriesResult.error().message())
            );
            return MakeUnexpected(Failure{});
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to write generated pixel shader '{}'")
                , PathToString<tchar>(outputPath)
            );
            return MakeUnexpected(Failure{});
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += "generated/material_ps/";
        generated.name += entry.virtualPath;
        AssignGeneratedMaterialShaderSource(generated.source, scratchArena, outputPath);

        const Name pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated pixel shader name is invalid for material '{}'")
                , StringConvert(AStringView(entry.virtualPath))
            );
            return MakeUnexpected(Failure{});
        }

        Core::Assets::AssetRef<IShader> pixelShaderRef;
        pixelShaderRef.virtualPath = pixelShaderName;
        Core::Assets::AssetRef<IShader> meshShaderRef;
        meshShaderRef.virtualPath = sharedMeshShaderNameId;
        if(!entry.stageShaders.emplace(Core::ShaderType::PixelStage, pixelShaderRef).second
            || !entry.stageShaders.emplace(Core::ShaderType::MeshStage, meshShaderRef).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign generated stage shaders for material '{}'")
                , StringConvert(AStringView(entry.virtualPath))
            );
            return MakeUnexpected(Failure{});
        }

        generatedShaders.push_back(Move(generated));
    }

    return generatedShaders;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared body for the three AVBOIT pass-PS generators; opaque materials are skipped.
static Expected<CookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitPassPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView generatedDirectoryLeaf,
    const AStringView authoringHeaderInclude,
    const AStringView passLabel,
    const AStringView generatedNamePrefix,
    MaterialCookString MaterialCookEntry::* entryShaderNameField,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    CookVector<GeneratedMaterialPixelShader> generatedShaders(arena);

    const Path generatedRoot = cacheDirectory / configurationSafeName / "generated" / generatedDirectoryLeaf;
    auto removeAllIfExistsResult2 = RemoveAllIfExists(generatedRoot);
    if(!removeAllIfExistsResult2){
        NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to clear generated directory '{}': {}")
            , StringConvert(passLabel)
            , PathToString<tchar>(generatedRoot)
            , StringConvert(removeAllIfExistsResult2.error().message())
        );
        return MakeUnexpected(Failure{});
    }

    for(MaterialCookEntry& entry : materialEntries){
        if(!entry.transparent)
            continue;
        if(entry.surfaceSource.empty())
            continue;
        if(entry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: transparent material '{}' needs an interface to generate its AVBOIT {} pixel shader")
                , StringConvert(AStringView(entry.virtualPath))
                , StringConvert(passLabel)
            );
            return MakeUnexpected(Failure{});
        }

        // Same .bind + .surface pair as the G-buffer PS.
        CookString generatedSource(arena);
        generatedSource += "// Generated per-material AVBOIT pass pixel shader: engine AVBOIT pass authoring + this material's\n";
        generatedSource += "// typed .bind + its surface hook. The material declares only its 'surface' fragment; the cook\n";
        generatedSource += "// assembles this here, in the cook cache. Do not edit -- regenerated every cook.\n";
        AppendGeneratedMaterialShaderIncludes(generatedSource, authoringHeaderInclude, entry);

        CookString relativeFile(arena);
        relativeFile += entry.virtualPath;
        relativeFile += ".slang";
        const Path outputPath = generatedRoot / AStringView(relativeFile);
        auto ensureDirectoriesResult2 = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult2){
            NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to create generated parent '{}': {}")
                , StringConvert(passLabel)
                , PathToString<tchar>(outputPath.parentPath())
                , StringConvert(ensureDirectoriesResult2.error().message())
            );
            return MakeUnexpected(Failure{});
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to write generated pixel shader '{}'")
                , StringConvert(passLabel)
                , PathToString<tchar>(outputPath)
            );
            return MakeUnexpected(Failure{});
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += generatedNamePrefix;
        generated.name += entry.virtualPath;
        AssignGeneratedMaterialShaderSource(generated.source, scratchArena, outputPath);

        const Name pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated AVBOIT {} pixel shader name is invalid for material '{}'")
                , StringConvert(passLabel)
                , StringConvert(AStringView(entry.virtualPath))
            );
            return MakeUnexpected(Failure{});
        }

        // Record the generated PS identity so the CSG clip-variant collector can give it AVBOIT CSG clip variants
        // and the renderer can bind it for the transparent draw, both keyed by the SAME name.
        (entry.*entryShaderNameField).assign(AStringView(generated.name));

        generatedShaders.push_back(Move(generated));
    }

    return generatedShaders;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitAccumulatePixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    return EmitMaterialAvboitPassPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        AStringView("material_avboit_accumulate_pixel_shaders"),
        AStringView("avboit/accumulate_ps_authoring.slangi"),
        AStringView("accumulate"),
        s_AvboitAccumulatePixelShaderGeneratedPrefix,
        &MaterialCookEntry::avboitAccumulatePixelShaderName,
        materialEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitOccupancyPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    return EmitMaterialAvboitPassPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        AStringView("material_avboit_occupancy_pixel_shaders"),
        AStringView("avboit/occupancy_ps_authoring.slangi"),
        AStringView("occupancy"),
        s_AvboitOccupancyPixelShaderGeneratedPrefix,
        &MaterialCookEntry::avboitOccupancyPixelShaderName,
        materialEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> EmitMaterialAvboitExtinctionPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    ScratchArena& scratchArena
){
    return EmitMaterialAvboitPassPixelShadersImpl(
        arena,
        cacheDirectory,
        configurationSafeName,
        AStringView("material_avboit_extinction_pixel_shaders"),
        AStringView("avboit/extinction_ps_authoring.slangi"),
        AStringView("extinction"),
        s_AvboitExtinctionPixelShaderGeneratedPrefix,
        &MaterialCookEntry::avboitExtinctionPixelShaderName,
        materialEntries,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

