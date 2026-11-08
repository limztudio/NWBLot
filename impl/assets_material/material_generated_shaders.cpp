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


static void AssignGeneratedMaterialShaderSource(const ScratchPath& sourcePath, ScratchArena& scratchArena, CookString& inOutSource){
    ScratchString sourceText = PathToString(scratchArena, sourcePath);
    Replace(sourceText.begin(), sourceText.end(), '\\', '/');
    inOutSource += sourceText;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





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
        if(!ValidateMaterialGeneratedPixelShaderEntry(
            AStringView(entry.virtualPath),
            hasExplicitShaders,
            hasSurface,
            AStringView(entry.materialInterface)
        ))
            return MakeUnexpected(Failure{});
        if(hasExplicitShaders)
            continue;

        // Engine authoring + .bind + surface hook; mesh stage is shared.
        CookString generatedSource(arena);
        BuildGeneratedMaterialPixelShaderSource(
            "// Generated per-material G-buffer pixel shader: engine pixel-shader authoring + this material's\n",
            "mesh/material_ps_authoring.slangi",
            AStringView(entry.materialInterface),
            AStringView(entry.surfaceSource),
            generatedSource
        );

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





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
