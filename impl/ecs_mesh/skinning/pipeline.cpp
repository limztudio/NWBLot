// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <impl/assets_shader/loader.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
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
    return ShaderAssetLoader::Load<ComputeShader>(resolved.name(), Core::ShaderArchive::s_DefaultVariant, debugName, NWB_TEXT("MeshSkinningSystem"), graphics, assetManager, shaderPathResolver, nullptr, shader);
}

[[nodiscard]] static b8 EnsurePushConstantBindingLayout(
    Core::Alloc::GlobalArena& arena,
    Core::Device& device,
    const u32 pushConstantBytes,
    const TStringView layoutLabel,
    Core::BindingLayoutHandle& inOutBindingLayout
){
    if(inOutBindingLayout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, pushConstantBytes));

    auto bindingLayoutResult = device.createBindingLayout(bindingLayoutDesc);
    if(bindingLayoutResult){
        inOutBindingLayout = Move(*bindingLayoutResult);
        return true;
    }

    inOutBindingLayout.reset();
    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create {} binding layout"), layoutLabel);
    return false;
}

[[nodiscard]] static b8 EnsureComputePipeline(
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    MeshSkinningSystem::ShaderPathResolveCallback& shaderPathResolver,
    const ActiveSceneSubsystems* const activeSceneSubsystems,
    const Core::Assets::AssetRef<ComputeShader> SkinningSubsystem::* const member,
    const Name& debugName,
    const Core::BindingLayoutHandle& bindingLayout,
    const TStringView pipelineLabel,
    Core::ShaderHandle& inOutShader,
    Core::ComputePipelineHandle& inOutPipeline
){
    if(!LoadComputeShader(graphics, assetManager, shaderPathResolver, activeSceneSubsystems, member, debugName, inOutShader))
        return false;
    if(inOutPipeline)
        return true;

    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(inOutShader).addBindingLayout(bindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto computePipelineResult = device.createComputePipeline(pipelineDesc);
    if(computePipelineResult){
        inOutPipeline = Move(*computePipelineResult);
        return true;
    }

    inOutPipeline.reset();
    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create {} compute pipeline"), pipelineLabel);
    return false;
}

template<typename PushConstantsT>
[[nodiscard]] static b8 EnsurePushConstantComputePipeline(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    MeshSkinningSystem::ShaderPathResolveCallback& shaderPathResolver,
    const ActiveSceneSubsystems* const activeSceneSubsystems,
    const Core::Assets::AssetRef<ComputeShader> SkinningSubsystem::* const member,
    const Name& debugName,
    const TStringView heapRequirementLabel,
    const TStringView pipelineLabel,
    Core::BindingLayoutHandle& inOutBindingLayout,
    Core::ShaderHandle& inOutShader,
    Core::ComputePipelineHandle& inOutPipeline
){
    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: {} compute requires the initialized global descriptor heap"), heapRequirementLabel);
        return false;
    }
    if(!EnsurePushConstantBindingLayout(arena, device, sizeof(PushConstantsT), pipelineLabel, inOutBindingLayout))
        return false;
    return EnsureComputePipeline(graphics, assetManager, shaderPathResolver, activeSceneSubsystems, member, debugName, inOutBindingLayout, pipelineLabel, inOutShader, inOutPipeline);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSkinningSystem::ensureSkinningPipeline(){
    return __hidden_pipeline::EnsurePushConstantComputePipeline<MeshSkinningPushConstants>(
        m_arena,
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        m_activeSceneSubsystems,
        &SkinningSubsystem::skinningCs,
        s_SkinningComputeShaderName,
        NWB_TEXT("skinning"),
        NWB_TEXT("skinning"),
        m_skinningBindingLayout,
        m_skinningComputeShader,
        m_skinningComputePipeline
    );
}

b8 MeshSkinningSystem::ensureBoundsPipeline(){
    return __hidden_pipeline::EnsurePushConstantComputePipeline<MeshletBoundsPushConstants>(
        m_arena,
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        m_activeSceneSubsystems,
        &SkinningSubsystem::meshletBoundsCs,
        s_MeshletBoundsComputeShaderName,
        NWB_TEXT("meshlet-bounds"),
        NWB_TEXT("bounds"),
        m_boundsBindingLayout,
        m_boundsComputeShader,
        m_boundsComputePipeline
    );
}

b8 MeshSkinningSystem::ensureLocalBoundsPipeline(){
    if(m_localBoundsComputePipeline)
        return true;
    if(!ensureBoundsPipeline())
        return false;
    return __hidden_pipeline::EnsureComputePipeline(
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        m_activeSceneSubsystems,
        &SkinningSubsystem::localBoundsCs,
        s_LocalBoundsComputeShaderName,
        m_boundsBindingLayout,
        NWB_TEXT("local bounds"),
        m_localBoundsComputeShader,
        m_localBoundsComputePipeline
    );
}

b8 MeshSkinningSystem::ensureRepackPipeline(){
    return __hidden_pipeline::EnsurePushConstantComputePipeline<MeshletRepackPushConstants>(
        m_arena,
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        m_activeSceneSubsystems,
        &SkinningSubsystem::repackNormalsCs,
        s_RepackNormalsComputeShaderName,
        NWB_TEXT("normal-repack"),
        NWB_TEXT("repack"),
        m_repackBindingLayout,
        m_repackComputeShader,
        m_repackComputePipeline
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

