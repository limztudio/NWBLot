// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_registry.h"

#include "session.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_graph_registry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FrameGraphNodeIndexLookup = HashMap<Name, u32, Alloc::ScratchArena, Hasher<Name>, EqualTo<Name>>;

static void ResolvePendingNameEdges(Alloc::ScratchArena& scratchArena, const FrameGraphNodeDescs& nodes, FrameGraphEdgeDescs& edges, const FrameGraphPendingNameEdges& pendingNameEdges){
    if(nodes.empty() || pendingNameEdges.empty())
        return;

    FrameGraphNodeIndexLookup nodeIndices(scratchArena);
    nodeIndices.reserve(nodes.size());
    for(u32 i = 0u; i < static_cast<u32>(nodes.size()); ++i){
        if(nodeIndices.contains(nodes[i].name))
            continue;
        if(!nodeIndices.emplace(nodes[i].name, i).second){
            NWB_ASSERT(false);
            return;
        }
    }

    edges.reserve(edges.size() + pendingNameEdges.size());
    for(const auto& pending : pendingNameEdges){
        const auto foundNode = nodeIndices.find(pending.toName);
        if(foundNode == nodeIndices.end())
            continue;

        edges.push_back(FrameGraphEdgeDesc{ .fromNodeIndex = pending.fromNodeIndex, .toNodeIndex = foundNode.value(), .kind = pending.kind, .flags = pending.flags, });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameGraphRegistry::FrameGraphRegistry(Alloc::GlobalArena& arena)
    : m_contributors(arena)
    , m_nodes(arena)
    , m_edges(arena)
    , m_pendingNameEdges(arena)
    , m_physicalQueueRuntimeStatistics(arena)
    , m_packetSubmissionStatistics(arena)
{}

void FrameGraphRegistry::registerContributor(IFrameGraphContributor& contributor){
    for(const auto existing : m_contributors){
        if(existing.get() == &contributor)
            return;
    }
    m_contributors.push_back(NotNull(&contributor));
}

void FrameGraphRegistry::unregisterContributor(IFrameGraphContributor& contributor)noexcept{
    for(auto iterator = m_contributors.begin(); iterator != m_contributors.end();){
        if(iterator->get() == &contributor)
            iterator = m_contributors.erase(iterator);
        else
            ++iterator;
    }
}

bool FrameGraphRegistry::record(CaptureSession& session){
    if(!session.captureOptions().frameGraphEnabled())
        return false;
    if(m_recording || m_contributors.empty())
        return false;

    Alloc::ScratchArena scratchArena(TelemetryArenaScope::s_FrameGraphScratch);
    m_recording = true;
    ScopeExit clearDescriptors([this]()noexcept{
        m_packetSubmissionStatistics.clear();
        m_physicalQueueRuntimeStatistics.clear();
        m_pendingNameEdges.clear();
        m_edges.clear();
        m_nodes.clear();
        m_recording = false;
    }
    );

    FrameGraphBuilder builder(scratchArena, m_nodes, m_edges, m_pendingNameEdges, m_physicalQueueRuntimeStatistics, m_packetSubmissionStatistics, session.frameIndex());
    bool hasGraph = false;
    for(const auto contributor : m_contributors){
        if(contributor->appendFrameGraph(builder))
            hasGraph = true;
    }

    if(!hasGraph)
        return false;

    __hidden_frame_graph_registry::ResolvePendingNameEdges(scratchArena, m_nodes, m_edges, m_pendingNameEdges);

    return session.recordFrameGraph(scratchArena, m_nodes, m_edges, m_physicalQueueRuntimeStatistics, m_packetSubmissionStatistics);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

