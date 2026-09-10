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


bool EmitMaterialPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView sharedMeshShaderName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
){
    outGenerated.clear();

    const Name sharedMeshShaderNameId = ToName(sharedMeshShaderName);
    if(!sharedMeshShaderNameId){
        NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: invalid shared mesh shader name '{}'")
            , StringConvert(sharedMeshShaderName)
        );
        return false;
    }

    ScratchString configurationName(configurationSafeName, scratchArena);
    const Path generatedRoot = cacheDirectory / configurationName.c_str() / "generated" / "material_pixel_shaders";
    ErrorCode errorCode;
    if(!RemoveAllIfExists(generatedRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to clear generated directory '{}': {}")
            , PathToString<tchar>(generatedRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        const bool hasExplicitShaders = !entry.stageShaders.empty();
        const bool hasSurface = !entry.surfaceSource.empty();
        if(hasExplicitShaders){
            if(hasSurface){
                NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' declares both 'surface' and 'shaders'; declare exactly one")
                    , StringConvert(entry.virtualPath.c_str())
                );
                return false;
            }
            continue;
        }
        if(!hasSurface){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' declares neither 'surface' nor 'shaders'")
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }
        if(entry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: material '{}' needs an interface to generate its pixel shader")
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
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
        const Path outputPath = generatedRoot / relativeFile.c_str();
        errorCode.clear();
        if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to create generated parent '{}': {}")
                , PathToString<tchar>(outputPath.parent_path())
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material pixel shader generation: failed to write generated pixel shader '{}'")
                , PathToString<tchar>(outputPath)
            );
            return false;
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += "generated/material_ps/";
        generated.name += entry.virtualPath;
        AssignGeneratedMaterialShaderSource(generated.source, scratchArena, outputPath);

        const Name pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated pixel shader name is invalid for material '{}'")
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }

        Core::Assets::AssetRef<Shader> pixelShaderRef;
        pixelShaderRef.virtualPath = pixelShaderName;
        Core::Assets::AssetRef<Shader> meshShaderRef;
        meshShaderRef.virtualPath = sharedMeshShaderNameId;
        if(!entry.stageShaders.emplace(Core::ShaderType::PixelStage, pixelShaderRef).second
            || !entry.stageShaders.emplace(Core::ShaderType::MeshStage, meshShaderRef).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: failed to assign generated stage shaders for material '{}'")
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }

        outGenerated.push_back(Move(generated));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared body for the three AVBOIT pass-PS generators; opaque materials are skipped.
static bool EmitMaterialAvboitPassPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const AStringView generatedDirectoryLeaf,
    const AStringView authoringHeaderInclude,
    const AStringView passLabel,
    const AStringView generatedNamePrefix,
    MaterialCookString MaterialCookEntry::* entryShaderNameField,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
    ScratchArena& scratchArena
){
    outGenerated.clear();

    ScratchString configurationName(configurationSafeName, scratchArena);
    const Path generatedRoot = cacheDirectory / configurationName.c_str() / "generated" / ScratchString(generatedDirectoryLeaf, scratchArena).c_str();
    ErrorCode errorCode;
    if(!RemoveAllIfExists(generatedRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to clear generated directory '{}': {}")
            , StringConvert(passLabel)
            , PathToString<tchar>(generatedRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    for(MaterialCookEntry& entry : materialEntries){
        if(!entry.transparent)
            continue;
        if(entry.surfaceSource.empty())
            continue;
        if(entry.materialInterface.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: transparent material '{}' needs an interface to generate its AVBOIT {} pixel shader")
                , StringConvert(entry.virtualPath.c_str())
                , StringConvert(passLabel)
            );
            return false;
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
        const Path outputPath = generatedRoot / relativeFile.c_str();
        errorCode.clear();
        if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to create generated parent '{}': {}")
                , StringConvert(passLabel)
                , PathToString<tchar>(outputPath.parent_path())
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to write generated pixel shader '{}'")
                , StringConvert(passLabel)
                , PathToString<tchar>(outputPath)
            );
            return false;
        }

        GeneratedMaterialPixelShader generated(arena);
        generated.name += generatedNamePrefix;
        generated.name += entry.virtualPath;
        AssignGeneratedMaterialShaderSource(generated.source, scratchArena, outputPath);

        const Name pixelShaderName = ToName(AStringView(generated.name));
        if(!pixelShaderName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material cook: generated AVBOIT {} pixel shader name is invalid for material '{}'")
                , StringConvert(passLabel)
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }

        // Record the generated PS identity so the CSG clip-variant collector can give it AVBOIT CSG clip variants
        // and the renderer can bind it for the transparent draw, both keyed by the SAME name.
        (entry.*entryShaderNameField).assign(AStringView(generated.name));

        outGenerated.push_back(Move(generated));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitMaterialAvboitAccumulatePixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
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
        outGenerated,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitMaterialAvboitOccupancyPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
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
        outGenerated,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitMaterialAvboitExtinctionPixelShadersImpl(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    CookVector<MaterialCookEntry>& materialEntries,
    CookVector<GeneratedMaterialPixelShader>& outGenerated,
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
        outGenerated,
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

