// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_gbuffer_resource_uses.h"

#include <impl/ecs_render/csg/types.adhd.h>
#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/shared/frame_push_constants.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueGbufferResourceUseBuilder::Append(const OpaqueGbufferResourceUseInputs& inputs, Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& uses){
    using namespace RendererTaskGraphDetail;

    uses.reserve((inputs.hasOpaqueDrawItems ? s_OpaqueDrawResourceUseCount : s_NoOpaqueDrawResourceUseCount) + (inputs.hasCsgFrameGpuWork ? s_OpaqueCsgFrameResourceUseCount : 0u) + (inputs.hasOpaqueCsgFrameWork ? s_OpaqueCsgFrameResourceUseCount : 0u));
    uses.push_back(ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer));
    if(inputs.hasOpaqueDrawItems){
        uses.push_back(ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange));
        uses.push_back(ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange));
    }
    if(inputs.hasCsgFrameGpuWork){
        uses.push_back(ReadBufferUse(inputs.resources.csgReceiverRanges, inputs.csgReceiverRange));
        uses.push_back(ReadBufferUse(inputs.resources.csgCutters, inputs.csgCutterRange));
        uses.push_back(ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        uses.push_back(ReadUse(inputs.resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        // CSG resolves target-specific images through the deferred bindless-slot buffer selected in its context.
        uses.push_back(ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    }
    if(inputs.hasOpaqueCsgFrameWork){
        // Payloads and events never load priors. Interval ID + event count keep sparse-write validity after clears; only they need ReadWrite.
        // Keeping the payloads write-only also permits their first graph use to begin at a fresh texture's native Unknown state.
        uses.push_back(WriteTextureUse(inputs.resources.csgCapBackNormal, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        uses.push_back(WriteTextureUse(inputs.resources.csgIntervalDepth, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteTextureUse(inputs.resources.csgIntervalId, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        uses.push_back(WriteTextureUse(inputs.resources.csgReceiverEventData, inputs.csgReceiverEventDataSubresources, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteTextureUse(inputs.resources.csgReceiverEventCount, inputs.csgReceiverEventCountSubresources, Core::ResourceStates::UnorderedAccess));
    }
    uses.push_back(WriteUse(inputs.resources.albedo, Core::ResourceStates::RenderTarget));
    uses.push_back(WriteUse(inputs.resources.normal, Core::ResourceStates::RenderTarget));
    uses.push_back(WriteUse(inputs.resources.worldPosition, Core::ResourceStates::RenderTarget));
    uses.push_back(WriteUse(inputs.resources.specularRoughness, Core::ResourceStates::RenderTarget));
    uses.push_back(WriteUse(inputs.resources.depth, Core::ResourceStates::DepthWrite));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

