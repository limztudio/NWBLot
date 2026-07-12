
#include "frame_graph_contributor.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FrameGraphBuilder::addEdge(const FrameGraphNodeHandle from, const FrameGraphNodeHandle to, const FrameGraphEdgeKind::Enum kind, const u8 flags){
    if(!from.valid() || !to.valid())
        return;

    m_edges.push_back(FrameGraphEdgeDesc{
        .fromNodeIndex = from.index,
        .toNodeIndex = to.index,
        .kind = kind,
        .flags = flags,
    });
}

void FrameGraphBuilder::dependsOnByName(const FrameGraphNodeHandle from, const Name& dependencyName, const u8 flags){
    if(!from.valid())
        return;

    m_pendingNameEdges.push_back(FrameGraphPendingNameEdge{
        .toName = dependencyName,
        .fromNodeIndex = from.index,
        .kind = FrameGraphEdgeKind::DependsOn,
        .flags = flags,
    });
}

FrameGraphNodeHandle FrameGraphBuilder::addNode(const Name& name, const AStringView label, const FrameGraphNodeKind::Enum kind, const u8 flags){
    const u32 index = static_cast<u32>(m_nodes.size());
    m_nodes.push_back(FrameGraphNodeDesc{
        .name = name,
        .label = label,
        .kind = kind,
        .flags = flags,
    });
    return FrameGraphNodeHandle{ .index = index };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

