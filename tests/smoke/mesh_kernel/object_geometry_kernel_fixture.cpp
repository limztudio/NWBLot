// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "object_geometry_kernel_fixture.h"

#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LoadObjectGeometryKernels(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    Alloc::ScratchArena& scratchArena,
    ObjectGeometryKernels& kernels){
    const Path sourceRoot(arena, NWB_MESH_KERNEL_SOURCE_ROOT);
    const Path graphicsRoot = sourceRoot / "impl/assets/graphics";
    const Path meshRoot = graphicsRoot / "mesh";
    const Path outputRoot(arena, NWB_MESH_KERNEL_OUTPUT_ROOT);
    ErrorCode directoryError;
    if(!CreateDirectories(outputRoot, directoryError) && directoryError)
        return false;
    const Path sources[] = {
        meshRoot / "object_decode_cs.slang",
        sourceRoot / "tests/smoke/mesh_kernel/assets/object_transform_cs.slang",
        meshRoot / "object_vs.slang"
    };
    constexpr AStringView names[] = { "object_decode", "object_transform", "object_vertex" };
    constexpr u32 pushBytes[] = {
        NWB_MESH_COMPUTE_PUSH_CONSTANT_BYTE_SIZE,
        NWB_MESH_COMPUTE_PUSH_CONSTANT_BYTE_SIZE, NWB_MESH_PUSH_CONSTANT_BYTE_SIZE
    };
    ComputePipelineHandle* const pipelines[] = { &kernels.decode, &kernels.transform };
    Impl::ShaderCook shaderCook(arena);
    Impl::ShaderCook::CookVector<Path> includes(arena);
    includes.push_back(graphicsRoot);
    includes.push_back(meshRoot);
    auto& heap = device.getDescriptorHeap();
    for(u32 stage = 0u; stage < LengthOf(sources); ++stage){
        Impl::ShaderCook::CookVector<Path> dependencies(arena);
        if(!shaderCook.gatherShaderDependencies(sources[stage], includes, dependencies, scratchArena))
            return false;
        const Path output = outputRoot / names[stage];
        const bool vertexStage = stage == 2u;
        const Impl::ShaderCook::ShaderCompilerRequest request{
            .shaderName = names[stage],
            .stage = vertexStage ? "vs" : "cs",
            .targetProfile = "spirv_1_5",
            .entryPoint = "main",
            .variantName = "default",
            .includeDirectories = includes,
            .dependencies = dependencies,
            .sourcePath = sources[stage],
            .outputPath = output,
        };
        Impl::ShaderCook::CookVector<u8> bytecode(arena);
        if(!shaderCook.compileVariant(request, bytecode) || bytecode.empty())
            return false;
        ShaderDesc shaderDesc(arena);
        shaderDesc.setShaderType(vertexStage ? ShaderType::Vertex : ShaderType::Compute).setEntryName("main");
        const ShaderHandle shader = device.createShader(shaderDesc, bytecode.data(), bytecode.size());
        if(!shader)
            return false;
        if(vertexStage){
            kernels.vertex = shader;
            continue;
        }
        BindingLayoutDesc layoutDesc(arena);
        layoutDesc.setVisibility(ShaderType::Compute).addItem(BindingLayoutItem::PushConstants(0u, pushBytes[stage]));
        const BindingLayoutHandle layout = device.createBindingLayout(layoutDesc);
        if(!layout)
            return false;
        ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(shader)
            .addBindingLayout(layout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        *pipelines[stage] = device.createComputePipeline(pipelineDesc);
        if(!*pipelines[stage])
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

