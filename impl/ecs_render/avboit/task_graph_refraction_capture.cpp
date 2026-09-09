// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_refraction_capture.h"

#include <core/graphics/vulkan/backend.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <global/basic_string.h>


NWB_IMPL_BEGIN

namespace RendererTaskGraphDetail{
namespace __hidden_refraction_capture{

[[nodiscard]] Core::GpuGraphResourceId ImportTexture(Core::GpuTaskGraph& graph, const Core::TextureHandle& texture){
    if(!texture)
        return {};
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return {};
        const Core::GpuGraphResourceId existing = declarations.findImportedTexture(texture);
        if(existing.valid())
            return existing;
    }
    return graph.importTexture(texture, TextureResourceDesc(texture->getCreationDescription().name, "Refraction Capture Texture"));
}

[[nodiscard]] Core::GpuGraphResourceId ImportBuffer(Core::GpuTaskGraph& graph, const Core::BufferHandle& buffer){
    if(!buffer)
        return {};
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return {};
        const Core::GpuGraphResourceId existing = declarations.findImportedBuffer(buffer);
        if(existing.valid())
            return existing;
    }
    return graph.importBuffer(buffer, BufferResourceDesc(buffer->getCreationDescription().debugName, "Refraction Capture Buffer"));
}

[[nodiscard]] Core::GpuTaskDesc TaskDesc(const Name identity, const AStringView label, const Core::GpuTaskId& dependency){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setQueue(GraphicsQueueRequest()).setScheduling(scheduling);
    if(dependency.valid())
        desc.setDependencies(&dependency, 1u);
    return desc;
}

struct CaptureDrawTask{
    struct Payload{
        RendererMaterialSystem* materialSystem = nullptr;
        const DeferredFrameTargets* deferredTargets = nullptr;
        AvboitFrameTargets avboitTargets;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena> drawItems;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool csg = false;
        bool compute = false;
        bool generate = false;

        explicit Payload(Core::Alloc::GlobalArena& arena) : drawItems(arena){}
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.materialSystem || !payload.deferredTargets || !payload.avboitTargets.refractionFramebuffer
            || !payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount)
            || (payload.csg && !payload.csgResources.bindingValid()))
            return false;

        Core::Alloc::ScratchArena scratch(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItemVector drawItems{scratch};
        drawItems.assign(payload.drawItems.begin(), payload.drawItems.end());
        if(!(payload.compute
            ? payload.materialSystem->computeMaterialPassDrawResourcesReady(drawItems, payload.frameBindings)
            : payload.materialSystem->meshMaterialPassDrawResourcesReady(drawItems, payload.frameBindings)))
            return false;

        commandList.endRenderPass();
        Core::ViewportState viewport;
        viewport.addViewportAndScissorRect(payload.avboitTargets.refractionFramebuffer->getFramebufferInfo().getViewport());
        const MaterialPassDrawContext drawContext{
            commandList,
            *payload.deferredTargets,
            payload.generate ? nullptr : payload.avboitTargets.refractionFramebuffer.get(),
            MaterialPipelinePass::AvboitRefractionCapture,
            &payload.avboitTargets,
            viewport,
            false,
            true,
            true,
            true,
            true,
            true,
            payload.csg ? &payload.csgResources : nullptr,
            payload.frameBindings
        };
        if(payload.generate)
            payload.materialSystem->generateComputeMaterialPassDrawItems(drawContext, drawItems);
        else if(payload.compute)
            payload.materialSystem->renderComputeMaterialPassDrawItemsRasterOnly(drawContext, drawItems);
        else
            payload.materialSystem->renderMeshMaterialPassDrawItems(drawContext, drawItems);
        commandList.endRenderPass();
        return true;
    }
};

struct FinalizeTask{
    struct Payload{};
    [[nodiscard]] static bool record(const Payload&, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        return true;
    }
};

};

Core::GpuTaskId DeclareAvboitRefractionCapture(
    Core::GpuTaskGraph& graph,
    Core::Alloc::GlobalArena& arena,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::MeshViewGpuData& meshViewState,
    Core::GpuTaskId dependency,
    const bool enabled
){
    using namespace __hidden_refraction_capture;
    const Core::GpuGraphResourceId depth = ImportTexture(graph, targets.avboit.refractionDepth);
    const Core::GpuGraphResourceId normalIor = ImportTexture(graph, targets.avboit.refractionNormalIor);
    const Core::GpuGraphResourceId tintCoverage = ImportTexture(graph, targets.avboit.refractionTintCoverage);
    const Core::GpuGraphResourceId instance = ImportTexture(graph, targets.avboit.refractionInstance);
    if(!depth.valid() || !normalIor.valid() || !tintCoverage.valid() || !instance.valid())
        return {};

    const auto appendClear = [&](const Name identity, const Core::GpuGraphResourceId destination, const bool isDepth){
        Core::GpuTaskDesc desc = TaskDesc(identity, "Refraction Capture Clear", dependency);
        desc.setQueue(GraphicsUploadQueueRequest());
        Core::GpuClearTextureTaskDesc clear;
        clear.destination = destination;
        clear.subresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
        clear.valueType = isDepth ? Core::GpuClearTextureTaskValueType::DepthStencil : Core::GpuClearTextureTaskValueType::Float;
        clear.clearDepth = isDepth;
        clear.depthValue = 1.f;
        clear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
        dependency = graph.addClearTextureTask(desc, clear);
        return dependency.valid();
    };
    if(!appendClear(Name("render.refraction.clear.depth"), depth, true)
        || !appendClear(Name("render.refraction.clear.normal_ior"), normalIor, false)
        || !appendClear(Name("render.refraction.clear.tint_coverage"), tintCoverage, false)
        || !appendClear(Name("render.refraction.clear.instance"), instance, false))
        return {};

    const auto finalize = [&](){
        const Core::GpuTaskResourceUse uses[] = {ReadUse(depth), ReadUse(normalIor), ReadUse(tintCoverage), ReadUse(instance)};
        Core::GpuTaskDesc desc = TaskDesc(Name("render.refraction.capture_finalize"), "Refraction Capture Finalize", dependency);
        desc.setResourceUses(uses, LengthOf(uses));
        return graph.addTask<FinalizeTask>(desc, FinalizeTask::Payload{});
    };
    if(!enabled)
        return finalize();
    if(!targets.avboit.refractionFramebuffer)
        return {};
    // The fourth shader output has its write mask disabled, but Vulkan still requires the attached scratch
    // image in attachment state. The ordinary AVBOIT clear initializes it after capture completes.
    const Core::GpuGraphResourceId unusedExtinctionAttachment = ImportTexture(graph, targets.avboit.foregroundAccumExtinction);
    if(!unusedExtinctionAttachment.valid())
        return {};

    Core::Alloc::ScratchArena scratch(RendererArenaScope::s_TaskGraphArena);
    MaterialPassDrawItemPartitions drawItems{scratch};
    InstanceGpuDataVector instances{scratch};
    MaterialTypedByteDataVector typedBytes{scratch};
    CsgFrameGpuData csgFrameData{scratch};
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector typedRanges{scratch};
#endif
    materialSystem.gatherMaterialPassDrawItems(
        targets.avboit.refractionFramebuffer.get(), MaterialPipelinePass::AvboitRefractionCapture, true,
        csgFrameState, drawItems, instances, csgFrameData,
#if defined(NWB_DEBUG)
        typedRanges,
#endif
        typedBytes, RendererResourceLookupMode::PreparedOnly, &meshViewState
    );
    if(drawItems.empty())
        return finalize();
    const bool hasCsg = !drawItems.csg.empty();
    if(!frameBindings.frameReady(instances.size(), typedBytes.size())
        || !materialSystem.materialPassDrawResourcesReady(drawItems.regular, frameBindings)
        || (hasCsg && (!csgFrameData.hasWork() || !csgResources.frameReady(csgFrameData)
            || !materialSystem.materialPassDrawResourcesReady(drawItems.csg, frameBindings))))
        return {};

    const MaterialPassDrawItems* const drawSets[] = {&drawItems.regular, &drawItems.csg};
    Core::GpuGraphResourceSetId geometrySet;
    Core::GpuGraphResourceSetId sampledTextureSet;
    if(!GatherPreparedMaterialGeometryResourceSet(graph, drawSets, LengthOf(drawSets), scratch,
            Name("render.refraction.capture.geometry"), "Refraction Capture Geometry", geometrySet)
        || !GatherPreparedMaterialSampledTextureResourceSet(materialSystem, graph, drawSets, LengthOf(drawSets), scratch,
            Name("render.refraction.capture.material_textures"), "Refraction Capture Material Textures", sampledTextureSet))
        return {};
    const Core::GpuTaskResourceSetUse setUses[] = {
        {.resourceSet = geometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read},
        {.resourceSet = sampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read},
    };
    const usize setUseCount = sampledTextureSet.valid() ? 2u : 1u;

    materialSystem.prepareMaterialPassInstanceUploadData(instances, csgResources);
    const Core::GpuGraphResourceId materialInstances = ImportBuffer(graph, frameBindings.instanceBuffer);
    const Core::GpuGraphResourceId materialTyped = ImportBuffer(graph, frameBindings.materialTypedBuffer);
    const Core::GpuGraphResourceId meshView = ImportBuffer(graph, frameBindings.meshView.buffer);
    const Core::GpuGraphResourceId slots = ImportBuffer(graph, targets.bindless.slotsBuffer);
    const Core::GpuGraphResourceId opaqueDepth = ImportTexture(graph, targets.depth);
    if(!materialInstances.valid() || !materialTyped.valid() || !meshView.valid() || !slots.valid() || !opaqueDepth.valid())
        return {};

    const auto appendUpload = [&](const Name identity, const Core::GpuGraphResourceId destination,
                                  const void* bytes, const usize byteSize, const usize alignment){
        if(!destination.valid() || !bytes || byteSize == 0u)
            return false;
        const Core::GpuUploadBlobId blob = graph.copyUploadData(bytes, byteSize, alignment);
        if(!blob.valid())
            return false;
        Core::GpuTaskDesc desc = TaskDesc(identity, "Refraction Capture Upload", dependency);
        desc.setQueue(GraphicsUploadQueueRequest());
        dependency = graph.addUploadBufferTask(desc, Core::GpuUploadBufferTaskDesc{
            .source = blob, .destination = destination, .finalState = Core::ResourceStates::Common,
        });
        return dependency.valid();
    };
    if(!appendUpload(Name("render.refraction.capture.instances_upload"), materialInstances,
            instances.data(), instances.size() * sizeof(InstanceGpuData), alignof(InstanceGpuData))
        || !appendUpload(Name("render.refraction.capture.material_upload"), materialTyped,
            typedBytes.data(), typedBytes.size(), alignof(u32)))
        return {};

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> commonUses{scratch};
    commonUses.push_back(ReadBufferUse(materialInstances, Core::BufferRange(0u, instances.size() * sizeof(InstanceGpuData))));
    commonUses.push_back(ReadBufferUse(materialTyped, Core::BufferRange(0u, typedBytes.size())));
    commonUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
    commonUses.push_back(ReadUse(slots, Core::ResourceStates::ConstantBuffer));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgUses{scratch};
    if(hasCsg){
        CsgClipContextSlots clipContext;
        if(!csgSystem.prepareCsgClipContextSlotData(targets, csgFrameData, csgResources, frameBindings, clipContext))
            return {};
        const Core::GpuGraphResourceId ranges = ImportBuffer(graph, csgResources.receiverRanges);
        const Core::GpuGraphResourceId cutters = ImportBuffer(graph, csgResources.cutters);
        const Core::GpuGraphResourceId clip = ImportBuffer(graph, csgResources.clipContextSlots);
        const Core::GpuGraphResourceId interval = ImportBuffer(graph, csgResources.intervalSampleState);
        if(!interval.valid()
            || !appendUpload(Name("render.refraction.capture.csg_ranges_upload"), ranges,
                csgFrameData.receiverRanges.data(), csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData), alignof(CsgReceiverRangeGpuData))
            || !appendUpload(Name("render.refraction.capture.csg_cutters_upload"), cutters,
                csgFrameData.cutters.data(), csgFrameData.cutters.size() * sizeof(CsgCutterGpuData), alignof(CsgCutterGpuData))
            || !appendUpload(Name("render.refraction.capture.csg_context_upload"), clip, &clipContext, sizeof(clipContext), alignof(CsgClipContextSlots)))
            return {};
        csgUses.push_back(ReadBufferUse(ranges, Core::BufferRange(0u, csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData))));
        csgUses.push_back(ReadBufferUse(cutters, Core::BufferRange(0u, csgFrameData.cutters.size() * sizeof(CsgCutterGpuData))));
        csgUses.push_back(ReadUse(clip, Core::ResourceStates::ConstantBuffer));
        csgUses.push_back(ReadUse(interval, Core::ResourceStates::ConstantBuffer));
        const Core::TextureHandle intervalTextures[] = {
            targets.csgRemovedIntervalDepth, targets.csgRemovedIntervalCapNormal,
            targets.csgRemovedIntervalData, targets.csgRemovedIntervalCount,
        };
        for(const Core::TextureHandle& texture : intervalTextures){
            const Core::GpuGraphResourceId resource = ImportTexture(graph, texture);
            if(!resource.valid())
                return {};
            csgUses.push_back(ReadTextureUse(resource, Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess));
        }
    }

    usize drawTaskIndex = 0u;
    const auto appendDraw = [&](const MaterialPassDrawItem* items, const usize count, const bool csg,
                                const bool compute, const bool generate, const Core::GpuGraphResourceId output){
        if(count == 0u)
            return true;
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratch};
        uses.assign(commonUses.begin(), commonUses.end());
        if(csg)
            uses.insert(uses.end(), csgUses.begin(), csgUses.end());
        if(compute)
            uses.push_back(generate ? WriteUse(output, Core::ResourceStates::UnorderedAccess) : ReadUse(output, Core::ResourceStates::VertexBuffer));
        if(!generate){
            uses.push_back(ReadUse(opaqueDepth));
            uses.push_back(ReadWriteUse(depth, Core::ResourceStates::DepthWrite));
            uses.push_back(ReadWriteUse(normalIor, Core::ResourceStates::RenderTarget));
            uses.push_back(ReadWriteUse(tintCoverage, Core::ResourceStates::RenderTarget));
            uses.push_back(ReadWriteUse(instance, Core::ResourceStates::RenderTarget));
            // This attachment has no enabled color writes or blending and its old contents are irrelevant.
            // A write-only use permits the first capture to discard Undefined contents; ReadWrite would demand
            // an initial state source before AVBOIT's following clear has initialized the scratch image.
            uses.push_back(WriteUse(unusedExtinctionAttachment, Core::ResourceStates::RenderTarget));
        }
        const auto identityText = StringFormat(scratch, "render.refraction.capture.draw_{}", drawTaskIndex++);
        Core::GpuTaskDesc desc = TaskDesc(ToName(identityText), generate ? "Refraction Capture Generate" : "Refraction Capture Raster", dependency);
        desc.setQueue(GraphicsComputeQueueRequest()).setResourceUses(uses.data(), uses.size()).setResourceSetUses(setUses, setUseCount);
        CaptureDrawTask::Payload payload{arena};
        payload.materialSystem = &materialSystem;
        payload.deferredTargets = &targets;
        payload.avboitTargets = targets.avboit;
        payload.frameBindings = frameBindings;
        payload.csgResources = csgResources;
        payload.drawItems.assign(items, items + count);
        payload.instanceCount = instances.size();
        payload.materialTypedByteCount = typedBytes.size();
        payload.csg = csg;
        payload.compute = compute;
        payload.generate = generate;
        dependency = graph.addTask<CaptureDrawTask>(desc, Move(payload));
        return dependency.valid();
    };

    // Native mesh draws batch by CSG mode. Each emulated draw retains an explicit generator/raster pair so
    // shared generated-vertex buffers can never be overwritten before their owning draw consumes them.
    const MaterialPassDrawItems* const orderedSets[] = {&drawItems.regular, &drawItems.csg};
    for(usize setIndex = 0u; setIndex < LengthOf(orderedSets); ++setIndex){
        const MaterialPassDrawItems& items = *orderedSets[setIndex];
        const bool csg = setIndex != 0u;
        if(!appendDraw(items.meshDrawItems.data(), items.meshDrawItems.size(), csg, false, false, {}))
            return {};
        for(const MaterialPassDrawItem& item : items.computeDrawItems){
            const Core::GpuGraphResourceId output = ImportBuffer(graph, item.meshResources.emulationVertexBuffer);
            if(!output.valid() || !appendDraw(&item, 1u, csg, true, true, output)
                || !appendDraw(&item, 1u, csg, true, false, output))
                return {};
        }
    }
    return finalize();
}

};

NWB_IMPL_END
