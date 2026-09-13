// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_kernel_fixture.h"

#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets_csg/cook.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshKernelTest::loadMeshKernel(
    const bool candidate,
    Core::Alloc::ScratchArena& scratchArena,
    Core::ComputePipelineHandle& outPipeline){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    auto& memoryArena = arena();
    const Path sourceRoot(memoryArena, NWB_MESH_KERNEL_SOURCE_ROOT);
    const Path graphicsRoot = sourceRoot / "impl/assets/graphics";
    const Path kernelRoot = graphicsRoot / "mesh";
    const Path overlayRoot = sourceRoot / "tests/smoke/mesh_kernel/reference";
    // The reference retains the old entrypoint; the candidate always follows the current production source tree.
    const Path sourcePath = kernelRoot / "shared_ms.slang";
    const Path metadataPath = kernelRoot / "shared_ms.nwb";
    const Path entrypointPath = candidate ? kernelRoot / "entrypoints.slangi" : overlayRoot / "mesh/entrypoints.slangi";
    const Path authoringPath = candidate ? kernelRoot / "authoring.slangi" : overlayRoot / "mesh/authoring.slangi";
    const Path excludedEntrypoint = candidate ? overlayRoot / "mesh/entrypoints.slangi" : kernelRoot / "entrypoints.slangi";
    const Path outputRoot(memoryArena, NWB_MESH_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    const Path outputPath = outputRoot / (candidate ? "mesh_candidate.spv" : "mesh_reference.spv");
    Impl::ShaderCook shaderCook(memoryArena);
    Impl::ShaderCook::ShaderEntry entry(memoryArena);
    if(!shaderCook.parseShaderMeta(metadataPath, entry, scratchArena))
        return false;
    if(
        entry.stage.view() != "mesh"
        || entry.targetProfile.view() != "spirv_1_5"
        || AStringView(entry.entryPoint.data(), entry.entryPoint.size()) != "main"
        || !entry.defineValues.empty()
        || !entry.implicitDefines.empty()
        || entry.includeRoots.size() != 1u
        || entry.includeRoots[0] != "engine/graphics"
    )
        return false;
    Impl::ShaderCook::CookVector<Path> includes(memoryArena);
    if(!candidate)
        includes.push_back(overlayRoot);
    includes.push_back(graphicsRoot);
    includes.push_back(kernelRoot);
    // The dependency scanner follows disabled branches. Use the real empty-shape module generator in private output.
    const Impl::AssetsCsgCook::CsgShapeCookEntryVector noShapes(memoryArena);
    Path generatedCsgRoot(memoryArena);
    if(!Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(
        outputRoot, candidate ? "candidate" : "reference", noShapes, generatedCsgRoot, scratchArena
    ))
        return false;
    includes.push_back(generatedCsgRoot);
    Impl::ShaderCook::CookVector<Path> dependencies(memoryArena);
    if(!shaderCook.gatherShaderDependencies(sourcePath, includes, dependencies, scratchArena))
        return false;
    bool selectedEntrypoint = false;
    bool selectedAuthoring = false;
    for(const Path& dependency : dependencies){
        const Path normalized = dependency.lexically_normal();
        selectedEntrypoint |= normalized == entrypointPath.lexically_normal();
        selectedAuthoring |= normalized == authoringPath.lexically_normal();
        if(normalized == excludedEntrypoint.lexically_normal())
            return false;
    }
    if(!selectedEntrypoint || !selectedAuthoring)
        return false;
    const Impl::ShaderCook::ShaderMacroDefinition definitions[] = {
        { "NWB_MESH_SHADER_EMULATION_COMPUTE", "1" },
        { "NWB_CSG_ENABLED", "0" },
    };
    const Impl::ShaderCook::ShaderCompilerRequest request{
        .shaderName = "shared_ms",
        .stage = "cs",
        .targetProfile = entry.targetProfile.view(),
        .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
        .variantName = "NWB_CSG_ENABLED=0;NWB_MESH_SHADER_EMULATION_COMPUTE=1",
        .defines = definitions,
        .includeDirectories = includes,
        .dependencies = dependencies,
        .sourcePath = sourcePath,
        .outputPath = outputPath,
        .defineCount = LengthOf(definitions),
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
    layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, NWB_MESH_PUSH_CONSTANT_BYTE_SIZE));
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

