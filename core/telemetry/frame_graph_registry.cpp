// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_registry.h"

#include "session.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_registry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ResolvePendingNameEdges(
    const FrameGraphNodeDescs& nodes,
    FrameGraphEdgeDescs& edges,
    const FrameGraphPendingNameEdges& pendingNameEdges
){
    for(const auto& pending : pendingNameEdges){
        for(u32 i = 0u; i < static_cast<u32>(nodes.size()); ++i){
            if(nodes[i].name != pending.toName)
                continue;

            edges.push_back(FrameGraphEdgeDesc{
                .fromNodeIndex = pending.fromNodeIndex,
                .toNodeIndex = i,
                .kind = pending.kind,
                .flags = pending.flags,
            });
            break;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FrameGraphRegistry::registerContributor(IFrameGraphContributor& contributor){
    for(auto* existing : m_contributors){
        if(existing == &contributor)
            return;
    }
    m_contributors.push_back(&contributor);
}

void FrameGraphRegistry::unregisterContributor(IFrameGraphContributor& contributor){
    m_contributors.remove(&contributor);
}

bool FrameGraphRegistry::record(CaptureSession& session){
    if(!session.captureOptions().frameGraphEnabled())
        return false;
    if(m_contributors.empty())
        return false;

    TelemetryArena& arena = session.recorder().arena();
    FrameGraphNodeDescs nodes(arena);
    FrameGraphEdgeDescs edges(arena);
    FrameGraphPendingNameEdges pendingNameEdges(arena);

    bool hasGraph = false;
    for(auto* contributor : m_contributors){
        FrameGraphBuilder builder(nodes, edges, pendingNameEdges);
        if(contributor->appendFrameGraph(builder))
            hasGraph = true;
    }

    if(!hasGraph)
        return false;

    __hidden_frame_graph_registry::ResolvePendingNameEdges(nodes, edges, pendingNameEdges);

    return session.recordFrameGraph(nodes, edges);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

