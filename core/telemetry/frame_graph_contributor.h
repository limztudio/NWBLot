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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameGraphBuilder final : NoCopy{
public:
    FrameGraphBuilder(
        FrameGraphNodeDescs& nodes,
        FrameGraphEdgeDescs& edges,
        FrameGraphPendingNameEdges& pendingNameEdges
    )
        : m_nodes(nodes)
        , m_edges(edges)
        , m_pendingNameEdges(pendingNameEdges)
    {}


public:
    [[nodiscard]] FrameGraphNodeHandle addPass(const Name& scope, const AStringView label, const u8 flags = 0u){
        return addNode(scope, label, FrameGraphNodeKind::Pass, flags);
    }
    [[nodiscard]] FrameGraphNodeHandle addResource(const Name& id, const AStringView label, const u8 flags = 0u){
        return addNode(id, label, FrameGraphNodeKind::Resource, flags);
    }
    [[nodiscard]] FrameGraphNodeHandle addExternal(const Name& id, const AStringView label, const u8 flags = 0u){
        return addNode(id, label, FrameGraphNodeKind::External, flags);
    }

    void addEdge(FrameGraphNodeHandle from, FrameGraphNodeHandle to, FrameGraphEdgeKind::Enum kind, u8 flags = 0u);
    void dependsOnByName(FrameGraphNodeHandle from, const Name& dependencyName, u8 flags = 0u);

private:
    [[nodiscard]] FrameGraphNodeHandle addNode(const Name& name, AStringView label, FrameGraphNodeKind::Enum kind, u8 flags);

private:
    FrameGraphNodeDescs& m_nodes;
    FrameGraphEdgeDescs& m_edges;
    FrameGraphPendingNameEdges& m_pendingNameEdges;
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

