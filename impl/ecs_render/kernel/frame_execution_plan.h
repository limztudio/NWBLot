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
        AsyncRayEffects,
        GraphicsEffects,
        GraphicsAvboitPre,
        AsyncAvboitDepthWarp,
        GraphicsAvboitExtinction,
        AsyncAvboitIntegration,
        GraphicsAvboitAccumulation,
        DeferredLighting,
        DeferredComposite,
        GraphicsPresent,
        AsyncLaggedLightingStash,

        kCount,
    };
};


// Batches preserve packet order without duplicating topology in RendererSystem.
namespace FrameExecutionSubmissionBatch{
    enum Enum : u8{
        GraphicsPrefix,
        AsyncRayEffects,
        GraphicsEffects,
        DeferredLighting,
        DeferredComposite,
        GraphicsPresent,

        kCount,
    };
};


// Work-to-packet mapping keeps recording, timing, and submission declarative.
namespace FrameExecutionWork{
    enum Enum : u8{
        GraphicsPrefix,
        RayEffects,
        Caustics,
        SurfelGi,
        AvboitRaster,
        AsyncEffectsTiming,
        AvboitDepthWarp,
        AvboitExtinction,
        AvboitIntegration,
        AvboitAccumulation,
        DeferredLighting,
        DeferredComposite,
        GraphicsPresent,
        LaggedLightingStash,

        kCount,
    };
};


// External-token edges live with packet dependencies.
namespace FrameExecutionExternalWait{
    enum Enum : u8{
        LaggedLightingHistory,

        kCount,
    };
};


struct FrameExecutionPlanInput{
    bool dedicatedAsyncCompute = false;
    bool frameLaggedAsyncLightingEnabled = false;
    bool laggedLightingHistoryReady = false;
    bool laggedLightingHistoryAccepted = false;
    bool hasTransparentRenderers = false;
    // Hardware caustics run on Graphics; software caustics run on Compute.
    bool hardwareCaustics = false;
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


// RendererSystem supplies accepted history tokens as external dependencies.
struct FrameExecutionExternalWaitTokens{
    Core::QueueSubmissionToken tokens[FrameExecutionExternalWait::kCount] = {};


    [[nodiscard]] const Core::QueueSubmissionToken& token(
        const FrameExecutionExternalWait::Enum externalWait
    )const noexcept{
        NWB_ASSERT(externalWait < FrameExecutionExternalWait::kCount);
        return tokens[static_cast<usize>(externalWait)];
    }
};


// The plan routes work to RendererSystem-owned command lists.
struct FrameExecutionLaneCommandListPair{
    Core::CommandList* graphics = nullptr;
    Core::CommandList* asyncCompute = nullptr;
};


class FrameExecutionPlan final{
public:
    static constexpr usize s_MaxPacketWaits = 2u;
    static constexpr usize s_MaxSubmissionWaits = s_MaxPacketWaits + FrameExecutionExternalWait::kCount;


public:
    explicit FrameExecutionPlan(const FrameExecutionPlanInput& input){
        const bool usesDedicatedAsyncCompute = input.dedicatedAsyncCompute;
        // Keep one packet topology on every adapter.  Without a distinct compute-only family the plan routes this
        // work to Graphics, preserving packet order without a renderer-specific alternate path.
        const Core::RenderLane::Enum computeWorkLane = usesDedicatedAsyncCompute
            ? Core::RenderLane::AsyncCompute
            : Core::RenderLane::Graphics
        ;
        const bool capturesLaggedLightingHistory =
            input.dedicatedAsyncCompute
            && input.frameLaggedAsyncLightingEnabled
        ;
        const bool usesLaggedAsyncLighting =
            capturesLaggedLightingHistory
            && input.laggedLightingHistoryReady
            && input.laggedLightingHistoryAccepted
        ;
        const bool usesAsyncAvboit =
            input.dedicatedAsyncCompute
            && input.hasTransparentRenderers
            && !usesLaggedAsyncLighting
        ;
        const bool usesAsyncCaustics =
            input.dedicatedAsyncCompute
            && !input.hardwareCaustics
        ;

        enablePacket(FrameExecutionPacket::GraphicsPrefix, Core::RenderLane::Graphics);
        assignWork(FrameExecutionWork::GraphicsPrefix, FrameExecutionPacket::GraphicsPrefix);
        enablePacket(FrameExecutionPacket::AsyncRayEffects, computeWorkLane);
        addPacketWait(FrameExecutionPacket::AsyncRayEffects, FrameExecutionPacket::GraphicsPrefix);
        assignWork(FrameExecutionWork::RayEffects, FrameExecutionPacket::AsyncRayEffects);
        if(usesAsyncCaustics)
            assignWork(FrameExecutionWork::Caustics, FrameExecutionPacket::AsyncRayEffects);
        assignWork(FrameExecutionWork::SurfelGi, FrameExecutionPacket::AsyncRayEffects);

        FrameExecutionPacket::Enum graphicsEffectsCompletionPacket = FrameExecutionPacket::GraphicsEffects;
        if(usesAsyncAvboit){
            enablePacket(FrameExecutionPacket::GraphicsAvboitPre, Core::RenderLane::Graphics);
            addPacketWait(FrameExecutionPacket::GraphicsAvboitPre, FrameExecutionPacket::GraphicsPrefix);
            if(!usesAsyncCaustics){
                assignWork(FrameExecutionWork::Caustics, FrameExecutionPacket::GraphicsAvboitPre);
                // History stash must finish before Graphics hardware caustics rewrite live irradiance.
                if(usesLaggedAsyncLighting)
                    addExternalWait(
                        FrameExecutionPacket::GraphicsAvboitPre,
                        FrameExecutionExternalWait::LaggedLightingHistory
                    );
            }
            assignWork(FrameExecutionWork::AvboitRaster, FrameExecutionPacket::GraphicsAvboitPre);
            assignWork(FrameExecutionWork::AsyncEffectsTiming, FrameExecutionPacket::GraphicsAvboitPre);

            enablePacket(FrameExecutionPacket::AsyncAvboitDepthWarp, Core::RenderLane::AsyncCompute);
            addPacketWait(FrameExecutionPacket::AsyncAvboitDepthWarp, FrameExecutionPacket::GraphicsAvboitPre);
            assignWork(FrameExecutionWork::AvboitDepthWarp, FrameExecutionPacket::AsyncAvboitDepthWarp);

            enablePacket(FrameExecutionPacket::GraphicsAvboitExtinction, Core::RenderLane::Graphics);
            addPacketWait(FrameExecutionPacket::GraphicsAvboitExtinction, FrameExecutionPacket::AsyncAvboitDepthWarp);
            assignWork(FrameExecutionWork::AvboitExtinction, FrameExecutionPacket::GraphicsAvboitExtinction);

            enablePacket(FrameExecutionPacket::AsyncAvboitIntegration, Core::RenderLane::AsyncCompute);
            addPacketWait(FrameExecutionPacket::AsyncAvboitIntegration, FrameExecutionPacket::GraphicsAvboitExtinction);
            assignWork(FrameExecutionWork::AvboitIntegration, FrameExecutionPacket::AsyncAvboitIntegration);

            enablePacket(FrameExecutionPacket::GraphicsAvboitAccumulation, Core::RenderLane::Graphics);
            addPacketWait(FrameExecutionPacket::GraphicsAvboitAccumulation, FrameExecutionPacket::AsyncAvboitIntegration);
            assignWork(FrameExecutionWork::AvboitAccumulation, FrameExecutionPacket::GraphicsAvboitAccumulation);
            graphicsEffectsCompletionPacket = FrameExecutionPacket::GraphicsAvboitAccumulation;
        }
        else{
            enablePacket(FrameExecutionPacket::GraphicsEffects, Core::RenderLane::Graphics);
            addPacketWait(FrameExecutionPacket::GraphicsEffects, FrameExecutionPacket::GraphicsPrefix);
            if(!usesAsyncCaustics){
                assignWork(FrameExecutionWork::Caustics, FrameExecutionPacket::GraphicsEffects);
                if(usesLaggedAsyncLighting)
                    addExternalWait(
                        FrameExecutionPacket::GraphicsEffects,
                        FrameExecutionExternalWait::LaggedLightingHistory
                    );
            }
            assignWork(FrameExecutionWork::AvboitRaster, FrameExecutionPacket::GraphicsEffects);
            // This envelope measures inter-queue async effects, so it is intentionally absent when the same work
            // is serialized on Graphics.
            if(usesDedicatedAsyncCompute)
                assignWork(FrameExecutionWork::AsyncEffectsTiming, FrameExecutionPacket::GraphicsEffects);
        }

        const Core::RenderLane::Enum deferredLane = usesLaggedAsyncLighting
            ? Core::RenderLane::Graphics
            : computeWorkLane
        ;
        enablePacket(FrameExecutionPacket::DeferredLighting, deferredLane);
        addPacketWait(FrameExecutionPacket::DeferredLighting, graphicsEffectsCompletionPacket);
        assignWork(FrameExecutionWork::DeferredLighting, FrameExecutionPacket::DeferredLighting);
        if(usesLaggedAsyncLighting)
            addExternalWait(
                FrameExecutionPacket::DeferredLighting,
                FrameExecutionExternalWait::LaggedLightingHistory
            );
        else
            addPacketWait(FrameExecutionPacket::DeferredLighting, FrameExecutionPacket::AsyncRayEffects);

        enablePacket(FrameExecutionPacket::DeferredComposite, deferredLane);
        addPacketWait(FrameExecutionPacket::DeferredComposite, FrameExecutionPacket::DeferredLighting);
        assignWork(FrameExecutionWork::DeferredComposite, FrameExecutionPacket::DeferredComposite);

        enablePacket(FrameExecutionPacket::GraphicsPresent, Core::RenderLane::Graphics);
        addPacketWait(FrameExecutionPacket::GraphicsPresent, FrameExecutionPacket::DeferredComposite);
        assignWork(FrameExecutionWork::GraphicsPresent, FrameExecutionPacket::GraphicsPresent);
        if(usesLaggedAsyncLighting)
            addPacketWait(FrameExecutionPacket::GraphicsPresent, FrameExecutionPacket::AsyncRayEffects);

        if(capturesLaggedLightingHistory){
            enablePacket(
                FrameExecutionPacket::AsyncLaggedLightingStash,
                Core::RenderLane::AsyncCompute,
                false
            );
            addPacketWait(FrameExecutionPacket::AsyncLaggedLightingStash, FrameExecutionPacket::GraphicsPresent);
            assignWork(FrameExecutionWork::LaggedLightingStash, FrameExecutionPacket::AsyncLaggedLightingStash);
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
    [[nodiscard]] Core::CommandList* commandListForWork(
        const FrameExecutionWork::Enum work,
        const FrameExecutionLaneCommandListPair& commandLists
    )const noexcept{
        if(work >= FrameExecutionWork::kCount || !hasWork(work))
            return nullptr;

        switch(laneForWork(work)){
            case Core::RenderLane::Graphics:
                return commandLists.graphics;
            case Core::RenderLane::AsyncCompute:
                return commandLists.asyncCompute;
            default:
                NWB_ASSERT(false);
                return nullptr;
        }
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
            FrameExecutionSubmissionBatch::AsyncRayEffects,
            FrameExecutionPacket::AsyncRayEffects
        );
        if(hasWork(FrameExecutionWork::AvboitDepthWarp)){
            appendSubmissionPacket(
                FrameExecutionSubmissionBatch::GraphicsEffects,
                FrameExecutionPacket::GraphicsAvboitPre
            );
            appendSubmissionPacket(
                FrameExecutionSubmissionBatch::GraphicsEffects,
                FrameExecutionPacket::AsyncAvboitDepthWarp
            );
            appendSubmissionPacket(
                FrameExecutionSubmissionBatch::GraphicsEffects,
                FrameExecutionPacket::GraphicsAvboitExtinction
            );
            appendSubmissionPacket(
                FrameExecutionSubmissionBatch::GraphicsEffects,
                FrameExecutionPacket::AsyncAvboitIntegration
            );
            appendSubmissionPacket(
                FrameExecutionSubmissionBatch::GraphicsEffects,
                FrameExecutionPacket::GraphicsAvboitAccumulation
            );
        }
        else{
            appendSubmissionPacket(
                FrameExecutionSubmissionBatch::GraphicsEffects,
                FrameExecutionPacket::GraphicsEffects
            );
        }
        appendSubmissionPacket(
            FrameExecutionSubmissionBatch::DeferredLighting,
            FrameExecutionPacket::DeferredLighting
        );
        appendSubmissionPacket(
            FrameExecutionSubmissionBatch::DeferredComposite,
            FrameExecutionPacket::DeferredComposite
        );
        appendSubmissionPacket(
            FrameExecutionSubmissionBatch::GraphicsPresent,
            FrameExecutionPacket::GraphicsPresent
        );

        // History copy records only after presentation accepts.
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

