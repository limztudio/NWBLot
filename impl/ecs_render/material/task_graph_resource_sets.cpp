// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_sets.h"
#include <global/allocation_size.h>
#include <global/hash_utils.h>
#include "generated_geometry_validation.h"
#include "task_graph_compute_emulation_plan.h"
#include "task_graph_opaque_compute_emulation_plan.h"
#include "task_graph_object_geometry_cache.h"
#include "material_system.h"
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <global/basic_string.h>
#include "task_graph_opaque_compute_tasks.h"
#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>, SampledTextureImportFailure::Enum> ImportMaterialSampledTextureResources(
    const Span<const Core::TextureHandle> textures,
    const AStringView markerLabel,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> resources(scratchArena);
    resources.reserve(textures.size());
    for(const Core::TextureHandle& texture : textures){
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return MakeUnexpected(SampledTextureImportFailure::GraphUnavailable);
            resource = declarations.findImportedTexture(texture);
        }
        if(!resource.valid()){
            if(!texture)
                return MakeUnexpected(SampledTextureImportFailure::MissingIdentity);
            const Name identity = texture->getCreationDescription().name;
            if(!identity)
                return MakeUnexpected(SampledTextureImportFailure::MissingIdentity);
            const auto resourceResult = graph.importTexture(texture, RendererTaskGraphDetail::TextureResourceDesc(identity, markerLabel));
            if(!resourceResult)
                return MakeUnexpected(SampledTextureImportFailure::ImportFailed);
            resource = *resourceResult;
        }
        resources.push_back(resource);
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>> GatherPreparedMaterialGeometryUses(const Span<const MaterialPassDrawItemsView> drawItemSets, Core::GpuTaskGraph& graph, Core::Alloc::ScratchArena& scratchArena){
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses(scratchArena);

    // Indexed raster reads its decoded cache; only mesh/compute stages and cache producers consume source streams.
    constexpr usize s_MaxDrawItemCount = Limit<usize>::s_Max / NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT;
    usize drawItemCount = 0u;
    for(const MaterialPassDrawItemsView& drawItems : drawItemSets){
        for(const usize count : { drawItems.meshDrawItems.size(), drawItems.computeDrawItems.size() }){
            const auto nextCount = TryAccumulateMaterialDrawCount(drawItemCount, count, s_MaxDrawItemCount);
            if(!nextCount)
                return MakeUnexpected(Failure{});
            drawItemCount = *nextCount;
        }
    }
    if(drawItemCount == 0u)
        return resourceUses;

    using MeshSourceRef = NotNull<const MaterialPassMeshResourceSnapshot*>;
    Vector<MeshSourceRef, Core::Alloc::ScratchArena> uniqueMeshes{ scratchArena };
    uniqueMeshes.reserve(drawItemCount);
    {
        // Identify source tuples first so repeated instances cost one pointer each.
        const auto hashSources = [](const MeshSourceRef& mesh)noexcept{
            usize hash = 0u;
            ForEachMaterialPassMeshSourceBuffer(*mesh, [&](const Core::BufferHandle& buffer)noexcept{
                HashCombine(hash, buffer.get());
            });
            return hash;
        };
        const auto equalSources = [](const MeshSourceRef& lhs, const MeshSourceRef& rhs)noexcept{
            return lhs.get() == rhs.get() || lhs->sourceBuffers.sameBuffers(rhs->sourceBuffers);
        };
        using MeshSourceSet = HashSet<MeshSourceRef, Core::Alloc::ScratchArena, RemoveConst_T<decltype(hashSources)>, RemoveConst_T<decltype(equalSources)>>;
        Optional<MeshSourceSet> meshSources;
        if(drawItemCount > 1u)
            meshSources.emplace(AddSize(drawItemCount, drawItemCount), hashSources, equalSources, scratchArena);
        const auto appendDrawItem = [&](const MaterialPassDrawItem& drawItem){
            const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
            // Descriptors stay valid on repeated source tuples.
            if(!mesh.valid())
                return false;
            if(!meshSources || meshSources->insert(MeshSourceRef(&mesh)).second)
                uniqueMeshes.push_back(MeshSourceRef(&mesh));
            return true;
        };
        for(const MaterialPassDrawItemsView& drawItems : drawItemSets){
            for(const MaterialPassDrawItem& drawItem : drawItems.meshDrawItems){
                if(!appendDrawItem(drawItem))
                    return MakeUnexpected(Failure{});
            }
            for(const MaterialPassDrawItem& drawItem : drawItems.computeDrawItems){
                if(!appendDrawItem(drawItem))
                    return MakeUnexpected(Failure{});
            }
        }
    }

    const usize sourceBufferCapacity = uniqueMeshes.size() * NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT;
    resourceUses.reserve(sourceBufferCapacity);
    using BufferRef = NotNull<const Core::BufferHandle*>;
    Vector<BufferRef, Core::Alloc::ScratchArena> sourceBuffers{ scratchArena };
    sourceBuffers.reserve(sourceBufferCapacity);
    {
        using BufferPtr = NotNull<Core::Buffer*>;
        HashSet<BufferPtr, Core::Alloc::ScratchArena, Hasher<BufferPtr>, EqualTo<BufferPtr>> sourceBufferIdentities(AddSize(sourceBufferCapacity, sourceBufferCapacity), Hasher<BufferPtr>(), EqualTo<BufferPtr>(), scratchArena);
        for(const MeshSourceRef& mesh : uniqueMeshes){
            ForEachMaterialPassMeshSourceBuffer(*mesh, [&](const Core::BufferHandle& buffer){
                if(sourceBufferIdentities.insert(MakeNotNull(buffer.get())).second)
                    sourceBuffers.push_back(BufferRef(&buffer));
            });
        }
    }

    resourceUses.resize(sourceBuffers.size());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        for(usize bufferIndex = 0u; bufferIndex < sourceBuffers.size(); ++bufferIndex)
            resourceUses[bufferIndex].resource = declarations.findImportedBuffer(*sourceBuffers[bufferIndex]);
    }
    for(usize bufferIndex = 0u; bufferIndex < sourceBuffers.size(); ++bufferIndex){
        const Core::BufferHandle& buffer = *sourceBuffers[bufferIndex];
        Core::GpuGraphResourceId resource = resourceUses[bufferIndex].resource;
        if(!resource.valid()){
            const Name identity = buffer->getCreationDescription().debugName;
            if(!identity)
                return MakeUnexpected(Failure{});
            const auto importedResource = graph.importBuffer(buffer, BufferResourceDesc(identity, "Prepared Material Geometry"));
            if(!importedResource)
                return MakeUnexpected(Failure{});
            resource = *importedResource;
        }
        if(!resource.valid())
            return MakeUnexpected(Failure{});
        resourceUses[bufferIndex] = ReadUse(resource, Core::ResourceStates::ShaderResource);
    }
    return resourceUses;
}

Expected<Core::GpuGraphResourceSetId> GatherPreparedMaterialGeometryResourceSet(
    const Span<const MaterialPassDrawItemsView> drawItemSets,
    const Name& identity,
    const AStringView label,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    Core::GpuGraphResourceSetId resource{};
    const auto resourceUses = GatherPreparedMaterialGeometryUses(drawItemSets, graph, scratchArena);
    if(!resourceUses)
        return MakeUnexpected(Failure{});

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(resourceUses->size());
    for(const Core::GpuTaskResourceUse& use : *resourceUses)
        members.push_back(use.resource);

    const auto resourceResult = graph.importResourceSet(Core::GpuGraphResourceSetDesc{}.setIdentity(identity).setMarkerLabel(label).setMembers(members));
    if(!resourceResult)
        return MakeUnexpected(Failure{});
    resource = *resourceResult;
    return resource;
}

Expected<Core::GpuGraphResourceId> GatherRegularSharedComputeEmulationResource(Core::GpuTaskGraph& graph, const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& plan, const AStringView label){
    if(!plan.captured || !plan.outputBuffer)
        return MakeUnexpected(Failure{});
    return FindOrImportBuffer(graph, plan.outputBuffer, label);
}

Expected<Core::GpuGraphResourceSetId> GatherPreparedMaterialSampledTextureResourceSet(
    const RendererMaterialSystem& materialSystem,
    const Span<const MaterialPassDrawItemsView> drawItemSets,
    const Name& identity,
    const AStringView label,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    Core::GpuGraphResourceSetId resource{};
    const RendererMaterialState& materialState = materialSystem.materialState();
    const auto sampledTextures = GatherPreparedMaterialPassSampledTextures(materialState.m_surfaceInfos, materialState.m_resourceState, drawItemSets, scratchArena);
    if(!sampledTextures)
        return MakeUnexpected(Failure{});

    const auto members = ImportMaterialSampledTextureResources(*sampledTextures, "Prepared Material Sampled Texture", graph, scratchArena);
    if(!members)
        return MakeUnexpected(Failure{});
    if(members->empty())
        return resource;

    const auto resourceResult2 = graph.importResourceSet(Core::GpuGraphResourceSetDesc{}.setIdentity(identity).setMarkerLabel(label).setMembers(*members));
    if(!resourceResult2)
        return MakeUnexpected(Failure{});
    resource = *resourceResult2;
    return resource;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MatchesRetainedGeneratedGeometryOutputs(
    const b8 captured,
    const Span<const MaterialPassDrawItem> drawItems,
    const Span<const Core::BufferHandle> outputBuffers,
    const Span<const GeneratedGeometryOutputLayout> outputLayouts,
    const Span<const u32> outputHeapSlots
)noexcept{
    if(!captured || outputBuffers.size() != drawItems.size() || outputLayouts.size() != drawItems.size() || outputHeapSlots.size() != drawItems.size())
        return false;
    for(usize drawIndex = 0u; drawIndex < drawItems.size(); ++drawIndex){
        const MaterialPassMeshResourceSnapshot& mesh = drawItems[drawIndex].meshResources;
        if(
            !mesh.emulationVertexBuffer
            || !mesh.emulationVertexHeapHandle.valid()
            || mesh.emulationVertexBuffer.get() != outputBuffers[drawIndex].get()
            || !outputLayouts[drawIndex].matches(mesh.emulationIndexByteOffset, drawItems[drawIndex].pipelineResources.indexedGeometryOutput)
            || mesh.emulationVertexHeapHandle.slot() != outputHeapSlots[drawIndex]
        )
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AliasFreeComputeEmulationGraphPlan::reset()noexcept{
    drawItems.clear();
    outputBuffers.clear();
    outputLayouts.clear();
    outputHeapSlots.clear();
    captured = false;
}

b8 AliasFreeComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& sourceDrawItems, Core::Alloc::ScratchArena& scratchArena){
    reset();
    if(sourceDrawItems.computeDrawItems.empty())
        return false;

    if(!CaptureAliasFreeGeneratedGeometryDraws(sourceDrawItems, [](const MaterialPipelineCsgMode::Enum mode)noexcept{ return mode == MaterialPipelineCsgMode::None; }, *this, scratchArena)){
        reset();
        return false;
    }
    captured = drawItems.size() == outputBuffers.size() && outputBuffers.size() == outputHeapSlots.size() && !drawItems.empty();
    return captured;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueRegularComputeEmulationGraphPlan::reset()noexcept{
    meshDrawItems.clear();
    indexedDrawItems.clear();
    AliasFreeComputeEmulationGraphPlan::reset();
}

b8 OpaqueRegularComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& sourceDrawItems, Core::Alloc::ScratchArena& scratchArena){
    reset();
    if(sourceDrawItems.computeDrawItems.empty())
        return false;

    meshDrawItems.reserve(sourceDrawItems.meshDrawItems.size());
    meshDrawItems.assign(sourceDrawItems.meshDrawItems.begin(), sourceDrawItems.meshDrawItems.end());
    indexedDrawItems.reserve(sourceDrawItems.indexedDrawItems.size());
    indexedDrawItems.assign(sourceDrawItems.indexedDrawItems.begin(), sourceDrawItems.indexedDrawItems.end());
    if(!AliasFreeComputeEmulationGraphPlan::capture(sourceDrawItems, scratchArena)){
        reset();
        return false;
    }
    return captured;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RegularSharedComputeEmulationGraphPlan::reset()noexcept{
    for(MaterialPassDrawItem& drawItem : drawItems)
        drawItem = {};
    for(GeneratedGeometryEquivalenceKey& key : equivalenceKeys)
        key = {};
    outputBuffer = nullptr;
    outputHeapSlot = 0u;
    drawCount = 0u;
    captured = false;
}

b8 RegularSharedComputeEmulationGraphPlan::capture(const MaterialPassDrawItemsView& sourceDrawItems, const usize allowedMaxDrawCount)noexcept{
    reset();
    if(!IsSupportedSharedComputeEmulationDrawCount(allowedMaxDrawCount) || sourceDrawItems.computeDrawItems.size() < s_SharedComputeEmulationMinimumDrawCount || sourceDrawItems.computeDrawItems.size() > allowedMaxDrawCount)
        return false;

    drawCount = sourceDrawItems.computeDrawItems.size();
    for(usize drawIndex = 0u; drawIndex < drawCount; ++drawIndex){
        const MaterialPassDrawItem& drawItem = sourceDrawItems.computeDrawItems[drawIndex];
        if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None)
            return false;

        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        if(!mesh.emulationVertexBuffer || !mesh.emulationVertexHeapHandle.valid())
            return false;

        if(drawIndex == 0u){
            outputBuffer = mesh.emulationVertexBuffer;
            outputHeapSlot = mesh.emulationVertexHeapHandle.slot();
        }
        else if(mesh.emulationVertexBuffer.get() != outputBuffer.get() || mesh.emulationVertexHeapHandle.slot() != outputHeapSlot)
            return false;

        drawItems[drawIndex] = drawItem;
        equivalenceKeys[drawIndex] = MakeGeneratedGeometryEquivalenceKey(
            drawItem.meshKey,
            drawItem.pipelineKey.material,
            drawItem.pipelineKey.pass,
            drawItem.instanceIndex,
            drawItem.materialConstantByteOffset,
            drawItem.shadingModelId,
            mesh.emulationVertexHeapHandle.valid(),
            mesh.emulationVertexHeapHandle.slot(),
            mesh.emulationIndexByteOffset,
            drawItem.pipelineResources.indexedGeometryOutput
        );
    }
    captured = static_cast<b8>(outputBuffer);
    return captured;
}

b8 RegularSharedComputeEmulationGraphPlan::matches(const usize drawIndex)const noexcept{
    if(!captured || drawIndex >= drawCount || !outputBuffer)
        return false;

    const MaterialPassDrawItem& drawItem = drawItems[drawIndex];
    const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
    return
        mesh.emulationVertexBuffer
        && mesh.emulationVertexHeapHandle.valid()
        && mesh.emulationVertexBuffer.get() == outputBuffer.get()
        && mesh.emulationVertexHeapHandle.slot() == outputHeapSlot
        && equivalenceKeys[drawIndex].matches(
            drawItem.meshKey,
            drawItem.pipelineKey.material,
            drawItem.pipelineKey.pass,
            drawItem.pipelineKey.csgMode,
            drawItem.instanceIndex,
            drawItem.materialConstantByteOffset,
            drawItem.shadingModelId,
            mesh.emulationVertexHeapHandle.valid(),
            mesh.emulationVertexHeapHandle.slot(),
            mesh.emulationIndexByteOffset,
            drawItem.pipelineResources.indexedGeometryOutput
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_object_geometry_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace RendererTaskGraphDetail;

[[nodiscard]] b8 SameDecode(const MaterialPassDrawItem& left, const MaterialPassDrawItem& right)noexcept{
    const auto& leftCache = left.meshResources.objectGeometryCache;
    const auto& rightCache = right.meshResources.objectGeometryCache;
    if(
        left.meshKey != right.meshKey || left.meshResources.runtimeMesh != right.meshResources.runtimeMesh
        || left.meshResources.meshletCount != right.meshResources.meshletCount
        || leftCache.buffer != rightCache.buffer || leftCache.heapHandle != rightCache.heapHandle
        || leftCache.decoderPipeline != rightCache.decoderPipeline || leftCache.sourceRevision != rightCache.sourceRevision
        || leftCache.requiresDecode != rightCache.requiresDecode || leftCache.initialized != rightCache.initialized
        || leftCache.indexByteOffset != rightCache.indexByteOffset || leftCache.indexCount != rightCache.indexCount
    )
        return false;
    if(!left.meshResources.sourceBuffers.sameBuffers(right.meshResources.sourceBuffers))
        return false;
    for(usize index = 0u; index < NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT; ++index){
        if(left.meshResources.geometryHeapHandles[index] != right.meshResources.geometryHeapHandles[index])
            return false;
    }
    return true;
}

struct DecodeTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    struct Payload{
        RendererMaterialSystem& materialSystem;
        RendererMeshSystem& meshSystem;
        MaterialPassDrawItem draw;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Core::GpuTimingSubmissionTicket** rebindableTimingTicket = nullptr;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        Core::GpuTimingSubmissionTicket* ticket = payload.rebindableTimingTicket ? *payload.rebindableTimingTicket : payload.timingTicket;
        Optional<Core::GpuTimingSubmissionTicket::RecordingScope> timing;
        if(ticket)
            timing.emplace(*ticket);
        commandList.endRenderPass();
        const Core::ViewportState viewport;
        const MaterialPassDrawContext context{ commandList, nullptr, nullptr, viewport, nullptr, payload.frameBindings, MaterialPipelinePass::Opaque, true, true, true, };
        if(!payload.materialSystem.recordObjectGeometryDecode(context, payload.draw))
            return false;
        commandList.setBufferState(payload.draw.meshResources.objectGeometryCache.buffer.get(), ECSRenderDetail::s_ObjectGeometryRasterState);
        commandList.commitBarriers();
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken&)noexcept{
        // A replaced generation may reject content publication after accepted work; its output state still belongs to the mesh owner.
        if(!payload.meshSystem.confirmObjectGeometryCache(payload.draw.meshKey, payload.draw.meshResources.sourceBuffers, payload.draw.meshResources.objectGeometryCache, payload.draw.meshResources.runtimeMesh))
            return;
    }
};

struct ReadyTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
    struct Payload{};


public:
    [[nodiscard]] static b8 Record(const Payload&, Core::CommandList&, const Core::GpuTaskRecordContext&)noexcept{ return true; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ObjectGeometryCacheGraph::prepare(
    const Span<const MaterialPassDrawItem> draws,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    Core::Alloc::ScratchArena& scratchArena,
    Core::GpuTaskId& dependency,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& rasterUses,
    Core::GpuTimingSubmissionTicket* timingTicket,
    Core::GpuTimingSubmissionTicket** rebindableTimingTicket
){
    using namespace RendererTaskGraphDetail;
    using namespace __hidden_task_graph_object_geometry_cache;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> cacheReads{scratchArena};
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> producers{scratchArena};
    cacheReads.reserve(draws.size());
    producers.reserve(draws.size() + 1u);
    for(const MaterialPassDrawItem& draw : draws){
        if(!draw.pipelineResources.indexedPipeline || !draw.pipelineResources.objectGeometryDecodePipeline)
            return false;
        const auto& cache = draw.meshResources.objectGeometryCache;
        if(!cache.valid() || draw.pipelineResources.objectGeometryDecodePipeline != cache.decoderPipeline || !frameBindings.bindingValid() || draw.instanceIndex >= frameBindings.instanceBufferCapacity)
            return false;
        Entry* retained = nullptr;
        for(Entry& entry : m_entries){
            if(entry.draw.meshResources.objectGeometryCache.buffer == cache.buffer){
                if(!SameDecode(entry.draw, draw))
                    return false;
                retained = &entry;
                break;
            }
        }
        if(!retained){
            Core::GpuGraphResourceDesc bufferDesc = BufferResourceDesc(cache.buffer->getCreationDescription().debugName, "Object Geometry Cache");
            bufferDesc.setInitialState(cache.initialized ? ECSRenderDetail::s_ObjectGeometryRasterState : Core::ResourceStates::Common).setExternalFinalState(ECSRenderDetail::s_ObjectGeometryRasterState);
            Entry entry{draw, {}, {}};
            const auto importedResource = m_graph.importBuffer(cache.buffer, bufferDesc);
            if(!importedResource)
                return false;
            entry.resource = *importedResource;
            if(cache.requiresDecode){
                const Array<MaterialPassDrawItemsView, 1u> sets = {
                    MaterialPassDrawItemsView{ {}, {}, Span<const MaterialPassDrawItem>(&draw, 1u) },
                };
                auto gatheredUses = GatherPreparedMaterialGeometryUses(sets, m_graph, scratchArena);
                if(!gatheredUses)
                    return false;
                auto& uses = *gatheredUses;
                uses.reserve(uses.size() + s_DecodeExtraResourceUseCount);
                Core::GpuGraphResourceId instances;
                {
                    const Core::GpuTaskGraph::DeclarationReadView declarations(m_graph);
                    if(!declarations.valid())
                        return false;
                    instances = declarations.findImportedBuffer(frameBindings.instanceBuffer);
                }
                if(!instances.valid())
                    return false;
                uses.push_back(ReadBufferUse(instances, Core::BufferRange(static_cast<u64>(draw.instanceIndex) * sizeof(InstanceGpuData), sizeof(InstanceGpuData))));
                uses.push_back(WriteUse(entry.resource, Core::ResourceStates::UnorderedAccess));
                uses.push_back(ReadUse(entry.resource, ECSRenderDetail::s_ObjectGeometryRasterState));
                const auto identity = ToName(StringFormat(scratchArena, "render.object_geometry.decode_{}", m_entries.size()));
                if(!identity || !*identity)
                    return false;
                Core::GpuTaskDesc desc = TaskDesc(**identity, "Object Geometry Decode");
                desc.setDependencies({ &dependency, 1u }).setResourceUses(uses);
                const auto entryProducerResult = m_graph.addTask<DecodeTask>(desc, DecodeTask::Payload{ m_materialSystem, m_meshSystem, draw, frameBindings, timingTicket, rebindableTimingTicket, });
                if(!entryProducerResult){
                    entry.producer = {};
                    return false;
                }
                entry.producer = *entryProducerResult;
                dependency = entry.producer;
            }
            m_entries.push_back(Move(entry));
            retained = &m_entries.back();
        }
        b8 alreadyRead = false;
        for(const Core::GpuTaskResourceUse& use : cacheReads)
            alreadyRead = alreadyRead || use.resource == retained->resource;
        if(!alreadyRead){
            cacheReads.push_back(ReadUse(retained->resource, ECSRenderDetail::s_ObjectGeometryRasterState));
            if(retained->producer.valid())
                producers.push_back(retained->producer);
        }
    }
    if(cacheReads.empty())
        return true;
    rasterUses.insert(rasterUses.end(), cacheReads.begin(), cacheReads.end());
    // Persistent read-only geometry needs no synthetic phase task when this frame produced no cache writes.
    if(producers.empty())
        return true;
    if(!dependency.valid())
        return false;
    b8 containsDependency = false;
    for(const Core::GpuTaskId producer : producers)
        containsDependency = containsDependency || producer == dependency;
    if(!containsDependency)
        producers.push_back(dependency);
    const auto readyIdentity = ToName(StringFormat(scratchArena, "render.object_geometry.ready_{}", m_phaseCount++));
    if(!readyIdentity || !*readyIdentity){
        dependency = {};
        return false;
    }
    Core::GpuTaskDesc ready = TaskDesc(**readyIdentity, "Object Geometry Ready");
    ready.setDependencies(producers).setResourceUses(cacheReads);
    const auto dependencyResult = m_graph.addTask<ReadyTask>(ready, ReadyTask::Payload{});
    if(!dependencyResult){
        dependency = {};
        return false;
    }
    dependency = *dependencyResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 OpaqueRegularComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.plan.captured)
        return false;

    RendererMaterialSystem& materialSystem = payload.materialSystem;
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    const b8 frameSetupReady = FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    if(!frameSetupReady)
        return true;

    // Reject corrupted streams instead of dispatching outside the imported handle set.
    if(!payload.plan.matches())
        return false;
    const b8 drawResourcesReady =
        materialSystem.drawRecorder().meshMaterialPassDrawResourcesReady(payload.plan.meshDrawItems, payload.frameBindings)
        && materialSystem.drawRecorder().indexedMaterialPassDrawResourcesReady(payload.plan.indexedDrawItems, payload.frameBindings)
        && materialSystem.drawRecorder().computeMaterialPassDrawResourcesReady(payload.plan.drawItems, payload.frameBindings)
    ;
    if(!payload.materialDrawBuffersUploaded || !payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount) || !drawResourcesReady)
        return true;

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(payload.targets.framebuffer->getFramebufferInfo().getViewport());
    const MaterialPassDrawContext drawContext{ commandList, nullptr, nullptr, deferredViewportState, nullptr, payload.frameBindings, MaterialPipelinePass::Opaque, payload.materialFrameStatesGraphOwned, payload.materialGeometryStatesGraphOwned, true };
    materialSystem.drawRecorder().generateComputeMaterialPassDrawItems(drawContext, payload.plan.drawItems);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 OpaqueRegularSharedComputeEmulationGraphTask::Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
    if(!payload.plan.captured)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    // G-buffer starts the preserved range only when frozen resources are ready; later miss is a no-op.
    if(!payload.opaqueRegularTiming.has_value()){
        if(payload.phase == SharedComputeEmulationPhase::Raster)
            commandList.endRenderPass();
        return true;
    }

    RendererMaterialSystem& materialSystem = payload.materialSystem;
    const b8 frameSetupReady = FrameSetupReady(payload.meshViewSetupReady, payload.sceneShadingSetupReady);
    if(!frameSetupReady || !payload.plan.matches(payload.drawIndex))
        return false;

    const Span<const MaterialPassDrawItem> drawItems(&payload.plan.drawItems[payload.drawIndex], 1u);
    if(!payload.materialDrawBuffersUploaded || !payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount) || !materialSystem.drawRecorder().computeMaterialPassDrawResourcesReady(drawItems, payload.frameBindings)){
        Core::DiscardGpuTimingMeasure(&payload.opaqueRegularTiming);
        if(payload.phase == SharedComputeEmulationPhase::Raster)
            commandList.endRenderPass();
        return true;
    }

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(payload.targets.framebuffer->getFramebufferInfo().getViewport());
    const MaterialPassDrawContext drawContext{
        commandList,
        payload.phase == SharedComputeEmulationPhase::Raster ? payload.targets.framebuffer.get() : nullptr,
        nullptr,
        deferredViewportState,
        nullptr,
        payload.frameBindings,
        MaterialPipelinePass::Opaque,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        true
    };
    if(payload.phase == SharedComputeEmulationPhase::Generate)
        materialSystem.drawRecorder().generateComputeMaterialPassDrawItems(drawContext, drawItems);
    else{
        materialSystem.drawRecorder().renderComputeMaterialPassDrawItemsRasterOnly(drawContext, drawItems);
        if(payload.finishTiming){
            payload.opaqueRegularTiming.value().finishTiming(commandList);
            payload.opaqueRegularTiming.reset();
        }
        commandList.endRenderPass();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
