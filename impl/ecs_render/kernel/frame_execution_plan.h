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


// The renderer has two intentionally constrained submission topologies. This plan owns their lane selection and
// dependency graph, while RendererSystem continues to own resource-state handoffs, temporal commits, and recovery.
namespace FrameExecutionPacket{
    enum Enum : u8{
        GraphicsFallback,
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


// Submission batches preserve the renderer's accepted packet order without making RendererSystem restate every
// topology-specific packet sequence. RendererSystem retains the per-batch temporal and state-handoff decisions.
namespace FrameExecutionSubmissionBatch{
    enum Enum : u8{
        GraphicsFallback,
        GraphicsPrefix,
        AsyncRayEffects,
        GraphicsEffects,
        DeferredLighting,
        DeferredComposite,
        GraphicsPresent,

        kCount,
    };
};


// A recorded workload can share a submission packet with other work. Keep that mapping declarative with the
// packet graph so recording, timing, and submission cannot drift into separate topology decisions.
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


struct FrameExecutionPlanInput{
    bool dedicatedAsyncCompute = false;
    bool frameLaggedAsyncLightingEnabled = false;
    bool laggedLightingHistoryReady = false;
    bool laggedLightingHistoryAccepted = false;
    bool hasTransparentRenderers = false;
};


struct FrameExecutionPacketPlan{
    Core::RenderLane::Enum lane = Core::RenderLane::Graphics;
    FrameExecutionPacket::Enum waitPackets[2] = {};
    u8 waitPacketCount = 0u;
    bool enabled = false;
    // An enabled packet receives a timing ticket indexed by its packet ID unless it is explicitly execution-only.
    // This avoids a second packet-to-ticket registry that must be updated whenever the topology grows.
    bool recordsTiming = false;
    bool waitsForLaggedLightingHistory = false;
};


// The current widest batch is the five-packet async AVBOIT chain. Keep the storage bounded by the full packet set
// so a future topology can regroup packets without silently changing the plan's fixed-capacity contract.
struct FrameExecutionSubmissionBatchPlan{
    FrameExecutionPacket::Enum packets[FrameExecutionPacket::kCount] = {};
    u8 packetCount = 0u;
};


struct FrameExecutionWorkPlan{
    FrameExecutionPacket::Enum packet = FrameExecutionPacket::kCount;
};


// RendererSystem owns the persistent command-list instances for both logical lanes. The plan resolves which one a
// work item needs from its packet lane, so topology predicates do not become a second command-list routing table.
struct FrameExecutionLaneCommandListPair{
    Core::CommandList* graphics = nullptr;
    Core::CommandList* asyncCompute = nullptr;
};


class FrameExecutionPlan final{
public:
    static constexpr usize s_MaxPacketWaits = 2u;
    static constexpr usize s_MaxSubmissionWaits = s_MaxPacketWaits + 1u;


public:
    explicit FrameExecutionPlan(const FrameExecutionPlanInput& input){
        m_usesDedicatedAsyncCompute = input.dedicatedAsyncCompute;
        m_capturesLaggedLightingHistory =
            input.dedicatedAsyncCompute
            && input.frameLaggedAsyncLightingEnabled
        ;
        m_usesLaggedAsyncLighting =
            m_capturesLaggedLightingHistory
            && input.laggedLightingHistoryReady
            && input.laggedLightingHistoryAccepted
        ;
        m_usesAsyncAvboit =
            input.dedicatedAsyncCompute
            && input.hasTransparentRenderers
            && !m_usesLaggedAsyncLighting
        ;

        if(!m_usesDedicatedAsyncCompute){
            enablePacket(FrameExecutionPacket::GraphicsFallback, Core::RenderLane::Graphics);
            assignWork(FrameExecutionWork::GraphicsPrefix, FrameExecutionPacket::GraphicsFallback);
            assignWork(FrameExecutionWork::RayEffects, FrameExecutionPacket::GraphicsFallback);
            assignWork(FrameExecutionWork::Caustics, FrameExecutionPacket::GraphicsFallback);
            assignWork(FrameExecutionWork::SurfelGi, FrameExecutionPacket::GraphicsFallback);
            assignWork(FrameExecutionWork::AvboitRaster, FrameExecutionPacket::GraphicsFallback);
            assignWork(FrameExecutionWork::DeferredLighting, FrameExecutionPacket::GraphicsFallback);
            assignWork(FrameExecutionWork::DeferredComposite, FrameExecutionPacket::GraphicsFallback);
            assignWork(FrameExecutionWork::GraphicsPresent, FrameExecutionPacket::GraphicsFallback);
            configureSubmissionBatches();
            return;
        }

        enablePacket(FrameExecutionPacket::GraphicsPrefix, Core::RenderLane::Graphics);
        assignWork(FrameExecutionWork::GraphicsPrefix, FrameExecutionPacket::GraphicsPrefix);
        enablePacket(FrameExecutionPacket::AsyncRayEffects, Core::RenderLane::AsyncCompute);
        addPacketWait(FrameExecutionPacket::AsyncRayEffects, FrameExecutionPacket::GraphicsPrefix);
        assignWork(FrameExecutionWork::RayEffects, FrameExecutionPacket::AsyncRayEffects);
        assignWork(FrameExecutionWork::Caustics, FrameExecutionPacket::AsyncRayEffects);
        assignWork(FrameExecutionWork::SurfelGi, FrameExecutionPacket::AsyncRayEffects);

        FrameExecutionPacket::Enum graphicsEffectsCompletionPacket = FrameExecutionPacket::GraphicsEffects;
        if(m_usesAsyncAvboit){
            enablePacket(FrameExecutionPacket::GraphicsAvboitPre, Core::RenderLane::Graphics);
            addPacketWait(FrameExecutionPacket::GraphicsAvboitPre, FrameExecutionPacket::GraphicsPrefix);
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
            assignWork(FrameExecutionWork::AvboitRaster, FrameExecutionPacket::GraphicsEffects);
            assignWork(FrameExecutionWork::AsyncEffectsTiming, FrameExecutionPacket::GraphicsEffects);
        }

        const Core::RenderLane::Enum deferredLane = m_usesLaggedAsyncLighting
            ? Core::RenderLane::Graphics
            : Core::RenderLane::AsyncCompute
        ;
        enablePacket(FrameExecutionPacket::DeferredLighting, deferredLane);
        addPacketWait(FrameExecutionPacket::DeferredLighting, graphicsEffectsCompletionPacket);
        assignWork(FrameExecutionWork::DeferredLighting, FrameExecutionPacket::DeferredLighting);
        if(m_usesLaggedAsyncLighting)
            mutablePacket(FrameExecutionPacket::DeferredLighting).waitsForLaggedLightingHistory = true;
        else
            addPacketWait(FrameExecutionPacket::DeferredLighting, FrameExecutionPacket::AsyncRayEffects);

        enablePacket(FrameExecutionPacket::DeferredComposite, deferredLane);
        addPacketWait(FrameExecutionPacket::DeferredComposite, FrameExecutionPacket::DeferredLighting);
        assignWork(FrameExecutionWork::DeferredComposite, FrameExecutionPacket::DeferredComposite);

        enablePacket(FrameExecutionPacket::GraphicsPresent, Core::RenderLane::Graphics);
        addPacketWait(FrameExecutionPacket::GraphicsPresent, FrameExecutionPacket::DeferredComposite);
        assignWork(FrameExecutionWork::GraphicsPresent, FrameExecutionPacket::GraphicsPresent);
        if(m_usesLaggedAsyncLighting)
            addPacketWait(FrameExecutionPacket::GraphicsPresent, FrameExecutionPacket::AsyncRayEffects);

        if(m_capturesLaggedLightingHistory){
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
    [[nodiscard]] bool usesDedicatedAsyncCompute()const noexcept{ return m_usesDedicatedAsyncCompute; }
    [[nodiscard]] bool usesLaggedAsyncLighting()const noexcept{ return m_usesLaggedAsyncLighting; }
    [[nodiscard]] bool capturesLaggedLightingHistory()const noexcept{ return m_capturesLaggedLightingHistory; }
    [[nodiscard]] bool usesAsyncAvboit()const noexcept{ return m_usesAsyncAvboit; }
    [[nodiscard]] bool usesAsyncRayEffects()const noexcept{ return m_usesDedicatedAsyncCompute; }
    [[nodiscard]] bool usesAsyncDeferredLighting()const noexcept{
        return m_usesDedicatedAsyncCompute && !m_usesLaggedAsyncLighting;
    }
    [[nodiscard]] bool usesAsyncCaustics()const noexcept{ return usesAsyncRayEffects(); }
    [[nodiscard]] bool usesAsyncSurfelGi()const noexcept{ return usesAsyncRayEffects(); }
    [[nodiscard]] bool usesGraphicsFallback()const noexcept{ return !m_usesDedicatedAsyncCompute; }
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
    [[nodiscard]] Core::CommandList* commandListForWork(
        const FrameExecutionWork::Enum work,
        const FrameExecutionLaneCommandListPair& commandLists
    )const noexcept{
        if(work >= FrameExecutionWork::kCount || !hasWork(work))
            return nullptr;

        switch(packet(packetForWork(work)).lane){
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
        for(u8 waitPacketIndex = 0u; waitPacketIndex < packetPlan.waitPacketCount; ++waitPacketIndex){
            const FrameExecutionPacket::Enum waitPacket = packetPlan.waitPackets[waitPacketIndex];
            NWB_ASSERT(m_submissionPacketScheduled[static_cast<usize>(waitPacket)]);
        }
        NWB_ASSERT(batchPlan.packetCount < FrameExecutionPacket::kCount);
        batchPlan.packets[batchPlan.packetCount++] = packetID;
        m_submissionPacketScheduled[packetIndex] = true;
    }
    void configureSubmissionBatches()noexcept{
        if(usesGraphicsFallback()){
            appendSubmissionPacket(
                FrameExecutionSubmissionBatch::GraphicsFallback,
                FrameExecutionPacket::GraphicsFallback
            );
            return;
        }

        appendSubmissionPacket(
            FrameExecutionSubmissionBatch::GraphicsPrefix,
            FrameExecutionPacket::GraphicsPrefix
        );
        appendSubmissionPacket(
            FrameExecutionSubmissionBatch::AsyncRayEffects,
            FrameExecutionPacket::AsyncRayEffects
        );
        if(m_usesAsyncAvboit){
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

        // The optional history copy is recorded only after Graphics presentation accepts, so it intentionally stays
        // outside the ordinary pre-recorded submission batches and retains its explicit lifecycle handling.
    }
private:
    FrameExecutionPacketPlan m_packets[FrameExecutionPacket::kCount] = {};
    FrameExecutionWorkPlan m_workPlans[FrameExecutionWork::kCount] = {};
    FrameExecutionSubmissionBatchPlan m_submissionBatches[FrameExecutionSubmissionBatch::kCount] = {};
    FrameExecutionSubmissionBatch::Enum m_submissionBatchOrder[FrameExecutionSubmissionBatch::kCount] = {};
    bool m_submissionPacketScheduled[FrameExecutionPacket::kCount] = {};
    usize m_submissionBatchCount = 0u;
    bool m_usesDedicatedAsyncCompute = false;
    bool m_usesLaggedAsyncLighting = false;
    bool m_capturesLaggedLightingHistory = false;
    bool m_usesAsyncAvboit = false;
};


// Recording stays in RendererSystem because it owns the resource-state handoffs, but the ordered command lists
// submitted for each packet follow the plan's work mapping. A work item may append more than once: the async-effects
// timing bracket deliberately contributes a begin and an end command list around its packet's other work.
struct FrameExecutionPacketCommandListRange{
    Core::CommandList* const* commandLists = nullptr;
    usize commandListCount = 0u;
};


// RendererSystem owns the actual command lists, while the plan owns their work-to-packet routing. Supplying these
// bindings in recording order keeps optional topology work out of the renderer's submission assembly branches.
struct FrameExecutionWorkCommandListBinding{
    FrameExecutionWork::Enum work = FrameExecutionWork::kCount;
    Core::CommandList* commandList = nullptr;
};


class FrameExecutionPacketCommandLists final{
public:
    // Graphics fallback currently contains the largest packet: twelve lists. Keep this constrained collector
    // allocation-free and make any future packet-size increase explicit in the plan contract.
    static constexpr usize s_MaxCommandListsPerPacket = 12u;


public:
    explicit FrameExecutionPacketCommandLists(const FrameExecutionPlan& plan)noexcept
        : m_plan(plan)
    {}


public:
    // Disabled work is intentionally skipped: callers can declare the complete canonical recording order once,
    // including command lists that are only instantiated by a split topology.
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


// Mutable, per-frame execution state for a FrameExecutionPlan. It resolves the plan's predecessor edges into
// accepted submission tokens and retains the newest accepted AsyncCompute edge for recovery. The renderer still
// owns recovery lifecycle handling, while this state decides whether an accepted Compute packet remains to join.
class FrameExecutionPlanSubmissionState final{
public:
    explicit FrameExecutionPlanSubmissionState(
        const FrameExecutionPlan& plan,
        const Core::QueueSubmissionToken laggedLightingHistoryToken = {}
    )noexcept
        : m_plan(plan)
        , m_laggedLightingHistoryToken(laggedLightingHistoryToken)
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
            + (packetPlan.waitsForLaggedLightingHistory ? 1u : 0u)
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
        if(packetPlan.waitsForLaggedLightingHistory){
            if(!m_laggedLightingHistoryToken.valid())
                return false;
            waitTokens[waitTokenCount++] = m_laggedLightingHistoryToken;
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
    // A null result means no accepted AsyncCompute packet is outstanding, so failure recovery has no queue join to
    // submit. A non-null token is always the latest accepted packet on the ordered Compute lane, which also covers
    // every earlier accepted Compute packet on that lane.
    [[nodiscard]] const Core::QueueSubmissionToken* asyncRecoveryWaitToken()const noexcept{
        return m_asyncRecoveryWaitToken.valid() ? &m_asyncRecoveryWaitToken : nullptr;
    }


private:
    const FrameExecutionPlan& m_plan;
    Core::QueueSubmissionToken m_laggedLightingHistoryToken;
    Core::QueueSubmissionToken m_packetTokens[FrameExecutionPacket::kCount] = {};
    Core::QueueSubmissionToken m_asyncRecoveryWaitToken;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
