// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_kernel_fixture.h"

#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CausticKernelTest::loadResolveKernel(
    const bool reference,
    const u32 stage,
    Core::Alloc::ScratchArena& scratchArena,
    Core::ComputePipelineHandle& outPipeline,
    const bool directWavelet){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_CAUSTIC_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "impl/assets/graphics/caustic";
    const AStringView kernelName = "caustic_resolve_cs";
    constexpr AStringView s_StageValues[] = { "3", "1", "2", "4" };
    constexpr AStringView s_StageVariants[] = {
        "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=3",
        "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=1",
        "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=2",
        "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE=4"
    };
    constexpr AStringView s_StageOutputs[] = {
        "caustic_resolve_prepare.spv", "caustic_resolve_wavelet.spv", "caustic_resolve_upsample.spv",
        "caustic_resolve_wavelet_direct.spv"
    };
    if(stage > 2u || (directWavelet && (reference || stage != 1u)))
        return false;
    const u32 variantIndex = directWavelet ? 3u : stage;
    const bool waveletReference = stage == 1u;
    const Path sourcePath = reference
        ? sourceRoot / (waveletReference
            ? "tests/smoke/caustic_kernel/reference/caustic_resolve_cs.slang"
            : "tests/smoke/caustic_kernel/reference/caustic_resolve_stages_6c9207d03.slang")
        : kernelRoot / "caustic_resolve_cs.slang";
    const Path metadataPath = kernelRoot / "caustic_resolve_cs.nwb";
    const Path outputRoot(memoryArena, NWB_CAUSTIC_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    const AStringView referenceOutput = waveletReference ? "caustic_resolve_reference.spv" : "caustic_resolve_stages_reference.spv";
    const Path outputPath = outputRoot / (reference ? referenceOutput : s_StageOutputs[variantIndex]);
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
        return false;
    if(
        entry.stage.view() != "cs"
        || entry.targetProfile.view() != "spirv_1_5"
        || AStringView(entry.entryPoint.data(), entry.entryPoint.size()) != "main"
        || entry.defineValues.size() != 1u
        || !entry.implicitDefines.empty()
        || entry.includeRoots.size() != 1u
        || entry.includeRoots[0] != "engine/graphics"
    )
        return false;
    const auto& stageEntry = *entry.defineValues.begin();
    if(
        AStringView(stageEntry.first.data(), stageEntry.first.size()) != "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE"
        || stageEntry.second.values.size() != LengthOf(s_StageValues)
    )
        return false;
    for(usize index = 0u; index < LengthOf(s_StageValues); ++index){
        const auto& value = stageEntry.second.values[index];
        if(AStringView(value.data(), value.size()) != s_StageValues[index])
            return false;
    }
    const AStringView variant = reference ? AStringView("default") : s_StageVariants[variantIndex];
    if(!reference && !shaderCook.validateVariantSignature(kernelName, variant, entry.defineValues, scratchArena))
        return false;
    const Impl::ShaderCook::ShaderMacroDefinition definition{ "NWB_CAUSTIC_RESOLVE_COMPILED_STAGE", s_StageValues[variantIndex] };
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
        .defines = reference ? nullptr : &definition,
        .includeDirectories = includes,
        .dependencies = dependencies,
        .sourcePath = sourcePath,
        .outputPath = outputPath,
        .defineCount = reference ? 0u : 1u,
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
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, 56u));
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

