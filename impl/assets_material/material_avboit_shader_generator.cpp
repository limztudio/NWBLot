// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


#include "material_avboit_shader_generator.h"
#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


namespace MaterialAvboitShaderDetail{


using namespace MaterialCookDetail;


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


static void AssignGeneratedMaterialShaderSource(const ScratchPath& sourcePath, ScratchArena& scratchArena, CookString& inOutSource){
    ScratchString sourceText = PathToString(scratchArena, sourcePath);
    Replace(sourceText.begin(), sourceText.end(), '\\', '/');
    inOutSource += sourceText;
}


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
        if(!ValidateTransparentMaterialInterface(AStringView(entry.virtualPath), AStringView(entry.materialInterface), spec.passLabel))
            return MakeUnexpected(Failure{});

        // Same .bind + .surface pair as the G-buffer PS.
        CookString generatedSource(arena);
        BuildGeneratedMaterialPixelShaderSource(
            "// Generated per-material AVBOIT pass pixel shader: engine AVBOIT pass authoring + this material's\n",
            spec.authoringHeaderInclude,
            AStringView(entry.materialInterface),
            AStringView(entry.surfaceSource),
            generatedSource
        );

        ScratchString relativeFile(scratchArena);
        relativeFile += entry.virtualPath;
        relativeFile += s_GeneratedShaderSourceExtension;
        const ScratchPath outputPath = generatedRoot / AStringView(relativeFile);
        auto ensureDirectoriesResult2 = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult2){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Material AVBOIT {} pixel shader generation: failed to create generated parent '{}': {}")
                , ConvertString(spec.passLabel)
                , PathToString<tchar>(outputPath.parentPathView())
                , ConvertString(ensureDirectoriesResult2.error().message())
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


};


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> MaterialAvboitShaderGenerator::EmitAccumulate(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialAvboitShaderDetail;
    return EmitMaterialAvboitPassPixelShadersImpl(cacheDirectory, configurationSafeName, s_AvboitAccumulatePassSpec, arena, scratchArena, materialEntries);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> MaterialAvboitShaderGenerator::EmitOccupancy(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialAvboitShaderDetail;
    return EmitMaterialAvboitPassPixelShadersImpl(cacheDirectory, configurationSafeName, s_AvboitOccupancyPassSpec, arena, scratchArena, materialEntries);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookVector<GeneratedMaterialPixelShader>> MaterialAvboitShaderGenerator::EmitExtinction(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    MaterialCookArena& arena,
    Core::Alloc::ScratchArena& scratchArena,
    const Span<MaterialCookEntry> materialEntries
){
    using namespace MaterialAvboitShaderDetail;
    return EmitMaterialAvboitPassPixelShadersImpl(cacheDirectory, configurationSafeName, s_AvboitExtinctionPassSpec, arena, scratchArena, materialEntries);
}


NWB_IMPL_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

