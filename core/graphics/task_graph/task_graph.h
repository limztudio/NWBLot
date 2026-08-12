// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_desc.h"

#include <core/alloc/scratch.h>
#include <global/arena_object.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraphAnalysis;
class GpuTaskGraphQueueAssignments;
class GpuCompiledGraph;
struct GpuCompiledBarrier;

namespace Telemetry{
    class FrameGraphBuilder;
};


struct GpuTaskGraphTaskView{
    GpuTaskId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuQueueRequest queue;
    GpuTaskSchedulingHint scheduling;
    const GpuTaskId* dependencies = nullptr;
    usize dependencyCount = 0u;
    const GpuExternalCompletionId* externalDependencies = nullptr;
    usize externalDependencyCount = 0u;
    const GpuTaskResourceUse* resourceUses = nullptr;
    usize resourceUseCount = 0u;
    bool hasPayload = false;
};

struct GpuTaskGraphResourceView{
    GpuGraphResourceId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    bool hasBackendResource = false;
};

struct GpuTaskGraphPipelineView{
    GpuGraphPipelineId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuGraphPipelineType::Enum type = GpuGraphPipelineType::kCount;
    bool hasBackendPipeline = false;
};

struct GpuTaskGraphExternalCompletionView{
    GpuExternalCompletionId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
};

struct GpuTaskGraphTelemetryOptions{
    const GpuTaskGraphQueueAssignments* queueAssignments = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph final : NoCopy{
    friend class GpuTaskGraphCompiler;

private:
    struct GpuTaskNode{
        Name identity = NAME_NONE;
        GpuQueueRequest queue;
        GpuTaskSchedulingHint scheduling;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        u32 dependencyOffset = 0u;
        u32 dependencyCount = 0u;
        u32 externalDependencyOffset = 0u;
        u32 externalDependencyCount = 0u;
        u32 resourceUseOffset = 0u;
        u32 resourceUseCount = 0u;
        void* payload = nullptr;
        GpuTaskRecordThunk recordPayload = nullptr;
        GpuTaskAcceptedThunk acceptPayload = nullptr;
        GpuTaskDiscardedThunk discardPayload = nullptr;
        GpuTaskPayloadDestroyThunk destroyPayload = nullptr;
    };

    struct GpuGraphResourceNode{
        Name identity = NAME_NONE;
        GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
        ResourceStates::Mask initialState = ResourceStates::Unknown;
        ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        TextureHandle texture;
        BufferHandle buffer;
        RayTracingAccelStructHandle accelStruct;
    };

    struct GpuGraphPipelineNode{
        Name identity = NAME_NONE;
        GpuGraphPipelineType::Enum type = GpuGraphPipelineType::kCount;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        GraphicsPipelineHandle graphicsPipeline;
        ComputePipelineHandle computePipeline;
        MeshletPipelineHandle meshletPipeline;
        RayTracingPipelineHandle rayTracingPipeline;
    };

    struct GpuExternalCompletionNode{
        Name identity = NAME_NONE;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
    };

    struct GpuUploadBlobNode{
        explicit GpuUploadBlobNode(GraphicsArena& arena)
            : bytes(arena)
        {}

        GraphicsBytes bytes;
    };


public:
    explicit GpuTaskGraph(GraphicsArena& arena);
    ~GpuTaskGraph();


public:
    // Metadata-only tasks support graph analysis and scheduling. A task recorded by GpuNativePacketRecorder must
    // provide a payload thunk through the templated overload below.
    [[nodiscard]] GpuTaskId addTask(const GpuTaskDesc& desc);

    // Adds a graph-owned native buffer-copy task. The helper derives CopySource/CopyDest resource uses from its
    // regions and retains the imported buffers through recording, so desc must declare Transfer capability and
    // must not provide separate resource uses.
    [[nodiscard]] GpuTaskId addCopyBufferTask(const GpuTaskDesc& desc, const GpuCopyBufferTaskDesc& copyDesc);

    // Adds a graph-owned native texture-copy task. The helper derives CopySource/CopyDest resource uses from its
    // regions and retains the imported textures through recording, so desc must declare Transfer capability and
    // must not provide separate resource uses.
    [[nodiscard]] GpuTaskId addCopyTextureTask(const GpuTaskDesc& desc, const GpuCopyTextureTaskDesc& copyDesc);

    // Copies caller-owned bytes into graph-owned CPU storage. `alignment` must be a nonzero power of two; blobs
    // expose only an opaque byte view, so no typed-alignment promise escapes the graph. Built-in upload tasks resolve
    // the immutable blob while recording, then use the ordinary CommandList staging allocator for GPU lifetime.
    [[nodiscard]] GpuUploadBlobId copyUploadData(
        const void* data,
        usize byteSize,
        usize alignment = alignof(u8)
    );

    // Adds graph-owned buffer/texture uploads. Their single resource use describes the state visible after the
    // task's internal CopyDest write and optional final transition, so later graph packets see exact final state.
    [[nodiscard]] GpuTaskId addUploadBufferTask(const GpuTaskDesc& desc, const GpuUploadBufferTaskDesc& uploadDesc);
    [[nodiscard]] GpuTaskId addUploadTextureTask(const GpuTaskDesc& desc, const GpuUploadTextureTaskDesc& uploadDesc);

    // Adds a graph-owned native uint-buffer clear. The helper retains the imported buffer and derives its CopyDest
    // write declaration, so desc must declare Transfer capability and must not provide separate resource uses.
    [[nodiscard]] GpuTaskId addClearBufferTask(const GpuTaskDesc& desc, const GpuClearBufferTaskDesc& clearDesc);

    // Adds a graph-owned native texture clear. The helper retains the imported texture and derives its CopyDest
    // write declaration, so desc must declare Transfer capability and must not provide separate resource uses.
    [[nodiscard]] GpuTaskId addClearTextureTask(const GpuTaskDesc& desc, const GpuClearTextureTaskDesc& clearDesc);

    template<typename TaskT>
    [[nodiscard]] GpuTaskId addTask(const GpuTaskDesc& desc, typename TaskT::Payload&& payload){
        using Payload = typename TaskT::Payload;

        Payload* const storedPayload = NewArenaObject<Payload>(m_arena, Move(payload));
        if(!storedPayload)
            return {};

        GpuTaskRecordThunk recordPayload = nullptr;
        if constexpr(requires(const Payload& value, CommandList& commandList, const GpuTaskRecordContext& context){
            { TaskT::record(value, commandList, context) } -> SameAs<bool>;
        })
            recordPayload = &RecordPayload<TaskT>;

        GpuTaskAcceptedThunk acceptPayload = nullptr;
        if constexpr(requires(Payload& value, const QueueSubmissionToken& token){
            TaskT::accepted(value, token);
        })
            acceptPayload = &AcceptPayload<TaskT>;

        GpuTaskDiscardedThunk discardPayload = nullptr;
        if constexpr(requires(Payload& value){
            TaskT::discarded(value);
        })
            discardPayload = &DiscardPayload<TaskT>;

        const GpuTaskId task = appendTask(
            desc,
            storedPayload,
            recordPayload,
            acceptPayload,
            discardPayload,
            &DestroyPayload<Payload>
        );
        if(!task.valid())
            DestroyArenaObject(m_arena, storedPayload);
        return task;
    }

    // This form is useful for abstract resources and conservative bindless hazard domains during the metadata-only
    // phase. Tasks that will be recorded later must use a typed import overload so the graph retains the resource.
    [[nodiscard]] GpuGraphResourceId importResource(const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuGraphResourceId importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuGraphResourceId importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc);
    // Reuses a typed buffer import whose identity may have been chosen by an earlier producer. This lets later
    // consumers add resource uses for the same physical buffer without inventing incompatible graph metadata.
    [[nodiscard]] GpuGraphResourceId findImportedBuffer(const BufferHandle& buffer)const noexcept;
    [[nodiscard]] GpuGraphResourceId importAccelStruct(
        const RayTracingAccelStructHandle& accelStruct,
        const GpuGraphResourceDesc& desc
    );
    [[nodiscard]] GpuGraphResourceId importHazardDomain(const GpuGraphResourceDesc& desc);
    // Pipeline IDs are graph-local side-table entries.  Metadata-only entries support analysis/capture setup;
    // typed imports retain a stable engine handle until native recording or later IR replay resolves it.
    [[nodiscard]] GpuGraphPipelineId importPipeline(const GpuGraphPipelineDesc& desc);
    [[nodiscard]] GpuGraphPipelineId importGraphicsPipeline(
        const GraphicsPipelineHandle& pipeline,
        const GpuGraphPipelineDesc& desc
    );
    [[nodiscard]] GpuGraphPipelineId importComputePipeline(
        const ComputePipelineHandle& pipeline,
        const GpuGraphPipelineDesc& desc
    );
    [[nodiscard]] GpuGraphPipelineId importMeshletPipeline(
        const MeshletPipelineHandle& pipeline,
        const GpuGraphPipelineDesc& desc
    );
    [[nodiscard]] GpuGraphPipelineId importRayTracingPipeline(
        const RayTracingPipelineHandle& pipeline,
        const GpuGraphPipelineDesc& desc
    );
    [[nodiscard]] GpuExternalCompletionId importExternalCompletion(const GpuExternalCompletionDesc& desc);

    void reset();

    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }
    [[nodiscard]] bool validTask(const GpuTaskId& id)const noexcept;
    [[nodiscard]] bool validResource(const GpuGraphResourceId& id)const noexcept;
    [[nodiscard]] bool validUploadBlob(const GpuUploadBlobId& id)const noexcept;
    [[nodiscard]] bool validPipeline(const GpuGraphPipelineId& id)const noexcept;
    [[nodiscard]] bool validExternalCompletion(const GpuExternalCompletionId& id)const noexcept;
    [[nodiscard]] usize taskCount()const noexcept{ return m_tasks.size(); }
    [[nodiscard]] usize resourceCount()const noexcept{ return m_resources.size(); }
    [[nodiscard]] usize uploadBlobCount()const noexcept{ return m_uploadBlobs.size(); }
    [[nodiscard]] usize pipelineCount()const noexcept{ return m_pipelines.size(); }
    [[nodiscard]] usize externalCompletionCount()const noexcept{ return m_externalCompletions.size(); }
    [[nodiscard]] GpuTaskGraphTaskView taskAt(usize index)const;
    [[nodiscard]] GpuTaskGraphResourceView resourceAt(usize index)const;
    [[nodiscard]] GpuTaskGraphPipelineView pipelineAt(usize index)const;
    [[nodiscard]] GpuTaskGraphExternalCompletionView externalCompletionAt(usize index)const;
    [[nodiscard]] Texture* textureForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] Buffer* bufferForResource(const GpuGraphResourceId& resource)const noexcept;
    // Immutable byte view for graph-owned task recorders. Callers must consume it only while the graph generation
    // remains valid; `outByteSize` is zero and the return value is null for an invalid/stale blob handle.
    [[nodiscard]] const void* uploadBlobData(const GpuUploadBlobId& blob, usize& outByteSize)const noexcept;
    [[nodiscard]] GraphicsPipeline* graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] ComputePipeline* computePipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] MeshletPipeline* meshletPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] RayTracingPipeline* rayTracingPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] bool recordTask(
        const GpuTaskId& task,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    )const;
    // Lowers a compiler-owned packet-boundary barrier through the existing CommandList state tracker.  Task thunks
    // retain responsibility only for barriers internal to their own command sequence.
    [[nodiscard]] bool applyCompiledBarrier(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledBarrier& barrier,
        CommandList& commandList
    )const;
    // Materializes retained buffer states in the native tracker after compiler barriers lower. This preserves
    // graph-owned packet handoffs when a required state already matches an imported automatic-state buffer and
    // therefore needs no Vulkan transition command.
    [[nodiscard]] bool seedTaskRetainedBufferStates(
        const GpuTaskId& task,
        CommandList& commandList
    )const;
    void acceptTask(const GpuTaskId& task, const QueueSubmissionToken& token)noexcept;
    void discardTask(const GpuTaskId& task)noexcept;
    [[nodiscard]] bool appendFrameGraphTelemetry(
        Telemetry::FrameGraphBuilder& builder,
        const GpuTaskGraphAnalysis& analysis,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraphTelemetryOptions& options = {}
    )const;


private:
    template<typename TaskT>
    static bool RecordPayload(
        const void* const payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        using Payload = typename TaskT::Payload;
        return TaskT::record(*static_cast<const Payload*>(payload), commandList, context);
    }
    template<typename TaskT>
    static void AcceptPayload(void* const payload, const QueueSubmissionToken& token){
        using Payload = typename TaskT::Payload;
        TaskT::accepted(*static_cast<Payload*>(payload), token);
    }
    template<typename TaskT>
    static void DiscardPayload(void* const payload){
        using Payload = typename TaskT::Payload;
        TaskT::discarded(*static_cast<Payload*>(payload));
    }
    template<typename PayloadT>
    static void DestroyPayload(GraphicsArena& arena, void* payload)noexcept{
        DestroyArenaObject(arena, static_cast<PayloadT*>(payload));
    }

    [[nodiscard]] GpuTaskId appendTask(
        const GpuTaskDesc& desc,
        void* payload,
        GpuTaskRecordThunk recordPayload,
        GpuTaskAcceptedThunk acceptPayload,
        GpuTaskDiscardedThunk discardPayload,
        GpuTaskPayloadDestroyThunk destroyPayload
    );
    [[nodiscard]] GpuGraphResourceId appendResource(const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuGraphPipelineId appendPipeline(const GpuGraphPipelineDesc& desc);
    [[nodiscard]] GpuExternalCompletionId appendExternalCompletion(const GpuExternalCompletionDesc& desc);
    [[nodiscard]] const GpuUploadBlobNode* findUploadBlob(const GpuUploadBlobId& blob)const noexcept;
    [[nodiscard]] bool appendMarkerLabel(AStringView text, u32& outOffset, u32& outSize);
    [[nodiscard]] AStringView markerLabel(u32 offset, u32 size)const;
    void destroyTaskPayloads()noexcept;


private:
    GraphicsArena& m_arena;
    GraphicsVector<GpuTaskNode> m_tasks;
    GraphicsVector<GpuTaskId> m_dependencies;
    GraphicsVector<GpuExternalCompletionId> m_externalDependencies;
    GraphicsVector<GpuTaskResourceUse> m_resourceUses;
    GraphicsVector<GpuGraphResourceNode> m_resources;
    GraphicsVector<GpuGraphPipelineNode> m_pipelines;
    GraphicsVector<GpuExternalCompletionNode> m_externalCompletions;
    GraphicsVector<GpuUploadBlobNode> m_uploadBlobs;
    GraphicsBytes m_markerText;
    u64 m_generation = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

