// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing_metric_correlator.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_metric_correlator{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Retain enough rejected-frame endpoints to pair an asynchronously arriving timestamp without allowing the correlation caches to grow unbounded.
inline constexpr u64 s_PendingMetricRetentionFrames = static_cast<u64>(s_MaxFramesInFlight) * 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingMetricCorrelator::GpuTimingMetricCorrelator(Alloc::GlobalArena& arena, Perf::TimingSink& timing)noexcept
    : m_arena(arena)
    , m_timing(timing)
    , m_overlapMetrics(arena, timing)
    , m_pendingPacketEnvelopeMetrics(arena)
    , m_packetEnvelopeMetricOutputRoles(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTimingMetricCorrelator::prepareOverlapMetric(const Name& firstScope, const Name& secondScope, const Name& outputScope){
    if(!firstScope || !secondScope || !outputScope || firstScope == secondScope || outputScope == firstScope || outputScope == secondScope)
        return false;

    for(const PacketEnvelopeMetricOutputRoleRecord& packetEnvelopeOutput : m_packetEnvelopeMetricOutputRoles){
        if(firstScope == packetEnvelopeOutput.scopeName || secondScope == packetEnvelopeOutput.scopeName || outputScope == packetEnvelopeOutput.scopeName)
            return false;
    }

    return m_overlapMetrics.prepare(firstScope, secondScope, outputScope);
}

b8 GpuTimingMetricCorrelator::preparePacketEnvelopeMetrics(
    const u64 sourceFrameIndex,
    const Span<const GpuPacketEnvelopeMetricScope> scopeInputs,
    const Name& queueOverlapScope,
    const Span<const GpuPacketEnvelopeMetricQueueOutput> queueOutputInputs
){
    const GpuPacketEnvelopeMetricScope* const scopes = scopeInputs.data();
    const usize scopeCount = scopeInputs.size();
    const GpuPacketEnvelopeMetricQueueOutput* const queueOutputs = queueOutputInputs.data();
    const usize queueOutputCount = queueOutputInputs.size();
    if(scopeCount == 0u || !scopes || !queueOverlapScope || queueOutputCount == 0u || !queueOutputs || queueOutputCount > scopeCount)
        return false;

    for(auto it = m_pendingPacketEnvelopeMetrics.begin(); it != m_pendingPacketEnvelopeMetrics.end(); ){
        if(sourceFrameIndex > it->sourceFrameIndex && sourceFrameIndex - it->sourceFrameIndex > __hidden_gpu_timing_metric_correlator::s_PendingMetricRetentionFrames)
            it = m_pendingPacketEnvelopeMetrics.erase(it);
        else
            ++it;
    }

    // The validated index moves into the pending frame, keeping lookup and duplicate checks on the same identity.
    PendingPacketEnvelopeMetric candidate(m_arena);
    candidate.scopeIndices.reserve(scopeCount);
    const u16 deviceGeneration = scopes[0u].physicalQueue.deviceGeneration;
    for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
        const GpuPacketEnvelopeMetricScope& scope = scopes[scopeIndex];
        if(!scope.scopeName || !scope.physicalQueue.valid() || scope.physicalQueue.deviceGeneration != deviceGeneration || scope.scopeName == queueOverlapScope)
            return false;
        if(!candidate.scopeIndices.try_emplace(scope.scopeName, scopeIndex).second)
            return false;
        for(const PacketEnvelopeMetricOutputRoleRecord& outputRole : m_packetEnvelopeMetricOutputRoles){
            if(scope.scopeName == outputRole.scopeName)
                return false;
        }
    }

    for(const PacketEnvelopeMetricOutputRoleRecord& outputRole : m_packetEnvelopeMetricOutputRoles){
        if(outputRole.scopeName == queueOverlapScope && outputRole.queueInternalIdle)
            return false;
    }

    for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
        const GpuPacketEnvelopeMetricQueueOutput& output = queueOutputs[outputIndex];
        if(!output.physicalQueue.valid() || output.physicalQueue.deviceGeneration != deviceGeneration || !output.internalIdleScopeName || output.internalIdleScopeName == queueOverlapScope)
            return false;
        for(usize previousOutputIndex = 0u; previousOutputIndex < outputIndex; ++previousOutputIndex){
            const GpuPacketEnvelopeMetricQueueOutput& previousOutput = queueOutputs[previousOutputIndex];
            if(previousOutput.physicalQueue == output.physicalQueue || previousOutput.internalIdleScopeName == output.internalIdleScopeName)
                return false;
        }

        b8 queueHasInput = false;
        for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
            if(scopes[scopeIndex].scopeName == output.internalIdleScopeName)
                return false;
            queueHasInput = queueHasInput || scopes[scopeIndex].physicalQueue == output.physicalQueue;
        }
        if(!queueHasInput)
            return false;
        for(const PacketEnvelopeMetricOutputRoleRecord& outputRole : m_packetEnvelopeMetricOutputRoles){
            if(outputRole.scopeName != output.internalIdleScopeName)
                continue;
            if(!outputRole.queueInternalIdle || outputRole.physicalQueue != output.physicalQueue)
                return false;
        }
    }
    for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
        b8 hasQueueOutput = false;
        for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex)
            hasQueueOutput = hasQueueOutput || queueOutputs[outputIndex].physicalQueue == scopes[scopeIndex].physicalQueue;
        if(!hasQueueOutput)
            return false;
    }

    if(m_overlapMetrics.conflicts(scopeInputs, queueOverlapScope, queueOutputInputs))
        return false;

    for(const PendingPacketEnvelopeMetric& pending : m_pendingPacketEnvelopeMetrics){
        for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
            if(scopes[scopeIndex].scopeName == pending.queueOverlapScopeName)
                return false;
            for(const PacketEnvelopeMetricQueueOutputRecord& output : pending.queueOutputs){
                if(scopes[scopeIndex].scopeName == output.internalIdleScopeName)
                    return false;
            }
        }
        for(const PacketEnvelopeMetricScopeRecord& pendingScope : pending.scopes){
            if(pendingScope.scopeName == queueOverlapScope)
                return false;
            for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
                if(pendingScope.scopeName == queueOutputs[outputIndex].internalIdleScopeName)
                    return false;
            }
        }

        if(pending.sourceFrameIndex != sourceFrameIndex)
            continue;
        b8 sharesOutput = pending.queueOverlapScopeName == queueOverlapScope;
        for(const PacketEnvelopeMetricQueueOutputRecord& pendingOutput : pending.queueOutputs){
            for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
                sharesOutput = sharesOutput || pendingOutput.internalIdleScopeName == queueOutputs[outputIndex].internalIdleScopeName;
            }
        }
        if(!sharesOutput)
            continue;
        if(pending.queueOverlapScopeName != queueOverlapScope)
            return false;
        if(pending.scopes.size() != scopeCount || pending.queueOutputs.size() != queueOutputCount)
            return false;
        for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
            if(pending.scopes[scopeIndex].scopeName != scopes[scopeIndex].scopeName || pending.scopes[scopeIndex].physicalQueue != scopes[scopeIndex].physicalQueue)
                return false;
        }
        for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
            if(pending.queueOutputs[outputIndex].physicalQueue != queueOutputs[outputIndex].physicalQueue || pending.queueOutputs[outputIndex].internalIdleScopeName != queueOutputs[outputIndex].internalIdleScopeName)
                return false;
        }
        return true;
    }

    const auto overlapOutput = m_timing.registerScope(queueOverlapScope);
    if(!overlapOutput)
        return false;
    rememberMetricOutput(queueOverlapScope, {}, false);

    candidate.sourceFrameIndex = sourceFrameIndex;
    candidate.queueOverlapScopeName = queueOverlapScope;
    candidate.queueOverlapScope = *overlapOutput;
    candidate.scopes.reserve(scopeCount);
    candidate.queueOutputs.reserve(queueOutputCount);
    candidate.remainingScopeCount = scopeCount;
    for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
        candidate.scopes.push_back(PacketEnvelopeMetricScopeRecord{ .range = {}, .scopeName = scopes[scopeIndex].scopeName, .physicalQueue = scopes[scopeIndex].physicalQueue, .received = false, });
    }
    for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
        const GpuPacketEnvelopeMetricQueueOutput& output = queueOutputs[outputIndex];
        const auto idleOutput = m_timing.registerScope(output.internalIdleScopeName);
        if(!idleOutput)
            return false;
        rememberMetricOutput(output.internalIdleScopeName, output.physicalQueue, true);
        candidate.queueOutputs.push_back(PacketEnvelopeMetricQueueOutputRecord{ .physicalQueue = output.physicalQueue, .internalIdleScopeName = output.internalIdleScopeName, .internalIdleScope = *idleOutput, });
    }

    // Publish only a complete frame; candidate cleanup preserves output roles already recorded above.
    m_pendingPacketEnvelopeMetrics.push_back(Move(candidate));
    return true;
}

void GpuTimingMetricCorrelator::recordTimestampRange(const Name& scopeName, const u64 frameIndex, const GpuComparableTimestampRange& range, Alloc::ScratchArena& scratchArena, GpuTimingSinkSampleVector& performanceSamples){
    m_overlapMetrics.record(scopeName, frameIndex, range, __hidden_gpu_timing_metric_correlator::s_PendingMetricRetentionFrames, performanceSamples);

    for(auto it = m_pendingPacketEnvelopeMetrics.begin(); it != m_pendingPacketEnvelopeMetrics.end(); ){
        if(frameIndex > it->sourceFrameIndex && frameIndex - it->sourceFrameIndex > __hidden_gpu_timing_metric_correlator::s_PendingMetricRetentionFrames){
            it = m_pendingPacketEnvelopeMetrics.erase(it);
            continue;
        }
        if(frameIndex != it->sourceFrameIndex){
            ++it;
            continue;
        }

        const auto scopeIndex = it->scopeIndices.find(scopeName);
        if(scopeIndex != it->scopeIndices.end()){
            PacketEnvelopeMetricScopeRecord& scope = it->scopes[scopeIndex.value()];
            if(scope.physicalQueue == range.physicalQueue){
                scope.range = range;
                if(!scope.received){
                    NWB_ASSERT(it->remainingScopeCount > 0u);
                    scope.received = true;
                    --it->remainingScopeCount;
                }
            }
        }

        if(it->remainingScopeCount != 0u){
            ++it;
            continue;
        }

        performanceSamples.reserve(performanceSamples.size() + 1u + it->queueOutputs.size());
        Vector<GpuComparableTimestampRange, Alloc::ScratchArena> packetRanges{scratchArena};
        packetRanges.reserve(it->scopes.size());
        for(const PacketEnvelopeMetricScopeRecord& scope : it->scopes)
            packetRanges.push_back(scope.range);

        GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
        const auto envelopeMetrics = TryAggregateGpuPacketEnvelopeMetrics(packetRanges, scratchArena, queueMetrics);
        b8 aggregated = envelopeMetrics && queueMetrics.size() == it->queueOutputs.size();
        for(const GpuQueuePacketEnvelopeMetrics& queueMetric : queueMetrics){
            b8 hasOutput = false;
            for(const PacketEnvelopeMetricQueueOutputRecord& output : it->queueOutputs)
                hasOutput = hasOutput || output.physicalQueue == queueMetric.physicalQueue;
            aggregated = aggregated && hasOutput;
        }

        if(aggregated){
            const f64 secondsPerTick = envelopeMetrics->secondsPerTick;
            const f64 overlapSeconds = static_cast<f64>(envelopeMetrics->queueOverlapTicks) * secondsPerTick;
            performanceSamples.push_back(GpuTimingSinkSample{ .scope = it->queueOverlapScope, .durationSeconds = overlapSeconds, .sourceFrameIndex = it->sourceFrameIndex, });
            for(const GpuQueuePacketEnvelopeMetrics& queueMetric : queueMetrics){
                for(const PacketEnvelopeMetricQueueOutputRecord& output : it->queueOutputs){
                    if(output.physicalQueue != queueMetric.physicalQueue)
                        continue;
                    const f64 idleSeconds = static_cast<f64>(queueMetric.internalIdleTicks) * secondsPerTick;
                    performanceSamples.push_back(GpuTimingSinkSample{ .scope = output.internalIdleScope, .durationSeconds = idleSeconds, .sourceFrameIndex = it->sourceFrameIndex, });
                    break;
                }
            }
        }
        it = m_pendingPacketEnvelopeMetrics.erase(it);
    }
}

b8 GpuTimingMetricCorrelator::hasOutputRole(const Name& scopeName)const noexcept{
    if(m_overlapMetrics.hasOutputRole(scopeName))
        return true;
    for(const PacketEnvelopeMetricOutputRoleRecord& outputRole : m_packetEnvelopeMetricOutputRoles){
        if(outputRole.scopeName == scopeName)
            return true;
    }
    return false;
}

void GpuTimingMetricCorrelator::discardPendingRanges()noexcept{
    m_overlapMetrics.discardPendingRanges();
    m_pendingPacketEnvelopeMetrics.clear();
}

void GpuTimingMetricCorrelator::reset()noexcept{
    m_overlapMetrics.reset();
    m_pendingPacketEnvelopeMetrics.clear();
    m_packetEnvelopeMetricOutputRoles.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTimingMetricCorrelator::rememberMetricOutput(const Name& name, const GpuPhysicalQueueId& queue, const b8 internalIdle){
    for(const PacketEnvelopeMetricOutputRoleRecord& outputRole : m_packetEnvelopeMetricOutputRoles){
        if(outputRole.scopeName != name)
            continue;
        NWB_ASSERT(outputRole.physicalQueue == queue);
        NWB_ASSERT(outputRole.queueInternalIdle == internalIdle);
        return;
    }
    m_packetEnvelopeMetricOutputRoles.push_back(PacketEnvelopeMetricOutputRoleRecord{ .scopeName = name, .physicalQueue = queue, .queueInternalIdle = internalIdle, });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

