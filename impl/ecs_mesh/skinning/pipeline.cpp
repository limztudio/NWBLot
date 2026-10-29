// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <impl/assets_shader/loader.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 LoadComputeShader(
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    MeshSkinningSystem::ShaderPathResolveCallback& shaderPathResolver,
    const ActiveSceneSubsystems* const activeSceneSubsystems,
    const Core::Assets::AssetRef<ComputeShader> SkinningSubsystem::* const member,
    const Name& debugName,
    Core::ShaderHandle& shader
){
    if(!activeSceneSubsystems)
        return false;
    const SkinningSubsystem* const skinningSubsystem = activeSceneSubsystems->findSubsystem<SkinningSubsystem>();
    if(!skinningSubsystem)
        return false;
    const Core::Assets::AssetRef<ComputeShader>& resolved = skinningSubsystem->*member;
    if(!resolved.valid())
        return false;
    return ShaderAssetLoader::Load<ComputeShader>(shader, resolved.name(), Core::ShaderArchive::s_DefaultVariant, debugName, graphics, assetManager, shaderPathResolver, NWB_TEXT("MeshSkinningSystem"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSkinningSystem::ensureSkinningPipeline(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: skinning compute requires the initialized global descriptor heap"));
        return false;
    }

    if(!m_skinningBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        // Streams live in the global heap; local layout is push constants only.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(MeshSkinningPushConstants)));

        auto skinningBindingLayoutResult = device.createBindingLayout(bindingLayoutDesc);
        if(skinningBindingLayoutResult)
            m_skinningBindingLayout = Move(*skinningBindingLayoutResult);
        else
            m_skinningBindingLayout.reset();
        if(!skinningBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create skinning binding layout"));
            return false;
        }
    }

    if(!__hidden_pipeline::LoadComputeShader(m_graphics, m_assetManager, m_shaderPathResolver, m_activeSceneSubsystems, &SkinningSubsystem::skinningCs, Name("ECSMeshSkinning_SkinningCS"), m_skinningComputeShader))
        return false;

    if(m_skinningComputePipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_skinningComputeShader).addBindingLayout(m_skinningBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto skinningComputePipelineResult = device.createComputePipeline(pipelineDesc);
    if(skinningComputePipelineResult)
        m_skinningComputePipeline = Move(*skinningComputePipelineResult);
    else
        m_skinningComputePipeline.reset();
    if(skinningComputePipelineResult)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create skinning compute pipeline"));
    return false;
}

b8 MeshSkinningSystem::ensureBoundsPipeline(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: meshlet-bounds compute requires the initialized global descriptor heap"));
        return false;
    }

    if(!m_boundsBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        // Per-runtime selector is a global UniformBuffer entry via push-constant word.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(MeshletBoundsPushConstants)));

        auto boundsBindingLayoutResult = device.createBindingLayout(bindingLayoutDesc);
        if(boundsBindingLayoutResult)
            m_boundsBindingLayout = Move(*boundsBindingLayoutResult);
        else
            m_boundsBindingLayout.reset();
        if(!boundsBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create bounds binding layout"));
            return false;
        }
    }

    if(!__hidden_pipeline::LoadComputeShader(m_graphics, m_assetManager, m_shaderPathResolver, m_activeSceneSubsystems, &SkinningSubsystem::meshletBoundsCs, Name("ECSMeshSkinning_MeshletBoundsCS"), m_boundsComputeShader))
        return false;

    if(m_boundsComputePipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_boundsComputeShader).addBindingLayout(m_boundsBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto boundsComputePipelineResult = device.createComputePipeline(pipelineDesc);
    if(boundsComputePipelineResult)
        m_boundsComputePipeline = Move(*boundsComputePipelineResult);
    else
        m_boundsComputePipeline.reset();
    if(boundsComputePipelineResult)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create bounds compute pipeline"));
    return false;
}

b8 MeshSkinningSystem::ensureLocalBoundsPipeline(){
    if(m_localBoundsComputePipeline)
        return true;
    if(!ensureBoundsPipeline())
        return false;
    if(!__hidden_pipeline::LoadComputeShader(m_graphics, m_assetManager, m_shaderPathResolver, m_activeSceneSubsystems, &SkinningSubsystem::localBoundsCs, Name("ECSMeshSkinning_LocalBoundsCS"), m_localBoundsComputeShader))
        return false;
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_localBoundsComputeShader).addBindingLayout(m_boundsBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto localBoundsComputePipelineResult = device.createComputePipeline(pipelineDesc);
    if(localBoundsComputePipelineResult)
        m_localBoundsComputePipeline = Move(*localBoundsComputePipelineResult);
    else
        m_localBoundsComputePipeline.reset();
    if(localBoundsComputePipelineResult)
        return true;
    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create local bounds compute pipeline"));
    return false;
}

b8 MeshSkinningSystem::ensureRepackPipeline(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: normal-repack compute requires the initialized global descriptor heap"));
        return false;
    }

    if(!m_repackBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        // Per-runtime selector is a global UniformBuffer entry via push-constant word.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(MeshletRepackPushConstants)));

        auto repackBindingLayoutResult = device.createBindingLayout(bindingLayoutDesc);
        if(repackBindingLayoutResult)
            m_repackBindingLayout = Move(*repackBindingLayoutResult);
        else
            m_repackBindingLayout.reset();
        if(!repackBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create repack binding layout"));
            return false;
        }
    }

    if(!__hidden_pipeline::LoadComputeShader(m_graphics, m_assetManager, m_shaderPathResolver, m_activeSceneSubsystems, &SkinningSubsystem::repackNormalsCs, Name("ECSMeshSkinning_RepackNormalsCS"), m_repackComputeShader))
        return false;

    if(m_repackComputePipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_repackComputeShader).addBindingLayout(m_repackBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto repackComputePipelineResult = device.createComputePipeline(pipelineDesc);
    if(repackComputePipelineResult)
        m_repackComputePipeline = Move(*repackComputePipelineResult);
    else
        m_repackComputePipeline.reset();
    if(repackComputePipelineResult)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create repack compute pipeline"));
    return false;
}


b8 MeshSkinningSystem::skinningPipelineReady()const noexcept{
    return m_skinningBindingLayout && m_skinningComputePipeline;
}

b8 MeshSkinningSystem::boundsPipelinesReady()const noexcept{
    return m_boundsBindingLayout && m_boundsComputePipeline && m_localBoundsComputePipeline;
}

b8 MeshSkinningSystem::repackPipelineReady()const noexcept{
    return m_repackBindingLayout && m_repackComputePipeline;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

