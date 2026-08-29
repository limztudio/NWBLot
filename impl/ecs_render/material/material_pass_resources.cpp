// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/material/material_pass_csg_private.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/shared/renderer_state.h>

#include <impl/assets/graphics/mesh/names.h>

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMaterialSystem::createComputeEmulationResources(){
    if(!m_drawState.m_computeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        // The per-mesh generated-vertex UAV is a global StorageBuffer heap entry selected through the fourth mesh
        // frame-slot push-constant lane.  This local layout deliberately retains only the push range.
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        auto& device = m_graphics.getDevice();
        m_drawState.m_computeBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!m_drawState.m_computeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding layout"));
            return false;
        }
    }

    if(!m_drawState.m_emulationVertexShader){
        if(!m_shaderSystem.loadShader(
            m_drawState.m_emulationVertexShader,
            AssetsGraphicsMesh::s_EmulationVertexShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSRender_MeshEmulationVS"
        ))
            return false;
    }

    if(!m_drawState.m_emulationInputLayout){
        Core::VertexAttributeDesc attributes[NWB_MESH_EMULATION_VERTEX_ATTRIBUTE_COUNT];
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_POSITION_LOCATION],
            Core::Format::RGBA32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_POSITION_BYTE_OFFSET,
            "POSITION"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_NORMAL_LOCATION],
            Core::Format::RGBA16_FLOAT,
            NWB_MESH_EMULATION_VERTEX_NORMAL_BYTE_OFFSET,
            "NORMAL"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_TANGENT_LOCATION],
            Core::Format::RGBA16_FLOAT,
            NWB_MESH_EMULATION_VERTEX_TANGENT_BYTE_OFFSET,
            "TANGENT"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_UV0_LOCATION],
            Core::Format::RG32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_UV0_BYTE_OFFSET,
            "TEXCOORD"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_COLOR_LOCATION],
            Core::Format::RGBA16_FLOAT,
            NWB_MESH_EMULATION_VERTEX_COLOR_BYTE_OFFSET,
            "COLOR"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_LOCATION],
            Core::Format::RGBA32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_BYTE_OFFSET,
            "POSITION1"
        );

        auto& device = m_graphics.getDevice();
        m_drawState.m_emulationInputLayout = device.createInputLayout(
            attributes,
            NWB_MESH_EMULATION_VERTEX_ATTRIBUTE_COUNT,
            m_drawState.m_emulationVertexShader.get()
        );
        if(!m_drawState.m_emulationInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation input layout"));
            return false;
        }
    }

    return true;
}

bool RendererMaterialSystem::prepareMaterialPassResourceBindings(const MaterialPassDrawItems& drawItems){
    return prepareMeshMaterialPassResourceBindings(drawItems.meshDrawItems)
        && prepareComputeMaterialPassResourceBindings(drawItems.computeDrawItems)
    ;
}

bool RendererMaterialSystem::prepareMeshMaterialPassResourceBindings(const MaterialPassDrawItemVector& drawItems){
    return prepareMaterialPassResourceBindingsImpl(drawItems, false);
}

bool RendererMaterialSystem::prepareComputeMaterialPassResourceBindings(const MaterialPassDrawItemVector& drawItems){
    return prepareMaterialPassResourceBindingsImpl(drawItems, true);
}

bool RendererMaterialSystem::prepareMaterialPassResourceBindingsImpl(
    const MaterialPassDrawItemVector& drawItems,
    const bool computeEmulation
){
    if(drawItems.empty())
        return true;
    if(!m_meshSystem.createMeshFrameHeapHandles())
        return false;

    bool ready = true;
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem&, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        if(!ready)
            return;

        if(!computeEmulation){
            ready = pipelineResources.meshletPipeline
                && m_meshSystem.meshGeometryHeapHandlesReady(mesh)
            ;
            return;
        }

        ready = pipelineResources.computePipeline
            && pipelineResources.emulationPipeline
            && m_meshSystem.meshGeometryHeapHandlesReady(mesh)
            && mesh.emulationVertexBuffer
            && mesh.emulationVertexHeapHandle.valid()
            && mesh.emulationVertexHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        ;
    });
    return ready;
}

bool RendererMaterialSystem::reserveInstanceBufferCapacity(const usize instanceCount){
    if(instanceCount == 0)
        return true;
    NWB_ASSERT(instanceCount <= static_cast<usize>(Limit<u32>::s_Max));
    if(m_drawState.m_instanceBuffer && m_drawState.m_instanceBufferCapacity >= instanceCount)
        return true;

    const usize capacity = ::NextGrowingCapacity(m_drawState.m_instanceBufferCapacity, instanceCount);
#if defined(NWB_DEBUG)
    if(capacity > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer capacity overflows addressable memory"));
        return false;
    }
#endif

    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setDebugName(ECSRenderDetail::s_InstanceBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle instanceBuffer = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create instance data buffer"));
        return false;
    }

    m_meshSystem.releaseMeshFrameHeapHandles();
    m_drawState.m_instanceBuffer = Move(instanceBuffer);
    m_drawState.m_instanceBufferCapacity = capacity;
    return true;
}

bool RendererMaterialSystem::reserveMaterialTypedBufferCapacity(const usize byteCount){
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
#if defined(NWB_DEBUG)
    if(!AlignUpChecked(requiredByteCount, sizeof(u32), requiredByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed buffer request overflows alignment"));
        return false;
    }
    if(requiredByteCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed buffer request exceeds u32 byte-offset limits"));
        return false;
    }
#else
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
#endif
    if(m_drawState.m_materialTypedBuffer && m_drawState.m_materialTypedBufferCapacity >= requiredByteCount)
        return true;

    const usize capacity = ::NextGrowingCapacity(m_drawState.m_materialTypedBufferCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setDebugName(ECSRenderDetail::s_MaterialTypedBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialTypedBuffer = m_graphics.createBuffer(materialTypedBufferDesc);
    if(!materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material typed buffer"));
        return false;
    }

    m_meshSystem.releaseMeshFrameHeapHandles();
    m_drawState.m_materialTypedBuffer = Move(materialTypedBuffer);
    m_drawState.m_materialTypedBufferCapacity = capacity;
    return true;
}

bool RendererMaterialSystem::prepareMaterialPassDrawBuffers(
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes
){
    usize uploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes, uploadBytes))
        return false;

    return reserveInstanceBufferCapacity(instanceData.size()) && reserveMaterialTypedBufferCapacity(uploadBytes);
}

bool RendererMaterialSystem::materialPassDrawBuffersReady(
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes
)const{
    usize uploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes, uploadBytes))
        return false;

    return materialPassDrawBuffersReady(instanceData.size(), uploadBytes);
}

bool RendererMaterialSystem::materialPassDrawBuffersReady(
    const usize instanceCount,
    const usize materialTypedByteCount
)const{
    if(
        materialTypedByteCount == 0u
        || (materialTypedByteCount & (sizeof(u32) - 1u)) != 0u
    )
        return false;

    const usize requiredMaterialTypedBytes = Max<usize>(materialTypedByteCount, sizeof(u32));
    NWB_ASSERT((requiredMaterialTypedBytes & (sizeof(u32) - 1u)) == 0u);

    return
        (instanceCount == 0u || (m_drawState.m_instanceBuffer && m_drawState.m_instanceBufferCapacity >= instanceCount))
        && m_drawState.m_materialTypedBuffer
        && m_drawState.m_materialTypedBufferCapacity >= requiredMaterialTypedBytes
    ;
}

ECSRenderDetail::MaterialPassBufferSnapshot RendererMaterialSystem::materialPassBufferSnapshot()const{
    return {
        .instanceBuffer = m_drawState.m_instanceBuffer,
        .typedBuffer = m_drawState.m_materialTypedBuffer,
    };
}

void RendererMaterialSystem::prepareMaterialPassInstanceUploadData(InstanceGpuDataVector& instanceData){
    // Slot 6 carries CSG's heap-selected UniformBuffer context for every raster instance, avoiding a second
    // pipeline-local resource descriptor in the mesh and compute geometry stages.
    u32 csgContextHeapSlot = 0u;
    if(!m_csgSystem.findCsgClipContextHeapSlot(csgContextHeapSlot))
        csgContextHeapSlot = 0u;
    for(InstanceGpuData& instance : instanceData)
        instance.geometryHeapSlots[NWB_MESH_INSTANCE_CSG_CONTEXT_HEAP_SLOT] = csgContextHeapSlot;
}

bool RendererMaterialSystem::findMaterialPassDrawItemResources(
    const MaterialPassDrawItem& drawItem,
    MeshResources*& outMesh,
    MaterialPipelineResources*& outPipelineResources
){
    outMesh = nullptr;
    outPipelineResources = nullptr;

    MeshResources* mesh = nullptr;
    if(!m_meshSystem.findMeshResources(drawItem.meshKey, mesh))
        return false;

    const auto foundPipeline = m_materialState.m_pipelines.find(drawItem.pipelineKey);
    if(foundPipeline == m_materialState.m_pipelines.end())
        return false;

    outMesh = mesh;
    outPipelineResources = &foundPipeline.value();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

