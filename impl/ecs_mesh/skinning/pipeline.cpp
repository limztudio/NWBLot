
#include "system.h"

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/graphics/pipeline_helpers.h>
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
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(MeshSkinningPushConstants)));

        m_skinningBindingLayout = device->createBindingLayout(bindingLayoutDesc);
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

    if(Core::CreateComputePipelineIfNeeded(
        *device,
        m_skinningComputePipeline,
        m_skinningComputeShader,
        m_skinningBindingLayout
    ))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create skinning compute pipeline"));
    return false;
}

bool MeshSkinningSystem::ensureBoundsPipeline(){
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

    if(Core::CreateComputePipelineIfNeeded(
        *device,
        m_boundsComputePipeline,
        m_boundsComputeShader,
        m_boundsBindingLayout
    ))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create bounds compute pipeline"));
    return false;
}

bool MeshSkinningSystem::ensureRepackPipeline(){
    auto* device = m_graphics.getDevice();

    if(!m_repackBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_REPACK_BINDING_MESHLET_DESC, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SKINNED_MESH_REPACK_BINDING_PRIMITIVE_INDICES, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SKINNED_MESH_REPACK_BINDING_ATTRIBUTE_REF_DELTAS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_REPACK_BINDING_LOCAL_VERTEX_REFS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SKINNED_MESH_REPACK_BINDING_SKINNED_NORMALS, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_UAV(NWB_SKINNED_MESH_REPACK_BINDING_ATTRIBUTE_BUFFER, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(MeshletRepackPushConstants)));

        m_repackBindingLayout = device->createBindingLayout(bindingLayoutDesc);
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

    if(Core::CreateComputePipelineIfNeeded(
        *device,
        m_repackComputePipeline,
        m_repackComputeShader,
        m_repackBindingLayout
    ))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningSystem: failed to create repack compute pipeline"));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

