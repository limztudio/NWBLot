// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_object_geometry_cache.h"

#include "material_system.h"
#include "task_graph_resource_sets.h"

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/common/log.h>
#include <global/basic_string.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_object_geometry_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace RendererTaskGraphDetail;

[[nodiscard]] bool SameDecode(const MaterialPassDrawItem& left, const MaterialPassDrawItem& right){
    const auto& leftCache = left.meshResources.objectGeometryCache;
    const auto& rightCache = right.meshResources.objectGeometryCache;
    if(left.meshKey != right.meshKey || left.meshResources.runtimeMesh != right.meshResources.runtimeMesh
        || left.meshResources.meshletCount != right.meshResources.meshletCount
        || leftCache.buffer != rightCache.buffer || leftCache.heapHandle != rightCache.heapHandle
        || leftCache.decoderPipeline != rightCache.decoderPipeline || leftCache.sourceRevision != rightCache.sourceRevision
        || leftCache.requiresDecode != rightCache.requiresDecode || leftCache.initialized != rightCache.initialized
        || leftCache.indexByteOffset != rightCache.indexByteOffset || leftCache.indexCount != rightCache.indexCount)
        return false;
    Core::Buffer* rightSources[NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT] = {};
    usize sourceIndex = 0u;
    ForEachMaterialPassMeshSourceBuffer(right.meshResources, [&](const Core::BufferHandle& source){
        rightSources[sourceIndex++] = source.get();
    });
    sourceIndex = 0u;
    bool same = true;
    ForEachMaterialPassMeshSourceBuffer(left.meshResources, [&](const Core::BufferHandle& source){
        same = same && source.get() == rightSources[sourceIndex];
        ++sourceIndex;
    });
    for(usize index = 0u; index < NWB_MESH_INSTANCE_GEOMETRY_SLOT_COUNT; ++index)
        same = same && left.meshResources.geometryHeapHandles[index] == right.meshResources.geometryHeapHandles[index];
    return same;
}

[[nodiscard]] Core::GpuTaskDesc TaskDesc(const Name identity, const AStringView label){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setQueue(GraphicsComputeQueueRequest()).setScheduling(scheduling);
    return desc;
}

struct DecodeTask{
    struct Payload{
        RendererMaterialSystem& materialSystem;
        RendererMeshSystem& meshSystem;
        const DeferredFrameTargets& targets;
        MaterialPassDrawItem draw;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        Core::GpuTimingSubmissionTicket** rebindableTimingTicket = nullptr;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        Core::GpuTimingSubmissionTicket* ticket = payload.rebindableTimingTicket
            ? *payload.rebindableTimingTicket : payload.timingTicket;
        Optional<Core::GpuTimingSubmissionTicket::RecordingScope> timing;
        if(ticket)
            timing.emplace(*ticket);
        commandList.endRenderPass();
        const Core::ViewportState viewport;
        const MaterialPassDrawContext context{
            commandList, payload.targets, nullptr, nullptr, viewport, nullptr, payload.frameBindings,
            MaterialPipelinePass::Opaque, false, false, false, true, true, true,
        };
        if(!payload.materialSystem.recordObjectGeometryDecode(context, payload.draw))
            return false;
        commandList.setBufferState(payload.draw.meshResources.objectGeometryCache.buffer.get(), ECSRenderDetail::s_ObjectGeometryRasterState);
        commandList.commitBarriers();
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken&){
        // A replaced generation may reject content publication after accepted work; its output state still belongs to the mesh owner.
        if(!payload.meshSystem.confirmObjectGeometryCache(
            payload.draw.meshKey, payload.draw.meshResources.sourceBuffers,
            payload.draw.meshResources.objectGeometryCache, payload.draw.meshResources.runtimeMesh
        ))
            return;
    }
};

struct ReadyTask{
    struct Payload{};
    [[nodiscard]] static bool record(const Payload&, Core::CommandList&, const Core::GpuTaskRecordContext&){ return true; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ObjectGeometryCacheGraph::ObjectGeometryCacheGraph(
    Core::GpuTaskGraph& graph,
    RendererMaterialSystem& materialSystem,
    RendererMeshSystem& meshSystem,
    Core::Alloc::ScratchArena& arena
)
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_meshSystem(meshSystem)
    , m_entries(arena)
{}

bool ObjectGeometryCacheGraph::prepare(
    const MaterialPassDrawItem* draws,
    const usize drawCount,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const DeferredFrameTargets& targets,
    Core::GpuTaskId& dependency,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& rasterUses,
    Core::Alloc::ScratchArena& scratchArena,
    Core::GpuTimingSubmissionTicket* timingTicket,
    Core::GpuTimingSubmissionTicket** rebindableTimingTicket
){
    using namespace RendererTaskGraphDetail;
    using namespace __hidden_task_graph_object_geometry_cache;
    if(drawCount != 0u && !draws)
        return false;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> cacheReads{scratchArena};
    Vector<Core::GpuTaskId, Core::Alloc::ScratchArena> producers{scratchArena};
    cacheReads.reserve(drawCount);
    producers.reserve(drawCount + 1u);
    for(usize drawIndex = 0u; drawIndex < drawCount; ++drawIndex){
        const MaterialPassDrawItem& draw = draws[drawIndex];
        if(!draw.pipelineResources.indexedPipeline || !draw.pipelineResources.objectGeometryDecodePipeline)
            return false;
        const auto& cache = draw.meshResources.objectGeometryCache;
        if(!cache.valid() || draw.pipelineResources.objectGeometryDecodePipeline != cache.decoderPipeline
            || !frameBindings.bindingValid() || draw.instanceIndex >= frameBindings.instanceBufferCapacity)
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
            Core::GpuGraphResourceDesc bufferDesc = BufferResourceDesc(
                cache.buffer->getCreationDescription().debugName, "Object Geometry Cache"
            );
            bufferDesc.setInitialState(cache.initialized ? ECSRenderDetail::s_ObjectGeometryRasterState : Core::ResourceStates::Common)
                .setExternalFinalState(ECSRenderDetail::s_ObjectGeometryRasterState);
            Entry entry{draw, m_graph.importBuffer(cache.buffer, bufferDesc), {}};
            if(!entry.resource.valid())
                return false;
            if(cache.requiresDecode){
                MaterialPassDrawItems sourceDraws{scratchArena};
                sourceDraws.computeDrawItems.push_back(draw);
                const MaterialPassDrawItems* const sets[] = {&sourceDraws};
                Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
                if(!GatherPreparedMaterialGeometryUses(m_graph, sets, LengthOf(sets), scratchArena, uses))
                    return false;
                uses.reserve(uses.size() + 3u);
                Core::GpuGraphResourceId instances;
                {
                    const Core::GpuTaskGraph::DeclarationReadView declarations(m_graph);
                    if(!declarations.valid())
                        return false;
                    instances = declarations.findImportedBuffer(frameBindings.instanceBuffer);
                }
                if(!instances.valid())
                    return false;
                uses.push_back(ReadBufferUse(instances, Core::BufferRange(
                    static_cast<u64>(draw.instanceIndex) * sizeof(InstanceGpuData), sizeof(InstanceGpuData)
                )));
                uses.push_back(WriteUse(entry.resource, Core::ResourceStates::UnorderedAccess));
                uses.push_back(ReadUse(entry.resource, ECSRenderDetail::s_ObjectGeometryRasterState));
                Core::GpuTaskDesc desc = TaskDesc(ToName(StringFormat(scratchArena,
                    "render.object_geometry.decode_{}", m_entries.size())), "Object Geometry Decode");
                desc.setDependencies(&dependency, 1u).setResourceUses(uses.data(), uses.size());
                entry.producer = m_graph.addTask<DecodeTask>(desc, DecodeTask::Payload{
                    m_materialSystem, m_meshSystem, targets, draw, frameBindings, timingTicket, rebindableTimingTicket,
                });
                if(!entry.producer.valid())
                    return false;
                dependency = entry.producer;
            }
            m_entries.push_back(Move(entry));
            retained = &m_entries.back();
        }
        bool alreadyRead = false;
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
    bool containsDependency = false;
    for(const Core::GpuTaskId producer : producers)
        containsDependency = containsDependency || producer == dependency;
    if(!containsDependency)
        producers.push_back(dependency);
    Core::GpuTaskDesc ready = TaskDesc(ToName(StringFormat(scratchArena,
        "render.object_geometry.ready_{}", m_phaseCount++)), "Object Geometry Ready");
    ready.setDependencies(producers.data(), producers.size()).setResourceUses(cacheReads.data(), cacheReads.size());
    dependency = m_graph.addTask<ReadyTask>(ready, ReadyTask::Payload{});
    if(!dependency.valid())
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

