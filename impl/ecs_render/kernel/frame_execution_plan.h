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
    bool waitsForLaggedLightingHistory = false;
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
            return;
        }

        enablePacket(FrameExecutionPacket::GraphicsPrefix, Core::RenderLane::Graphics);
        enablePacket(FrameExecutionPacket::AsyncRayEffects, Core::RenderLane::AsyncCompute);
        addPacketWait(FrameExecutionPacket::AsyncRayEffects, FrameExecutionPacket::GraphicsPrefix);

        FrameExecutionPacket::Enum graphicsEffectsCompletionPacket = FrameExecutionPacket::GraphicsEffects;
        if(m_usesAsyncAvboit){
            enablePacket(FrameExecutionPacket::GraphicsAvboitPre, Core::RenderLane::Graphics);
            addPacketWait(FrameExecutionPacket::GraphicsAvboitPre, FrameExecutionPacket::GraphicsPrefix);

            enablePacket(FrameExecutionPacket::AsyncAvboitDepthWarp, Core::RenderLane::AsyncCompute);
            addPacketWait(FrameExecutionPacket::AsyncAvboitDepthWarp, FrameExecutionPacket::GraphicsAvboitPre);

            enablePacket(FrameExecutionPacket::GraphicsAvboitExtinction, Core::RenderLane::Graphics);
            addPacketWait(FrameExecutionPacket::GraphicsAvboitExtinction, FrameExecutionPacket::AsyncAvboitDepthWarp);

            enablePacket(FrameExecutionPacket::AsyncAvboitIntegration, Core::RenderLane::AsyncCompute);
            addPacketWait(FrameExecutionPacket::AsyncAvboitIntegration, FrameExecutionPacket::GraphicsAvboitExtinction);

            enablePacket(FrameExecutionPacket::GraphicsAvboitAccumulation, Core::RenderLane::Graphics);
            addPacketWait(FrameExecutionPacket::GraphicsAvboitAccumulation, FrameExecutionPacket::AsyncAvboitIntegration);
            graphicsEffectsCompletionPacket = FrameExecutionPacket::GraphicsAvboitAccumulation;
        }
        else{
            enablePacket(FrameExecutionPacket::GraphicsEffects, Core::RenderLane::Graphics);
            addPacketWait(FrameExecutionPacket::GraphicsEffects, FrameExecutionPacket::GraphicsPrefix);
        }

        const Core::RenderLane::Enum deferredLane = m_usesLaggedAsyncLighting
            ? Core::RenderLane::Graphics
            : Core::RenderLane::AsyncCompute
        ;
        enablePacket(FrameExecutionPacket::DeferredLighting, deferredLane);
        addPacketWait(FrameExecutionPacket::DeferredLighting, graphicsEffectsCompletionPacket);
        if(m_usesLaggedAsyncLighting)
            mutablePacket(FrameExecutionPacket::DeferredLighting).waitsForLaggedLightingHistory = true;
        else
            addPacketWait(FrameExecutionPacket::DeferredLighting, FrameExecutionPacket::AsyncRayEffects);

        enablePacket(FrameExecutionPacket::DeferredComposite, deferredLane);
        addPacketWait(FrameExecutionPacket::DeferredComposite, FrameExecutionPacket::DeferredLighting);

        enablePacket(FrameExecutionPacket::GraphicsPresent, Core::RenderLane::Graphics);
        addPacketWait(FrameExecutionPacket::GraphicsPresent, FrameExecutionPacket::DeferredComposite);
        if(m_usesLaggedAsyncLighting)
            addPacketWait(FrameExecutionPacket::GraphicsPresent, FrameExecutionPacket::AsyncRayEffects);

        if(m_capturesLaggedLightingHistory){
            enablePacket(FrameExecutionPacket::AsyncLaggedLightingStash, Core::RenderLane::AsyncCompute);
            addPacketWait(FrameExecutionPacket::AsyncLaggedLightingStash, FrameExecutionPacket::GraphicsPresent);
        }
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


private:
    [[nodiscard]] FrameExecutionPacketPlan& mutablePacket(const FrameExecutionPacket::Enum packet)noexcept{
        return m_packets[static_cast<usize>(packet)];
    }
    void enablePacket(const FrameExecutionPacket::Enum packetID, const Core::RenderLane::Enum lane)noexcept{
        FrameExecutionPacketPlan& packetPlan = mutablePacket(packetID);
        packetPlan.lane = lane;
        packetPlan.enabled = true;
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


private:
    FrameExecutionPacketPlan m_packets[FrameExecutionPacket::kCount] = {};
    bool m_usesDedicatedAsyncCompute = false;
    bool m_usesLaggedAsyncLighting = false;
    bool m_capturesLaggedLightingHistory = false;
    bool m_usesAsyncAvboit = false;
};


// Mutable, per-frame execution state for a FrameExecutionPlan. It resolves the plan's predecessor edges into
// accepted submission tokens and retains the most recent accepted token per lane for recovery joins. The renderer
// still decides when to recover; this object only preserves the exact completion edge that recovery must join.
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
        m_latestLaneTokens[static_cast<usize>(lane)] = submissionToken;
    }

    [[nodiscard]] Core::QueueSubmissionToken token(const FrameExecutionPacket::Enum packet)const noexcept{
        return m_packetTokens[static_cast<usize>(packet)];
    }
    [[nodiscard]] Core::QueueSubmissionToken latestAcceptedToken(const Core::RenderLane::Enum lane)const noexcept{
        return m_latestLaneTokens[static_cast<usize>(lane)];
    }


private:
    const FrameExecutionPlan& m_plan;
    Core::QueueSubmissionToken m_laggedLightingHistoryToken;
    Core::QueueSubmissionToken m_packetTokens[FrameExecutionPacket::kCount] = {};
    Core::QueueSubmissionToken m_latestLaneTokens[Core::RenderLane::kCount] = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
