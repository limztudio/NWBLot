// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_kernel_fixture.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShadowKernelTest::loadTraversalKernel(Core::Alloc::ScratchArena& scratchArena, Core::ComputePipelineHandle& outPipeline){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_SHADOW_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "tests/smoke/shadow_kernel/assets";
    const Path sourcePath = kernelRoot / "traversal_cs.slang";
    const Path metadataPath = kernelRoot / "traversal_cs.nwb";
    const Path outputRoot(memoryArena, NWB_SHADOW_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    const Path outputPath = outputRoot / "traversal_cs.spv";
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
    {
        const Core::Common::LoggerRegistrationGuard cookLogger(*s_logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
        const bool cooked = [&]{
            if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
                return false;
            Impl::ShaderCook::CookVector<Path> includes(memoryArena);
            includes.push_back(kernelRoot);
            includes.push_back(sourceRoot / "impl/assets/graphics");
            // The dependency scanner follows disabled CSG branches; use the production empty-shape module generator.
            const Impl::AssetsCsgCook::CsgShapeCookEntryVector noShapes(memoryArena);
            Path generatedCsgRoot(memoryArena);
            if(!Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(outputRoot, "traversal", noShapes, generatedCsgRoot, scratchArena))
                return false;
            includes.push_back(generatedCsgRoot);
            Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
            if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
                return false;
            const Impl::ShaderCook::ShaderCompilerRequest request{
                .shaderName = "tests/shadow_kernel/traversal_cs",
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
            return shaderCook.compileVariant(request, bytecode) && !bytecode.empty();
        }();
        if(!cooked){
            s_logger->emitErrorsToStderr();
            return false;
        }
    }
    ShaderDesc shaderDesc(memoryArena);
    shaderDesc.setShaderType(ShaderType::Compute).setEntryName(AStringView(entry.entryPoint.data(), entry.entryPoint.size()));
    const ShaderHandle shader = graphicsDevice.createShader(shaderDesc, bytecode.data(), bytecode.size());
    if(!shader)
        return false;
    BindingLayoutDesc layoutDesc(memoryArena);
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, 12u));
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

