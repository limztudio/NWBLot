// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_shader/loader.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const Name& SkinnedMeshComputeShaderName(){
    static const Name s("engine/graphics/skinned_mesh/skinning_cs");
    return s;
}

static const Name& MeshletBoundsComputeShaderName(){
    static const Name s("engine/graphics/skinned_mesh/meshlet_bounds_cs");
    return s;
}

static bool EnsureComputePipeline(
    Core::Device& device,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    SkinnedMeshSystem::ShaderPathResolveCallback& shaderPathResolver,
    Core::ShaderHandle& shader,
    Core::ComputePipelineHandle& pipeline,
    const Core::BindingLayoutHandle& bindingLayout,
    const Name& shaderName,
    const Name& debugName,
    const tchar* pipelineError
){
    if(!ShaderAssetLoader::Load(
        shader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        debugName,
        graphics,
        assetManager,
        shaderPathResolver,
        NWB_TEXT("SkinnedMeshSystem")
    ))
        return false;

    if(pipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader);
    pipelineDesc.addBindingLayout(bindingLayout);
    pipeline = device.createComputePipeline(pipelineDesc);
    if(!pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("{}"), pipelineError);
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedMeshSystem::ensureSkinningPipeline(){
    auto* device = m_graphics.getDevice();

    if(!m_skinningBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(5, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(6, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(7, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(8, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(9, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(10, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(11, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SkinnedMeshPushConstants)));

        m_skinningBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_skinningBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: failed to create skinning binding layout"));
            return false;
        }
    }

    return __hidden_pipeline::EnsureComputePipeline(
        *device,
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        m_skinningComputeShader,
        m_skinningComputePipeline,
        m_skinningBindingLayout,
        __hidden_pipeline::SkinnedMeshComputeShaderName(),
        Name("ECSSkinnedMeshRender_SkinnedMeshCS"),
        NWB_TEXT("SkinnedMeshSystem: failed to create skinning compute pipeline")
    );
}

bool SkinnedMeshSystem::ensureBoundsPipeline(){
    auto* device = m_graphics.getDevice();

    if(!m_boundsBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_UAV(5, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(MeshletBoundsPushConstants)));

        m_boundsBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_boundsBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: failed to create bounds binding layout"));
            return false;
        }
    }

    return __hidden_pipeline::EnsureComputePipeline(
        *device,
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        m_boundsComputeShader,
        m_boundsComputePipeline,
        m_boundsBindingLayout,
        __hidden_pipeline::MeshletBoundsComputeShaderName(),
        Name("ECSSkinnedMeshRender_MeshletBoundsCS"),
        NWB_TEXT("SkinnedMeshSystem: failed to create bounds compute pipeline")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

