// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets/graphics/skinned_mesh/binding_slots.h>
#include <impl/assets/graphics/skinned_mesh/names.h>
#include <impl/assets_shader/loader.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool LoadComputeShader(
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    MeshSkinningSystem::ShaderPathResolveCallback& shaderPathResolver,
    Core::ShaderHandle& shader,
    const Name& shaderName,
    const Name& debugName
){
    return ShaderAssetLoader::Load(
        shader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        debugName,
        graphics,
        assetManager,
        shaderPathResolver,
        NWB_TEXT("MeshSkinningSystem")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningSystem::ensureSkinningPipeline(){
    auto& device = *m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: skinning compute requires the initialized global descriptor heap"));
        return false;
    }

    if(!m_skinningBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::Compute)
        ;
        // Persistent stream descriptors and their per-runtime selector payload all live in the global heap. Keep
        // this local layout only for the dispatch push constants.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(MeshSkinningPushConstants)));

        m_skinningBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!m_skinningBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create skinning binding layout"));
            return false;
        }
    }

    if(!__hidden_pipeline::LoadComputeShader(
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        m_skinningComputeShader,
        AssetsGraphicsSkinnedMesh::s_SkinningComputeShaderName,
        Name("ECSMeshSkinning_SkinningCS")
    ))
        return false;

    if(m_skinningComputePipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_skinningComputeShader)
        .addBindingLayout(m_skinningBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_skinningComputePipeline = device.createComputePipeline(pipelineDesc);
    if(m_skinningComputePipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create skinning compute pipeline"));
    return false;
}

bool MeshSkinningSystem::ensureBoundsPipeline(){
    auto& device = *m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: meshlet-bounds compute requires the initialized global descriptor heap"));
        return false;
    }

    if(!m_boundsBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::Compute)
        ;
        // The per-runtime selector payload is a global UniformBuffer heap entry selected by a push-constant word.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(MeshletBoundsPushConstants)));

        m_boundsBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!m_boundsBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create bounds binding layout"));
            return false;
        }
    }

    if(!__hidden_pipeline::LoadComputeShader(
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        m_boundsComputeShader,
        AssetsGraphicsSkinnedMesh::s_MeshletBoundsComputeShaderName,
        Name("ECSMeshSkinning_MeshletBoundsCS")
    ))
        return false;

    if(m_boundsComputePipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_boundsComputeShader)
        .addBindingLayout(m_boundsBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_boundsComputePipeline = device.createComputePipeline(pipelineDesc);
    if(m_boundsComputePipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create bounds compute pipeline"));
    return false;
}

bool MeshSkinningSystem::ensureRepackPipeline(){
    auto& device = *m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: normal-repack compute requires the initialized global descriptor heap"));
        return false;
    }

    if(!m_repackBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::Compute)
        ;
        // The per-runtime selector payload is a global UniformBuffer heap entry selected by a push-constant word.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(MeshletRepackPushConstants)));

        m_repackBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!m_repackBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create repack binding layout"));
            return false;
        }
    }

    if(!__hidden_pipeline::LoadComputeShader(
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        m_repackComputeShader,
        AssetsGraphicsSkinnedMesh::s_RepackNormalsComputeShaderName,
        Name("ECSMeshSkinning_RepackNormalsCS")
    ))
        return false;

    if(m_repackComputePipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_repackComputeShader)
        .addBindingLayout(m_repackBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_repackComputePipeline = device.createComputePipeline(pipelineDesc);
    if(m_repackComputePipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create repack compute pipeline"));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

