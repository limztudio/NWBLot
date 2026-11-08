// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_emulation_resources.h"

#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/mesh/mesh_geometry_buffer_policy.adhd.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshEmulationResources::prepare(Core::GraphicsRuntime& graphics, MeshResources& mesh)const{
    if(mesh.emulationVertexHeapHandle.valid())
        return true;
    if(!mesh.emulationVertexBuffer){
        const Expected<ComputeEmulationLayout> layoutResult = MeshGeometryBufferPolicy().resolveComputeEmulationLayout(mesh.meshletLocalVertexRefBuffer->getDescription().byteSize, mesh.meshletPrimitiveIndexCount);
        if(!layoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: generated geometry layout exceeds the index byte-address range for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }
        const ComputeEmulationLayout& layout = *layoutResult;
        const auto emulationVertexBufferName = DeriveName(mesh.meshName, AStringView(":emulation_vb"));
        if(!emulationVertexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive compute-emulation vertex buffer name for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }

        Core::BufferDesc emulationVertexBufferDesc;
        emulationVertexBufferDesc
            .setByteSize(layout.bufferByteSize)
            .setStructStride(s_EmulatedVertexStride)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(*emulationVertexBufferName)
        ;
        auto emulationVertexBufferResult = graphics.createBuffer(emulationVertexBufferDesc);
        if(emulationVertexBufferResult)
            mesh.emulationVertexBuffer = Move(*emulationVertexBufferResult);
        else
            mesh.emulationVertexBuffer.reset();
        if(!emulationVertexBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
            return false;
        }
        mesh.emulationIndexByteOffset = layout.indexByteOffset;
    }

    auto& device = graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute-emulation vertex buffer requires the initialized global descriptor heap"));
        return false;
    }
    if(RendererDescriptorRegisterDetail::AssignDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::StructuredBufferUav(0u, mesh.emulationVertexBuffer.get()), mesh.emulationVertexHeapHandle))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register compute-emulation vertex buffer in the descriptor heap for mesh '{}'"), ConvertString(mesh.meshName.resolvedText()));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
