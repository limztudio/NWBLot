// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The plan owns lane routing and dependencies; RendererSystem owns state and recovery.
namespace FrameExecutionPacket{
    enum Enum : u8{
        GraphicsPrefix,

        kCount,
    };
};


// Batches preserve packet order without duplicating topology in RendererSystem.
namespace FrameExecutionSubmissionBatch{
    enum Enum : u8{
        GraphicsPrefix,

        kCount,
    };
};


// Work-to-packet mapping keeps recording, timing, and submission declarative.
namespace FrameExecutionWork{
    enum Enum : u8{
        GraphicsPrefix,

        kCount,
    };
};

struct FrameExecutionPacketPlan{
    Core::RenderLane::Enum lane = Core::RenderLane::Graphics;
    bool enabled = false;
    // Enabled timed packets receive a ticket keyed by packet ID.
    bool recordsTiming = false;
};


// Bound batch storage by the full packet set.
struct FrameExecutionSubmissionBatchPlan{
    FrameExecutionPacket::Enum packets[FrameExecutionPacket::kCount] = {};
    u8 packetCount = 0u;
};


struct FrameExecutionWorkPlan{
    FrameExecutionPacket::Enum packet = FrameExecutionPacket::kCount;
};


class FrameExecutionPlan final{
public:
    FrameExecutionPlan(){
        enablePacket(FrameExecutionPacket::GraphicsPrefix, Core::RenderLane::Graphics);
        assignWork(FrameExecutionWork::GraphicsPrefix, FrameExecutionPacket::GraphicsPrefix);

        configureSubmissionBatches();
    }


public:
    [[nodiscard]] const FrameExecutionPacketPlan& packet(const FrameExecutionPacket::Enum packet)const noexcept{
        return m_packets[static_cast<usize>(packet)];
    }
    [[nodiscard]] const FrameExecutionWorkPlan& work(const FrameExecutionWork::Enum work)const noexcept{
        return m_workPlans[static_cast<usize>(work)];
    }
    [[nodiscard]] bool hasWork(const FrameExecutionWork::Enum work)const noexcept{
        return this->work(work).packet != FrameExecutionPacket::kCount;
    }
    [[nodiscard]] FrameExecutionPacket::Enum packetForWork(const FrameExecutionWork::Enum work)const noexcept{
        const FrameExecutionPacket::Enum packetID = this->work(work).packet;
        NWB_ASSERT(packetID != FrameExecutionPacket::kCount);
        return packetID;
    }
    [[nodiscard]] Core::RenderLane::Enum laneForWork(const FrameExecutionWork::Enum work)const noexcept{
        NWB_ASSERT(work < FrameExecutionWork::kCount);
        NWB_ASSERT(hasWork(work));
        return packet(packetForWork(work)).lane;
    }
    // The legacy plan is the migration parity oracle.  Its scheduler lane is intent, while this is the concrete
    // transport expected by the established two-queue renderer topology.  A graph route may drive native recording
    // only after it agrees with this mapping for every enabled work item.
    [[nodiscard]] Core::CommandQueue::Enum expectedQueueForWork(
        const FrameExecutionWork::Enum work
    )const noexcept{
        if(work >= FrameExecutionWork::kCount || !hasWork(work))
            return Core::CommandQueue::kCount;

        switch(laneForWork(work)){
            case Core::RenderLane::Graphics:
                return Core::CommandQueue::Graphics;
            case Core::RenderLane::AsyncCompute:
                return Core::CommandQueue::Compute;
            default:
                NWB_ASSERT(false);
                return Core::CommandQueue::kCount;
        }
    }
    [[nodiscard]] bool workMatchesExpectedQueue(
        const FrameExecutionWork::Enum work,
        const Core::CommandQueue::Enum queue
    )const noexcept{
        return expectedQueueForWork(work) == queue;
    }
    [[nodiscard]] usize submissionBatchCount()const noexcept{ return m_submissionBatchCount; }
    [[nodiscard]] FrameExecutionSubmissionBatch::Enum submissionBatchID(const usize batchIndex)const noexcept{
        NWB_ASSERT(batchIndex < m_submissionBatchCount);
        return m_submissionBatchOrder[batchIndex];
    }
    [[nodiscard]] const FrameExecutionSubmissionBatchPlan& submissionBatch(
        const FrameExecutionSubmissionBatch::Enum batch
    )const noexcept{
        return m_submissionBatches[static_cast<usize>(batch)];
    }


private:
    [[nodiscard]] FrameExecutionPacketPlan& mutablePacket(const FrameExecutionPacket::Enum packet)noexcept{
        return m_packets[static_cast<usize>(packet)];
    }
    [[nodiscard]] FrameExecutionWorkPlan& mutableWork(const FrameExecutionWork::Enum work)noexcept{
        return m_workPlans[static_cast<usize>(work)];
    }
    void enablePacket(
        const FrameExecutionPacket::Enum packetID,
        const Core::RenderLane::Enum lane,
        const bool recordsTiming = true
    )noexcept{
        FrameExecutionPacketPlan& packetPlan = mutablePacket(packetID);
        packetPlan.lane = lane;
        packetPlan.enabled = true;
        packetPlan.recordsTiming = recordsTiming;
    }
    void assignWork(
        const FrameExecutionWork::Enum work,
        const FrameExecutionPacket::Enum packetID
    )noexcept{
        NWB_ASSERT(packet(packetID).enabled);
        FrameExecutionWorkPlan& workPlan = mutableWork(work);
        NWB_ASSERT(workPlan.packet == FrameExecutionPacket::kCount);
        workPlan.packet = packetID;
    }
    void appendSubmissionPacket(
        const FrameExecutionSubmissionBatch::Enum batch,
        const FrameExecutionPacket::Enum packetID
    )noexcept{
        NWB_ASSERT(packet(packetID).enabled);
        const usize batchIndex = static_cast<usize>(batch);
        FrameExecutionSubmissionBatchPlan& batchPlan = m_submissionBatches[batchIndex];
        if(batchPlan.packetCount == 0u){
            NWB_ASSERT(m_submissionBatchCount < FrameExecutionSubmissionBatch::kCount);
            m_submissionBatchOrder[m_submissionBatchCount++] = batch;
        }
        else
            NWB_ASSERT(m_submissionBatchOrder[m_submissionBatchCount - 1u] == batch);

        const usize packetIndex = static_cast<usize>(packetID);
        NWB_ASSERT(!m_submissionPacketScheduled[packetIndex]);
        NWB_ASSERT(batchPlan.packetCount < FrameExecutionPacket::kCount);
        batchPlan.packets[batchPlan.packetCount++] = packetID;
        m_submissionPacketScheduled[packetIndex] = true;
    }
    void configureSubmissionBatches()noexcept{
        appendSubmissionPacket(
            FrameExecutionSubmissionBatch::GraphicsPrefix,
            FrameExecutionPacket::GraphicsPrefix
        );
    }
private:
    FrameExecutionPacketPlan m_packets[FrameExecutionPacket::kCount] = {};
    FrameExecutionWorkPlan m_workPlans[FrameExecutionWork::kCount] = {};
    FrameExecutionSubmissionBatchPlan m_submissionBatches[FrameExecutionSubmissionBatch::kCount] = {};
    FrameExecutionSubmissionBatch::Enum m_submissionBatchOrder[FrameExecutionSubmissionBatch::kCount] = {};
    bool m_submissionPacketScheduled[FrameExecutionPacket::kCount] = {};
    usize m_submissionBatchCount = 0u;
};


// RendererSystem records lists; the plan maps them to packet order.
struct FrameExecutionPacketCommandListRange{
    Core::CommandList* const* commandLists = nullptr;
    usize commandListCount = 0u;
};


struct FrameExecutionWorkCommandListBinding{
    FrameExecutionWork::Enum work = FrameExecutionWork::kCount;
    Core::CommandList* commandList = nullptr;
};


class FrameExecutionPacketCommandLists final{
public:
    static constexpr usize s_MaxCommandListsPerPacket = 12u;


public:
    explicit FrameExecutionPacketCommandLists(const FrameExecutionPlan& plan)noexcept
        : m_plan(plan)
    {}


public:
    [[nodiscard]] bool appendPlannedWorkCommandLists(
        const FrameExecutionWorkCommandListBinding* const bindings,
        const usize bindingCount
    )noexcept{
        if(bindingCount > 0u && !bindings)
            return false;

        for(usize bindingIndex = 0u; bindingIndex < bindingCount; ++bindingIndex){
            const FrameExecutionWorkCommandListBinding& binding = bindings[bindingIndex];
            if(binding.work >= FrameExecutionWork::kCount)
                return false;
            if(!m_plan.hasWork(binding.work))
                continue;
            if(!appendForPacket(m_plan.packetForWork(binding.work), binding.commandList))
                return false;
        }
        return true;
    }
    [[nodiscard]] bool appendForWork(
        const FrameExecutionWork::Enum work,
        Core::CommandList* const commandList
    )noexcept{
        if(!m_plan.hasWork(work))
            return false;
        return appendForPacket(m_plan.packetForWork(work), commandList);
    }
    [[nodiscard]] FrameExecutionPacketCommandListRange commandLists(
        const FrameExecutionPacket::Enum packet
    )const noexcept{
        const usize packetIndex = static_cast<usize>(packet);
        return FrameExecutionPacketCommandListRange{
            m_commandLists[packetIndex],
            m_commandListCounts[packetIndex],
        };
    }


private:
    [[nodiscard]] bool appendForPacket(
        const FrameExecutionPacket::Enum packet,
        Core::CommandList* const commandList
    )noexcept{
        if(!commandList || !m_plan.packet(packet).enabled)
            return false;

        const usize packetIndex = static_cast<usize>(packet);
        usize& commandListCount = m_commandListCounts[packetIndex];
        if(commandListCount >= s_MaxCommandListsPerPacket)
            return false;
        m_commandLists[packetIndex][commandListCount++] = commandList;
        return true;
    }
    const FrameExecutionPlan& m_plan;
    Core::CommandList* m_commandLists[FrameExecutionPacket::kCount][s_MaxCommandListsPerPacket] = {};
    usize m_commandListCounts[FrameExecutionPacket::kCount] = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

