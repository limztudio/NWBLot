// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_InvalidFrameGraphNodeIndex = ~0u;

struct FrameGraphNodeHandle{
    u32 index = s_InvalidFrameGraphNodeIndex;

    [[nodiscard]] bool valid()const{ return index != s_InvalidFrameGraphNodeIndex; }
};

struct FrameGraphPendingNameEdge{
    Name toName = NAME_NONE;
    u32 fromNodeIndex = 0u;
    FrameGraphEdgeKind::Enum kind = FrameGraphEdgeKind::Unknown;
    u8 flags = 0u;
};

using FrameGraphPendingNameEdges = Vector<FrameGraphPendingNameEdge, TelemetryArena>;

struct FrameGraphPassMetadata{
    FrameGraphQueueAssignment queueAssignment;
    FrameGraphCompiledTask compiledTask;
    FrameGraphRuntimeStatistics runtimeStatistics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameGraphBuilder final : NoCopy{
public:
    FrameGraphBuilder(
        FrameGraphNodeDescs& nodes,
        FrameGraphEdgeDescs& edges,
        FrameGraphPendingNameEdges& pendingNameEdges,
        const u64 frameIndex = 0u
    )
        : m_nodes(nodes)
        , m_edges(edges)
        , m_pendingNameEdges(pendingNameEdges)
        , m_frameIndex(frameIndex)
    {}
    FrameGraphBuilder(
        FrameGraphNodeDescs& nodes,
        FrameGraphEdgeDescs& edges,
        FrameGraphPendingNameEdges& pendingNameEdges,
        FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
        const u64 frameIndex = 0u
    )
        : m_nodes(nodes)
        , m_edges(edges)
        , m_pendingNameEdges(pendingNameEdges)
        , m_physicalQueueRuntimeStatistics(&physicalQueueRuntimeStatistics)
        , m_frameIndex(frameIndex)
    {}


public:
    [[nodiscard]] u64 frameIndex()const{ return m_frameIndex; }
    [[nodiscard]] bool addPhysicalQueueRuntimeStatistics(
        FrameGraphNodeHandle owner,
        const FrameGraphPhysicalQueueRuntimeStatistics& statistics
    );

    [[nodiscard]] FrameGraphNodeHandle addPass(const Name& scope, const AStringView label, const u8 flags = 0u){
        return addNode(scope, label, FrameGraphNodeKind::Pass, FrameGraphPassMetadata{}, flags);
    }
    [[nodiscard]] FrameGraphNodeHandle addPass(
        const Name& scope,
        const AStringView label,
        const FrameGraphQueueAssignment& queueAssignment,
        const u8 flags = 0u
    ){
        return addNode(
            scope,
            label,
            FrameGraphNodeKind::Pass,
            FrameGraphPassMetadata{ .queueAssignment = queueAssignment, .compiledTask = {}, .runtimeStatistics = {} },
            flags
        );
    }
    [[nodiscard]] FrameGraphNodeHandle addPass(
        const Name& scope,
        const AStringView label,
        const FrameGraphPassMetadata& metadata,
        const u8 flags = 0u
    ){
        return addNode(scope, label, FrameGraphNodeKind::Pass, metadata, flags);
    }
    [[nodiscard]] FrameGraphNodeHandle addResource(const Name& id, const AStringView label, const u8 flags = 0u){
        return addNode(id, label, FrameGraphNodeKind::Resource, FrameGraphPassMetadata{}, flags);
    }
    [[nodiscard]] FrameGraphNodeHandle addExternal(const Name& id, const AStringView label, const u8 flags = 0u){
        return addNode(id, label, FrameGraphNodeKind::External, FrameGraphPassMetadata{}, flags);
    }

    void addEdge(FrameGraphNodeHandle from, FrameGraphNodeHandle to, FrameGraphEdgeKind::Enum kind, u8 flags = 0u);
    void dependsOnByName(FrameGraphNodeHandle from, const Name& dependencyName, u8 flags = 0u);

private:
    [[nodiscard]] FrameGraphNodeHandle addNode(
        const Name& name,
        AStringView label,
        FrameGraphNodeKind::Enum kind,
        const FrameGraphPassMetadata& metadata,
        u8 flags
    );

private:
    FrameGraphNodeDescs& m_nodes;
    FrameGraphEdgeDescs& m_edges;
    FrameGraphPendingNameEdges& m_pendingNameEdges;
    FrameGraphPhysicalQueueRuntimeStatisticsRecords* m_physicalQueueRuntimeStatistics = nullptr;
    u64 m_frameIndex = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IFrameGraphContributor{
public:
    virtual ~IFrameGraphContributor() = default;

    virtual bool appendFrameGraph(FrameGraphBuilder& builder) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

