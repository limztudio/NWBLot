// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "generated_geometry_reuse.h"

#include <impl/ecs_render/material/task_graph_object_geometry_key.h>

#include <impl/ecs_render/material/compute_emulation_output_index.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_generated_geometry_reuse{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool EligibleGroup(
    const MaterialPassDrawItemPartitions& draws,
    const InstanceGpuDataVector& instances,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const MaterialPipelinePass::Enum pass,
    Core::Alloc::ScratchArena& arena){
    if(
        !MaterialPipelinePassUsesRendererAvboit(pass)
        || draws.regular.computeDrawItems.empty() || !draws.regular.meshDrawItems.empty()
        || !draws.csg.empty() || !draws.csgReceiverSurface.empty()
        || !frameBindings.bindingValid() || frameBindings.instanceBufferCapacity < instances.size()
    )
        return false;

    ECSRenderDetail::MaterialPassEmulationOutputIndex<Core::Buffer*> outputs(arena);
    ECSRenderDetail::MaterialPassEmulationOutputIndex<u32> slots(arena);
    for(const MaterialPassDrawItem& draw : draws.regular.computeDrawItems){
        const MaterialPassMeshResourceSnapshot& mesh = draw.meshResources;
        if(
            draw.pipelineKey.pass != pass || draw.pipelineKey.csgMode != MaterialPipelineCsgMode::None
            || !draw.pipelineResources.sharedGeometryComputeProgram || draw.instanceIndex >= instances.size()
            || !mesh.valid() || !mesh.emulationVertexBuffer || !mesh.emulationVertexHeapHandle.valid()
            || mesh.emulationVertexHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
            || !outputs.insert(mesh.emulationVertexBuffer.get()) || !slots.insert(mesh.emulationVertexHeapHandle.slot())
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool SourceBuffersMatch(const RuntimeMeshBuffers& left, const RuntimeMeshBuffers& right)noexcept{
    return
        left.positionBuffer == right.positionBuffer && left.normalBuffer == right.normalBuffer
        && left.tangentBuffer == right.tangentBuffer && left.uv0Buffer == right.uv0Buffer
        && left.colorBuffer == right.colorBuffer && left.meshletDescBuffer == right.meshletDescBuffer
        && left.meshletBoundsBuffer == right.meshletBoundsBuffer
        && left.meshletPositionRefDeltaBuffer == right.meshletPositionRefDeltaBuffer
        && left.meshletAttributeRefDeltaBuffer == right.meshletAttributeRefDeltaBuffer
        && left.meshletLocalVertexRefBuffer == right.meshletLocalVertexRefBuffer
        && left.meshletPrimitiveIndexBuffer == right.meshletPrimitiveIndexBuffer
    ;
}

[[nodiscard]] bool DrawInputsMatch(const MaterialPassDrawItem& left, const MaterialPassDrawItem& right)noexcept{
    const MaterialPassMeshResourceSnapshot& leftMesh = left.meshResources;
    const MaterialPassMeshResourceSnapshot& rightMesh = right.meshResources;
    if(
        left.meshKey != right.meshKey || left.instanceIndex != right.instanceIndex
        || left.pipelineKey.material != right.pipelineKey.material
        || left.pipelineKey.twoSided != right.pipelineKey.twoSided
        || left.pipelineKey.csgEvaluatorVariant != right.pipelineKey.csgEvaluatorVariant
        || left.materialConstantByteOffset != right.materialConstantByteOffset || left.shadingModelId != right.shadingModelId
        || left.meshletConeCullScaleSafe != right.meshletConeCullScaleSafe
        || leftMesh.meshletCount != rightMesh.meshletCount
        || leftMesh.meshletPrimitiveIndexCount != rightMesh.meshletPrimitiveIndexCount
        || leftMesh.runtimeMesh != rightMesh.runtimeMesh
        || leftMesh.dynamicMeshletBoundsFresh != rightMesh.dynamicMeshletBoundsFresh
        || leftMesh.dynamicMeshletConesFresh != rightMesh.dynamicMeshletConesFresh
        || leftMesh.emulationVertexBuffer != rightMesh.emulationVertexBuffer
        || leftMesh.emulationVertexHeapHandle != rightMesh.emulationVertexHeapHandle
        || leftMesh.emulationIndexByteOffset != rightMesh.emulationIndexByteOffset
        || left.pipelineResources.indexedGeometryOutput != right.pipelineResources.indexedGeometryOutput
        || !ECSRenderDetail::MakeObjectGeometryEquivalenceKey(left).matches(right)
        || !SourceBuffersMatch(leftMesh.sourceBuffers, rightMesh.sourceBuffers)
    )
        return false;
    // The four eligible AVBOIT passes derive equal generation flags from the retained freshness/two-sided/cull inputs.
    for(usize index = 0u; index < LengthOf(leftMesh.geometryHeapHandles); ++index){
        if(leftMesh.geometryHeapHandles[index] != rightMesh.geometryHeapHandles[index])
            return false;
    }
    return true;
}

[[nodiscard]] bool FrameBindingsMatch(
    const ECSRenderDetail::MeshFrameBindingSnapshot& left,
    const ECSRenderDetail::MeshFrameBindingSnapshot& right)noexcept{
    return
        left.instanceBuffer == right.instanceBuffer && left.materialTypedBuffer == right.materialTypedBuffer
        && left.meshView.buffer == right.meshView.buffer && left.meshView.heapHandle == right.meshView.heapHandle
        && left.instanceHeapHandle == right.instanceHeapHandle && left.materialTypedHeapHandle == right.materialTypedHeapHandle
        && left.instanceBufferCapacity == right.instanceBufferCapacity
        && left.materialTypedBufferCapacity == right.materialTypedBufferCapacity
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitGeneratedGeometryReuse::AvboitGeneratedGeometryReuse(Core::Alloc::ScratchArena& arena)
    : m_arena(arena)
    , m_draws(arena)
    , m_instances(arena)
{}

void AvboitGeneratedGeometryReuse::reset()noexcept{
    m_draws.clear();
    m_instances.clear();
    m_frameBindings = {};
    m_producer = {};
    m_captured = false;
}

bool AvboitGeneratedGeometryReuse::capture(
    const MaterialPassDrawItemPartitions& draws,
    const InstanceGpuDataVector& instances,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::MeshViewGpuData& view,
    const MaterialPipelinePass::Enum pass){
    reset();
    if(!__hidden_generated_geometry_reuse::EligibleGroup(draws, instances, frameBindings, pass, m_arena))
        return false;
    m_draws.assign(draws.regular.computeDrawItems.begin(), draws.regular.computeDrawItems.end());
    m_instances.reserve(m_draws.size());
    for(const MaterialPassDrawItem& draw : m_draws)
        m_instances.push_back(instances[draw.instanceIndex]);
    m_frameBindings = frameBindings;
    static_assert(sizeof(m_viewBytes) == sizeof(view));
    NWB_MEMCPY(m_viewBytes, sizeof(m_viewBytes), &view, sizeof(view));
    m_captured = true;
    return true;
}

bool AvboitGeneratedGeometryReuse::matches(
    const MaterialPassDrawItemPartitions& draws,
    const InstanceGpuDataVector& instances,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::MeshViewGpuData& view,
    const MaterialPipelinePass::Enum pass){
    if(
        !m_captured || !m_producer.valid()
        || !__hidden_generated_geometry_reuse::EligibleGroup(draws, instances, frameBindings, pass, m_arena)
        || m_draws.size() != draws.regular.computeDrawItems.size()
        || !__hidden_generated_geometry_reuse::FrameBindingsMatch(m_frameBindings, frameBindings)
        || NWB_MEMCMP(m_viewBytes, &view, sizeof(view)) != 0
    ){
        reset();
        return false;
    }
    for(usize index = 0u; index < m_draws.size(); ++index){
        const MaterialPassDrawItem& current = draws.regular.computeDrawItems[index];
        if(
            !__hidden_generated_geometry_reuse::DrawInputsMatch(m_draws[index], current)
            || NWB_MEMCMP(&m_instances[index], &instances[current.instanceIndex], sizeof(InstanceGpuData)) != 0
        ){
            reset();
            return false;
        }
    }
    return true;
}

bool AvboitGeneratedGeometryReuse::publishProducer(const Core::GpuTaskId task)noexcept{
    if(!m_captured || !task.valid()){
        reset();
        return false;
    }
    m_producer = task;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

