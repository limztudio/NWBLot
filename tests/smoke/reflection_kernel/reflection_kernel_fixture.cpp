// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_kernel_fixture.h"

#include <impl/assets_shader/cook.h>
#include <impl/assets/graphics/reflection/spatial_constants.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ReflectionKernelTest::loadKernel(
    const AStringView kernelName,
    const u32 pushConstantBytes,
    Core::Alloc::ScratchArena& scratchArena,
    Core::ComputePipelineHandle& outPipeline,
    const Path* referenceSource,
    const u32 spatialRadius){
    const bool spatialKernel = kernelName == "spatial_cs";
    if(
        spatialRadius > NWB_REFLECTION_SPATIAL_MAX_RADIUS || (!spatialKernel && spatialRadius != 0u)
        || (referenceSource != nullptr && spatialRadius != 0u)
    )
        return false;
    constexpr AStringView radiusValues[] = { "0", "1", "2", "3" };
    constexpr AStringView radiusVariants[] = {
        "NWB_REFLECTION_SPATIAL_RADIUS=0", "NWB_REFLECTION_SPATIAL_RADIUS=1",
        "NWB_REFLECTION_SPATIAL_RADIUS=2", "NWB_REFLECTION_SPATIAL_RADIUS=3",
    };
    static_assert(LengthOf(radiusValues) == NWB_REFLECTION_SPATIAL_MAX_RADIUS + 1u);
    static_assert(LengthOf(radiusVariants) == LengthOf(radiusValues));
    const AStringView variant = spatialKernel ? radiusVariants[spatialRadius] : AStringView("default");
    const Impl::ShaderCook::ShaderMacroDefinition radiusDefinition{ "NWB_REFLECTION_SPATIAL_RADIUS", radiusValues[spatialRadius] };
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_REFLECTION_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "impl/assets/graphics/reflection";
    AString<Core::Alloc::ScratchArena> sourceName(kernelName, scratchArena);
    sourceName += ".slang";
    AString<Core::Alloc::ScratchArena> metadataName(kernelName, scratchArena);
    metadataName += ".nwb";
    AString<Core::Alloc::ScratchArena> outputName(kernelName, scratchArena);
    if(referenceSource != nullptr)
        outputName += "_reference.spv";
    else{
        if(spatialRadius != 0u){
            outputName += "_radius_";
            outputName += radiusValues[spatialRadius];
        }
        outputName += ".spv";
    }
    const Path sourcePath = referenceSource != nullptr ? *referenceSource : kernelRoot / sourceName.c_str();
    const Path metadataPath = kernelRoot / metadataName.c_str();
    const Path outputRoot(memoryArena, NWB_REFLECTION_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    const Path outputPath = outputRoot / outputName.c_str();
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
        return false;
    if(
        entry.stage.view() != "cs"
        || entry.targetProfile.view() != "spirv_1_5"
        || (!spatialKernel && !entry.defineValues.empty())
        || !entry.implicitDefines.empty()
        || entry.includeRoots.size() != 1u
        || entry.includeRoots[0] != "engine/graphics"
    )
        return false;
    if(spatialKernel){
        if(entry.defineValues.size() != 1u)
            return false;
        const auto& radiusEntry = *entry.defineValues.begin();
        if(
            AStringView(radiusEntry.first.data(), radiusEntry.first.size()) != "NWB_REFLECTION_SPATIAL_RADIUS"
            || radiusEntry.second.values.size() != LengthOf(radiusValues)
        )
            return false;
        for(usize index = 0u; index < LengthOf(radiusValues); ++index){
            const auto& value = radiusEntry.second.values[index];
            if(AStringView(value.data(), value.size()) != radiusValues[index])
                return false;
        }
        if(!shaderCook.validateVariantSignature(kernelName, variant, entry.defineValues, scratchArena))
            return false;
    }
    Impl::ShaderCook::CookVector<Path> includes(memoryArena);
    includes.push_back(sourceRoot / "impl/assets/graphics");
    includes.push_back(kernelRoot);
    Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
    if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
        return false;
    const Impl::ShaderCook::ShaderCompilerRequest request{
        .shaderName = kernelName,
        .stage = entry.stage.view(),
        .targetProfile = entry.targetProfile.view(),
        .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
        .variantName = variant,
        .defines = spatialKernel ? &radiusDefinition : nullptr,
        .includeDirectories = includes,
        .dependencies = dependencies,
        .sourcePath = sourcePath,
        .outputPath = outputPath,
        .defineCount = spatialKernel ? 1u : 0u,
        .optimizationLevel = entry.optimizationLevel
    };
    Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
    if(!shaderCook.compileVariant(request, bytecode) || bytecode.empty())
        return false;
    ShaderDesc shaderDesc(memoryArena);
    shaderDesc.setShaderType(ShaderType::Compute).setEntryName(request.entryPoint);
    const ShaderHandle shader = graphicsDevice.createShader(shaderDesc, bytecode.data(), bytecode.size());
    if(!shader)
        return false;
    BindingLayoutDesc layoutDesc(memoryArena);
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, pushConstantBytes));
    const BindingLayoutHandle layout = graphicsDevice.createBindingLayout(layoutDesc);
    if(!layout)
        return false;
    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(layout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    outPipeline = graphicsDevice.createComputePipeline(pipelineDesc);
    return outPipeline.get() != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

