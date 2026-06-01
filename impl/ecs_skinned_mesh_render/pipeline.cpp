// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets/graphics/skinned_mesh/binding_slots.h>
#include <impl/assets_shader/loader.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Name s_SkinnedMeshComputeShaderName("engine/graphics/skinned_mesh/skinning_cs");
static constexpr Name s_MeshletBoundsComputeShaderName("engine/graphics/skinned_mesh/meshlet_bounds_cs");


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
        NWB_LOGGER_ERROR(pipelineError);
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
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_REST_POSITION, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SKINNED_MESH_BINDING_SKINNED_POSITION, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_REST_NORMAL, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SKINNED_MESH_BINDING_SKINNED_NORMAL, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_REST_TANGENT, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SKINNED_MESH_BINDING_SKINNED_TANGENT, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_MESHLET_DESC, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SKINNED_MESH_BINDING_POSITION_REF_DELTAS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SKINNED_MESH_BINDING_ATTRIBUTE_REF_DELTAS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_ATTRIBUTE_SKINS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_SKIN_INFLUENCES, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BINDING_JOINT_PALETTE, 1));
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
        __hidden_pipeline::s_SkinnedMeshComputeShaderName,
        Name("ECSSkinnedMeshRender_SkinnedMeshCS"),
        NWB_TEXT("SkinnedMeshSystem: failed to create skinning compute pipeline")
    );
}

bool SkinnedMeshSystem::ensureBoundsPipeline(){
    auto* device = m_graphics.getDevice();

    if(!m_boundsBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_POSITIONS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_MESHLET_DESC, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_POSITION_REF_DELTAS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_LOCAL_VERTEX_REFS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SKINNED_MESH_BOUNDS_BINDING_PRIMITIVE_INDICES, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_UAV(NWB_SKINNED_MESH_BOUNDS_BINDING_DYNAMIC_BOUNDS, 1));
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
        __hidden_pipeline::s_MeshletBoundsComputeShaderName,
        Name("ECSSkinnedMeshRender_MeshletBoundsCS"),
        NWB_TEXT("SkinnedMeshSystem: failed to create bounds compute pipeline")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

