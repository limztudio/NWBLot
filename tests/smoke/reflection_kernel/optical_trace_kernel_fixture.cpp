// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_trace_kernel_fixture.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpticalTraceKernelTest::SetUp(){
    if(
        !device().queryFeatureSupport(Feature::RayTracingAccelStruct)
        || !device().queryFeatureSupport(Feature::RayQuery) || !device().getDescriptorHeap().hasAccelStructLayout()
    )
        GTEST_SKIP() << "Native optical traversal tests require acceleration structures and hardware ray queries.";
}

bool OpticalTraceKernelTest::loadTraceKernel(
    const bool closedMedia,
    Alloc::ScratchArena& scratchArena,
    ComputePipelineHandle& outPipeline){
    constexpr AStringView name = "optical_trace_cs";
    const AStringView variant = closedMedia ? "NWB_OPTICAL_CLOSED_MEDIA=1" : "NWB_OPTICAL_CLOSED_MEDIA=0";
    const Impl::ShaderCook::ShaderMacroDefinition definition{ "NWB_OPTICAL_CLOSED_MEDIA", closedMedia ? "1" : "0" };
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_REFLECTION_KERNEL_SOURCE_ROOT);
    const Path kernelRoot = sourceRoot / "tests/smoke/reflection_kernel/assets";
    Path sourcePath = kernelRoot / name;
    sourcePath.replace_extension(".slang");
    Path metadataPath = kernelRoot / name;
    metadataPath.replace_extension(".nwb");
    const Path outputRoot(memoryArena, NWB_REFLECTION_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    Path outputPath = outputRoot / (closedMedia ? "optical_trace_general.spv" : "optical_trace_unspecified.spv");
    outputPath.replace_extension(".spv");
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    Impl::ShaderCook::CookVector<u8> bytecode(memoryArena);
    {
        const Common::LoggerRegistrationGuard cookLogger(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
        const bool cooked = [&]{
            if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
                return false;
            if(!shaderCook.validateVariantSignature(name, variant, entry.defineValues, scratchArena))
                return false;
            Impl::ShaderCook::CookVector<Path> includes(memoryArena);
            includes.push_back(kernelRoot);
            includes.push_back(sourceRoot / "impl/assets/graphics");
            const Impl::AssetsCsgCook::CsgShapeCookEntryVector noShapes(memoryArena);
            Path generatedCsgRoot(memoryArena);
            if(!Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(
                outputRoot, "optical_trace", noShapes, generatedCsgRoot, scratchArena
            ))
                return false;
            includes.push_back(generatedCsgRoot);
            Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
            if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
                return false;
            const Impl::ShaderCook::ShaderCompilerRequest request{
                .shaderName = name,
                .stage = entry.stage.view(),
                .targetProfile = entry.targetProfile.view(),
                .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
                .variantName = variant,
                .defines = &definition,
                .includeDirectories = includes,
                .dependencies = dependencies,
                .sourcePath = sourcePath,
                .outputPath = outputPath,
                .defineCount = 1u,
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
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, 16u));
    const BindingLayoutHandle layout = graphicsDevice.createBindingLayout(layoutDesc);
    if(!layout)
        return false;
    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(layout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    outPipeline = graphicsDevice.createComputePipeline(pipelineDesc);
    return outPipeline.get() != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

