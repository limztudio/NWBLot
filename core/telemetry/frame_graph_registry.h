// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_graph_contributor.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CaptureSession;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameGraphRegistry final : NoCopy{
private:
    struct ContributorEntry{
        NotNull<IFrameGraphContributor*> contributor;
        b8 active = true;
    };


public:
    explicit FrameGraphRegistry(Alloc::GlobalArena& arena)
        : m_contributors(arena)
        , m_nodes(arena)
        , m_edges(arena)
        , m_pendingNameEdges(arena)
        , m_physicalQueueRuntimeStatistics(arena)
        , m_packetSubmissionStatistics(arena)
    {}


public:
    void registerContributor(IFrameGraphContributor& contributor);
    void unregisterContributor(IFrameGraphContributor& contributor)noexcept;
    void clear()noexcept;

    // Changes made by a callback affect the next record; removed contributors are skipped in the current record.
    [[nodiscard]] b8 record(CaptureSession& session);


private:
    List<ContributorEntry, Alloc::GlobalArena> m_contributors;
    FrameGraphNodeDescs m_nodes;
    FrameGraphEdgeDescs m_edges;
    FrameGraphPendingNameEdges m_pendingNameEdges;
    FrameGraphPhysicalQueueRuntimeStatisticsRecords m_physicalQueueRuntimeStatistics;
    FrameGraphPacketSubmissionStatisticsRecords m_packetSubmissionStatistics;
    b8 m_recording = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

