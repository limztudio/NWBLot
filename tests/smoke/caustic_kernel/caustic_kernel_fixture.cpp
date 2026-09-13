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
    Core::Alloc::ScratchArena& scratchArena,
    Core::ComputePipelineHandle& outPipeline){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_CAUSTIC_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "impl/assets/graphics/caustic";
    const AStringView kernelName = "caustic_resolve_cs";
    const Path sourcePath = reference
        ? sourceRoot / "tests/smoke/caustic_kernel/reference/caustic_resolve_cs.slang"
        : kernelRoot / "caustic_resolve_cs.slang";
    const Path metadataPath = kernelRoot / "caustic_resolve_cs.nwb";
    const Path outputRoot(memoryArena, NWB_CAUSTIC_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    const Path outputPath = outputRoot / (reference ? "caustic_resolve_reference.spv" : "caustic_resolve.spv");
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
        return false;
    if(
        entry.stage.view() != "cs"
        || entry.targetProfile.view() != "spirv_1_5"
        || AStringView(entry.entryPoint.data(), entry.entryPoint.size()) != "main"
        || !entry.defineValues.empty()
        || !entry.implicitDefines.empty()
        || entry.includeRoots.size() != 1u
        || entry.includeRoots[0] != "engine/graphics"
    )
        return false;
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
        .variantName = "default",
        .defines = nullptr,
        .includeDirectories = includes,
        .dependencies = dependencies,
        .sourcePath = sourcePath,
        .outputPath = outputPath,
        .defineCount = 0u,
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

