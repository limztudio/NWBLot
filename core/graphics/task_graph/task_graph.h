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

struct GpuTaskGraphExternalCompletionView{
    GpuExternalCompletionId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
};

struct GpuTaskGraphTelemetryOptions{
    const GpuTaskDependencyEdge* legacyScheduleMismatches = nullptr;
    usize legacyScheduleMismatchCount = 0u;
    const GpuTaskGraphQueueAssignments* queueAssignments = nullptr;
    const GpuTaskId* legacyQueueMismatches = nullptr;
    usize legacyQueueMismatchCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph final : NoCopy{
    friend class GpuTaskGraphCompiler;

private:
    using GpuTaskPayloadDestroyThunk = void(*)(GraphicsArena& arena, void* payload)noexcept;

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

    struct GpuExternalCompletionNode{
        Name identity = NAME_NONE;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
    };


public:
    explicit GpuTaskGraph(GraphicsArena& arena);
    ~GpuTaskGraph();


public:
    // Metadata-only tasks make the old renderer observable without pretending that it records through this graph.
    [[nodiscard]] GpuTaskId addTask(const GpuTaskDesc& desc);

    template<typename TaskT>
    [[nodiscard]] GpuTaskId addTask(const GpuTaskDesc& desc, typename TaskT::Payload&& payload){
        using Payload = typename TaskT::Payload;

        Payload* const storedPayload = NewArenaObject<Payload>(m_arena, Move(payload));
        if(!storedPayload)
            return {};

        const GpuTaskId task = appendTask(desc, storedPayload, &DestroyPayload<Payload>);
        if(!task.valid())
            DestroyArenaObject(m_arena, storedPayload);
        return task;
    }

    // This form is useful for abstract resources and conservative bindless hazard domains during the metadata-only
    // phase. Tasks that will be recorded later must use a typed import overload so the graph retains the resource.
    [[nodiscard]] GpuGraphResourceId importResource(const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuGraphResourceId importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuGraphResourceId importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuGraphResourceId importAccelStruct(
        const RayTracingAccelStructHandle& accelStruct,
        const GpuGraphResourceDesc& desc
    );
    [[nodiscard]] GpuGraphResourceId importHazardDomain(const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuExternalCompletionId importExternalCompletion(const GpuExternalCompletionDesc& desc);

    void reset();

    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }
    [[nodiscard]] bool validTask(const GpuTaskId& id)const noexcept;
    [[nodiscard]] bool validResource(const GpuGraphResourceId& id)const noexcept;
    [[nodiscard]] bool validExternalCompletion(const GpuExternalCompletionId& id)const noexcept;
    [[nodiscard]] usize taskCount()const noexcept{ return m_tasks.size(); }
    [[nodiscard]] usize resourceCount()const noexcept{ return m_resources.size(); }
    [[nodiscard]] usize externalCompletionCount()const noexcept{ return m_externalCompletions.size(); }
    [[nodiscard]] GpuTaskGraphTaskView taskAt(usize index)const;
    [[nodiscard]] GpuTaskGraphResourceView resourceAt(usize index)const;
    [[nodiscard]] GpuTaskGraphExternalCompletionView externalCompletionAt(usize index)const;
    [[nodiscard]] bool appendFrameGraphTelemetry(
        Telemetry::FrameGraphBuilder& builder,
        const GpuTaskGraphAnalysis& analysis,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraphTelemetryOptions& options = {}
    )const;


private:
    template<typename PayloadT>
    static void DestroyPayload(GraphicsArena& arena, void* payload)noexcept{
        DestroyArenaObject(arena, static_cast<PayloadT*>(payload));
    }

    [[nodiscard]] GpuTaskId appendTask(
        const GpuTaskDesc& desc,
        void* payload,
        GpuTaskPayloadDestroyThunk destroyPayload
    );
    [[nodiscard]] GpuGraphResourceId appendResource(const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuExternalCompletionId appendExternalCompletion(const GpuExternalCompletionDesc& desc);
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
    GraphicsVector<GpuExternalCompletionNode> m_externalCompletions;
    GraphicsBytes m_markerText;
    u64 m_generation = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

