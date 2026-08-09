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
        GraphicsPresent,

        kCount,
    };
};


// Batches preserve packet order without duplicating topology in RendererSystem.
namespace FrameExecutionSubmissionBatch{
    enum Enum : u8{
        GraphicsPrefix,
        GraphicsPresent,

        kCount,
    };
};


// Work-to-packet mapping keeps recording, timing, and submission declarative.
namespace FrameExecutionWork{
    enum Enum : u8{
        GraphicsPrefix,
        GraphicsPresent,

        kCount,
    };
};


// External-token edges live with packet dependencies.
namespace FrameExecutionExternalWait{
    enum Enum : u8{
        // Graph-owned deferred composite publishes this accepted token before legacy presentation submits.
        DeferredComposite,
        // Graph-owned surfel GI publishes this accepted token before its remaining legacy consumers submit.
        SurfelGi,

        kCount,
    };
};


struct FrameExecutionPlanInput{
    bool dedicatedAsyncCompute = false;
    bool frameLaggedAsyncLightingEnabled = false;
    bool laggedLightingHistoryReady = false;
    bool laggedLightingHistoryAccepted = false;
};


struct FrameExecutionPacketPlan{
    Core::RenderLane::Enum lane = Core::RenderLane::Graphics;
    FrameExecutionPacket::Enum waitPackets[2] = {};
    u8 waitPacketCount = 0u;
    FrameExecutionExternalWait::Enum externalWaits[FrameExecutionExternalWait::kCount] = {};
    u8 externalWaitCount = 0u;
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


// RendererSystem supplies accepted graph-produced tokens as external dependencies.
struct FrameExecutionExternalWaitTokens{
    Core::QueueSubmissionToken tokens[FrameExecutionExternalWait::kCount] = {};


    [[nodiscard]] const Core::QueueSubmissionToken& token(
        const FrameExecutionExternalWait::Enum externalWait
    )const noexcept{
        NWB_ASSERT(externalWait < FrameExecutionExternalWait::kCount);
        return tokens[static_cast<usize>(externalWait)];
    }
};


class FrameExecutionPlan final{
public:
    static constexpr usize s_MaxPacketWaits = 2u;
    static constexpr usize s_MaxSubmissionWaits = s_MaxPacketWaits + FrameExecutionExternalWait::kCount;


public:
    explicit FrameExecutionPlan(const FrameExecutionPlanInput& input){
        const bool capturesLaggedLightingHistory =
            input.dedicatedAsyncCompute
            && input.frameLaggedAsyncLightingEnabled
        ;
        const bool usesLaggedAsyncLighting =
            capturesLaggedLightingHistory
            && input.laggedLightingHistoryReady
            && input.laggedLightingHistoryAccepted
        ;
        enablePacket(FrameExecutionPacket::GraphicsPrefix, Core::RenderLane::Graphics);
        assignWork(FrameExecutionWork::GraphicsPrefix, FrameExecutionPacket::GraphicsPrefix);

        enablePacket(FrameExecutionPacket::GraphicsPresent, Core::RenderLane::Graphics);
        addExternalWait(FrameExecutionPacket::GraphicsPresent, FrameExecutionExternalWait::DeferredComposite);
        assignWork(FrameExecutionWork::GraphicsPresent, FrameExecutionPacket::GraphicsPresent);
        if(usesLaggedAsyncLighting){
            addExternalWait(FrameExecutionPacket::GraphicsPresent, FrameExecutionExternalWait::SurfelGi);
        }

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
    [[nodiscard]] bool workRunsOnLane(
        const FrameExecutionWork::Enum work,
        const Core::RenderLane::Enum lane
    )const noexcept{
        return work < FrameExecutionWork::kCount && hasWork(work) && laneForWork(work) == lane;
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
    [[nodiscard]] bool packetWaitsForExternalToken(
        const FrameExecutionPacket::Enum packetID,
        const FrameExecutionExternalWait::Enum externalWait
    )const noexcept{
        const FrameExecutionPacketPlan& packetPlan = packet(packetID);
        for(u8 waitIndex = 0u; waitIndex < packetPlan.externalWaitCount; ++waitIndex){
            if(packetPlan.externalWaits[waitIndex] == externalWait)
                return true;
        }
        return false;
    }
    [[nodiscard]] bool workWaitsForExternalToken(
        const FrameExecutionWork::Enum work,
        const FrameExecutionExternalWait::Enum externalWait
    )const noexcept{
        return work < FrameExecutionWork::kCount
            && hasWork(work)
            && packetWaitsForExternalToken(packetForWork(work), externalWait)
        ;
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
    void addPacketWait(
        const FrameExecutionPacket::Enum consumerPacket,
        const FrameExecutionPacket::Enum producerPacket
    )noexcept{
        FrameExecutionPacketPlan& consumerPlan = mutablePacket(consumerPacket);
        NWB_ASSERT(consumerPlan.enabled);
        NWB_ASSERT(packet(producerPacket).enabled);
        NWB_ASSERT(consumerPlan.waitPacketCount < s_MaxPacketWaits);
        consumerPlan.waitPackets[consumerPlan.waitPacketCount++] = producerPacket;
    }
    void addExternalWait(
        const FrameExecutionPacket::Enum consumerPacket,
        const FrameExecutionExternalWait::Enum externalWait
    )noexcept{
        FrameExecutionPacketPlan& consumerPlan = mutablePacket(consumerPacket);
        NWB_ASSERT(consumerPlan.enabled);
        NWB_ASSERT(externalWait < FrameExecutionExternalWait::kCount);
        NWB_ASSERT(consumerPlan.externalWaitCount < FrameExecutionExternalWait::kCount);
        consumerPlan.externalWaits[consumerPlan.externalWaitCount++] = externalWait;
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
        const FrameExecutionPacketPlan& packetPlan = packet(packetID);
        for(u8 waitPacketIndex = 0u; waitPacketIndex < packetPlan.waitPacketCount; ++waitPacketIndex)
            NWB_ASSERT(m_submissionPacketScheduled[static_cast<usize>(packetPlan.waitPackets[waitPacketIndex])]);
        NWB_ASSERT(batchPlan.packetCount < FrameExecutionPacket::kCount);
        batchPlan.packets[batchPlan.packetCount++] = packetID;
        m_submissionPacketScheduled[packetIndex] = true;
    }
    void configureSubmissionBatches()noexcept{
        appendSubmissionPacket(
            FrameExecutionSubmissionBatch::GraphicsPrefix,
            FrameExecutionPacket::GraphicsPrefix
        );
        appendSubmissionPacket(
            FrameExecutionSubmissionBatch::GraphicsPresent,
            FrameExecutionPacket::GraphicsPresent
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


// Per-frame state resolves accepted tokens and retains recoverable async edges.
class FrameExecutionPlanSubmissionState final{
public:
    explicit FrameExecutionPlanSubmissionState(
        const FrameExecutionPlan& plan,
        const FrameExecutionExternalWaitTokens& externalWaitTokens = {}
    )noexcept
        : m_plan(plan)
        , m_externalWaitTokens(externalWaitTokens)
    {}


public:
    [[nodiscard]] bool prepareSubmission(
        const FrameExecutionPacket::Enum packet,
        Core::QueueSubmissionDesc& submitDesc,
        Core::QueueSubmissionToken* const waitTokens,
        const usize waitTokenCapacity
    )const noexcept{
        submitDesc = Core::QueueSubmissionDesc{};
        const FrameExecutionPacketPlan& packetPlan = m_plan.packet(packet);
        NWB_ASSERT(packetPlan.enabled);
        const usize requiredWaitTokenCount =
            static_cast<usize>(packetPlan.waitPacketCount)
            + static_cast<usize>(packetPlan.externalWaitCount)
        ;
        if(
            !packetPlan.enabled
            || (requiredWaitTokenCount > 0u && !waitTokens)
            || requiredWaitTokenCount > waitTokenCapacity
        )
            return false;

        usize waitTokenCount = 0u;
        for(u8 waitPacketIndex = 0u; waitPacketIndex < packetPlan.waitPacketCount; ++waitPacketIndex){
            const FrameExecutionPacket::Enum waitPacket = packetPlan.waitPackets[waitPacketIndex];
            const Core::QueueSubmissionToken waitToken = token(waitPacket);
            if(!waitToken.valid())
                return false;
            waitTokens[waitTokenCount++] = waitToken;
        }
        for(u8 externalWaitIndex = 0u;
            externalWaitIndex < packetPlan.externalWaitCount;
            ++externalWaitIndex
        ){
            const Core::QueueSubmissionToken& externalWaitToken = m_externalWaitTokens.token(
                packetPlan.externalWaits[externalWaitIndex]
            );
            if(!externalWaitToken.valid())
                return false;
            waitTokens[waitTokenCount++] = externalWaitToken;
        }
        if(waitTokenCount > 0u)
            submitDesc.setWaitTokens(waitTokens, waitTokenCount);
        return true;
    }

    void acceptSubmission(
        const FrameExecutionPacket::Enum packet,
        const Core::QueueSubmissionToken submissionToken
    )noexcept{
        NWB_ASSERT(m_plan.packet(packet).enabled);
        if(!submissionToken.valid())
            return;

        const usize packetIndex = static_cast<usize>(packet);
        NWB_ASSERT(!m_packetTokens[packetIndex].valid());
        m_packetTokens[packetIndex] = submissionToken;
        const Core::RenderLane::Enum lane = m_plan.packet(packet).lane;
        if(lane == Core::RenderLane::AsyncCompute)
            m_asyncRecoveryWaitToken = submissionToken;
    }
    // A graph-migrated producer can publish an accepted completion between legacy plan batches.  The remaining
    // packet consumer still uses the plan's established wait builder until it too migrates.
    void setExternalWaitToken(
        const FrameExecutionExternalWait::Enum externalWait,
        const Core::QueueSubmissionToken submissionToken
    )noexcept{
        NWB_ASSERT(externalWait < FrameExecutionExternalWait::kCount);
        m_externalWaitTokens.tokens[static_cast<usize>(externalWait)] = submissionToken;
    }

    [[nodiscard]] Core::QueueSubmissionToken token(const FrameExecutionPacket::Enum packet)const noexcept{
        return m_packetTokens[static_cast<usize>(packet)];
    }
    // Accepted-token boundary distinguishes first rejection from later rollback.
    [[nodiscard]] bool batchHasAcceptedPacket(
        const FrameExecutionSubmissionBatch::Enum batch
    )const noexcept{
        const FrameExecutionSubmissionBatchPlan& batchPlan = m_plan.submissionBatch(batch);
        for(u8 packetIndex = 0u; packetIndex < batchPlan.packetCount; ++packetIndex){
            if(token(batchPlan.packets[packetIndex]).valid())
                return true;
        }
        return false;
    }
    // Latest accepted compute token covers every earlier compute packet.
    [[nodiscard]] const Core::QueueSubmissionToken* asyncRecoveryWaitToken()const noexcept{
        return m_asyncRecoveryWaitToken.valid() ? &m_asyncRecoveryWaitToken : nullptr;
    }


private:
    const FrameExecutionPlan& m_plan;
    FrameExecutionExternalWaitTokens m_externalWaitTokens;
    Core::QueueSubmissionToken m_packetTokens[FrameExecutionPacket::kCount] = {};
    Core::QueueSubmissionToken m_asyncRecoveryWaitToken;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

