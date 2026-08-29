// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_timing_metric_correlator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_metric_correlator{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Retain enough rejected-frame endpoints to pair an asynchronously arriving timestamp without allowing the
// correlation caches to grow unbounded.
inline constexpr u64 s_PendingMetricRetentionFrames = static_cast<u64>(s_MaxFramesInFlight) * 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTimingMetricCorrelator::GpuTimingMetricCorrelator(Alloc::GlobalArena& arena, Perf::TimingSink& timing)
    : m_arena(arena)
    , m_timing(timing)
    , m_overlapRecords(arena)
    , m_pendingPacketEnvelopeMetrics(arena)
    , m_packetEnvelopeMetricOutputRoles(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTimingMetricCorrelator::prepareOverlapMetric(
    const Name& firstScope,
    const Name& secondScope,
    const Name& outputScope
){
    if(
        !firstScope
        || !secondScope
        || !outputScope
        || firstScope == secondScope
        || outputScope == firstScope
        || outputScope == secondScope
    )
        return false;

    for(const PacketEnvelopeMetricOutputRoleRecord& packetEnvelopeOutput : m_packetEnvelopeMetricOutputRoles){
        if(
            firstScope == packetEnvelopeOutput.scopeName
            || secondScope == packetEnvelopeOutput.scopeName
            || outputScope == packetEnvelopeOutput.scopeName
        )
            return false;
    }

    Name canonicalFirstScope = firstScope;
    Name canonicalSecondScope = secondScope;
    if(canonicalSecondScope < canonicalFirstScope)
        Swap(canonicalFirstScope, canonicalSecondScope);

    for(const OverlapRecord& record : m_overlapRecords){
        if(record.outputScopeName == canonicalFirstScope || record.outputScopeName == canonicalSecondScope)
            return false;
        if(outputScope == record.firstScope || outputScope == record.secondScope)
            return false;

        const bool sameInputs = record.firstScope == canonicalFirstScope && record.secondScope == canonicalSecondScope;
        if(sameInputs)
            return record.outputScopeName == outputScope && record.outputScope.valid();
        if(record.outputScopeName == outputScope)
            return false;
    }

    const Perf::TimingScopeId output = m_timing.registerScope(outputScope);
    if(!output.valid())
        return false;

    m_overlapRecords.emplace_back(m_arena);
    OverlapRecord& record = m_overlapRecords.back();
    record.firstScope = canonicalFirstScope;
    record.secondScope = canonicalSecondScope;
    record.outputScopeName = outputScope;
    record.outputScope = output;
    return true;
}

bool GpuTimingMetricCorrelator::preparePacketEnvelopeMetrics(
    const u64 sourceFrameIndex,
    const NotNull<const GpuPacketEnvelopeMetricScope*> scopeInputs,
    const usize scopeCount,
    const Name& queueOverlapScope,
    const NotNull<const GpuPacketEnvelopeMetricQueueOutput*> queueOutputInputs,
    const usize queueOutputCount
){
    const GpuPacketEnvelopeMetricScope* const scopes = scopeInputs.get();
    const GpuPacketEnvelopeMetricQueueOutput* const queueOutputs = queueOutputInputs.get();
    if(
        scopeCount == 0u
        || !queueOverlapScope
        || queueOutputCount == 0u
        || queueOutputCount > scopeCount
    )
        return false;

    for(auto it = m_pendingPacketEnvelopeMetrics.begin(); it != m_pendingPacketEnvelopeMetrics.end(); ){
        if(
            sourceFrameIndex > it->sourceFrameIndex
            && sourceFrameIndex - it->sourceFrameIndex
                > __hidden_gpu_timing_metric_correlator::s_PendingMetricRetentionFrames
        )
            it = m_pendingPacketEnvelopeMetrics.erase(it);
        else
            ++it;
    }

    const u16 deviceGeneration = scopes[0u].physicalQueue.deviceGeneration;
    for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
        const GpuPacketEnvelopeMetricScope& scope = scopes[scopeIndex];
        if(
            !scope.scopeName
            || !scope.physicalQueue.valid()
            || scope.physicalQueue.deviceGeneration != deviceGeneration
            || scope.scopeName == queueOverlapScope
        )
            return false;
        for(usize previousScopeIndex = 0u; previousScopeIndex < scopeIndex; ++previousScopeIndex){
            if(scopes[previousScopeIndex].scopeName == scope.scopeName)
                return false;
        }
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
        if(
            !output.physicalQueue.valid()
            || output.physicalQueue.deviceGeneration != deviceGeneration
            || !output.internalIdleScopeName
            || output.internalIdleScopeName == queueOverlapScope
        )
            return false;
        for(usize previousOutputIndex = 0u; previousOutputIndex < outputIndex; ++previousOutputIndex){
            const GpuPacketEnvelopeMetricQueueOutput& previousOutput = queueOutputs[previousOutputIndex];
            if(
                previousOutput.physicalQueue == output.physicalQueue
                || previousOutput.internalIdleScopeName == output.internalIdleScopeName
            )
                return false;
        }

        bool queueHasInput = false;
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
        bool hasQueueOutput = false;
        for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex)
            hasQueueOutput = hasQueueOutput || queueOutputs[outputIndex].physicalQueue == scopes[scopeIndex].physicalQueue;
        if(!hasQueueOutput)
            return false;
    }

    for(const OverlapRecord& overlap : m_overlapRecords){
        for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
            if(scopes[scopeIndex].scopeName == overlap.outputScopeName)
                return false;
        }
        if(
            queueOverlapScope == overlap.firstScope
            || queueOverlapScope == overlap.secondScope
            || queueOverlapScope == overlap.outputScopeName
        )
            return false;
        for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
            const Name& outputName = queueOutputs[outputIndex].internalIdleScopeName;
            if(
                outputName == overlap.firstScope
                || outputName == overlap.secondScope
                || outputName == overlap.outputScopeName
            )
                return false;
        }
    }

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
        bool sharesOutput = pending.queueOverlapScopeName == queueOverlapScope;
        for(const PacketEnvelopeMetricQueueOutputRecord& pendingOutput : pending.queueOutputs){
            for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
                sharesOutput = sharesOutput
                    || pendingOutput.internalIdleScopeName == queueOutputs[outputIndex].internalIdleScopeName
                ;
            }
        }
        if(!sharesOutput)
            continue;
        if(pending.queueOverlapScopeName != queueOverlapScope)
            return false;
        if(pending.scopes.size() != scopeCount || pending.queueOutputs.size() != queueOutputCount)
            return false;
        for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
            if(
                pending.scopes[scopeIndex].scopeName != scopes[scopeIndex].scopeName
                || pending.scopes[scopeIndex].physicalQueue != scopes[scopeIndex].physicalQueue
            )
                return false;
        }
        for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
            if(
                pending.queueOutputs[outputIndex].physicalQueue != queueOutputs[outputIndex].physicalQueue
                || pending.queueOutputs[outputIndex].internalIdleScopeName != queueOutputs[outputIndex].internalIdleScopeName
            )
                return false;
        }
        return true;
    }

    const Perf::TimingScopeId overlapOutput = m_timing.registerScope(queueOverlapScope);
    if(!overlapOutput.valid())
        return false;
    rememberMetricOutput(queueOverlapScope, {}, false);

    m_pendingPacketEnvelopeMetrics.emplace_back(m_arena);
    PendingPacketEnvelopeMetric& pending = m_pendingPacketEnvelopeMetrics.back();
    pending.sourceFrameIndex = sourceFrameIndex;
    pending.queueOverlapScopeName = queueOverlapScope;
    pending.queueOverlapScope = overlapOutput;
    pending.scopes.reserve(scopeCount);
    pending.queueOutputs.reserve(queueOutputCount);
    for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
        pending.scopes.push_back(PacketEnvelopeMetricScopeRecord{
            .scopeName = scopes[scopeIndex].scopeName,
            .physicalQueue = scopes[scopeIndex].physicalQueue,
            .range = {},
            .received = false,
        });
    }
    for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
        const GpuPacketEnvelopeMetricQueueOutput& output = queueOutputs[outputIndex];
        const Perf::TimingScopeId idleOutput = m_timing.registerScope(output.internalIdleScopeName);
        if(!idleOutput.valid()){
            m_pendingPacketEnvelopeMetrics.pop_back();
            return false;
        }
        rememberMetricOutput(output.internalIdleScopeName, output.physicalQueue, true);
        pending.queueOutputs.push_back(PacketEnvelopeMetricQueueOutputRecord{
            .physicalQueue = output.physicalQueue,
            .internalIdleScopeName = output.internalIdleScopeName,
            .internalIdleScope = idleOutput,
        });
    }

    return true;
}

void GpuTimingMetricCorrelator::recordTimestampRange(
    const Name& scopeName,
    const u64 frameIndex,
    const GpuComparableTimestampRange& range,
    Alloc::ScratchArena& scratchArena
){
    for(OverlapRecord& record : m_overlapRecords){
        if(scopeName != record.firstScope && scopeName != record.secondScope)
            continue;

        for(auto it = record.pendingFrames.begin(); it != record.pendingFrames.end(); ){
            if(
                frameIndex > it->frameIndex
                && frameIndex - it->frameIndex > __hidden_gpu_timing_metric_correlator::s_PendingMetricRetentionFrames
            )
                it = record.pendingFrames.erase(it);
            else
                ++it;
        }

        PendingOverlapFrame* frame = nullptr;
        for(PendingOverlapFrame& candidate : record.pendingFrames){
            if(candidate.frameIndex == frameIndex){
                frame = &candidate;
                break;
            }
        }
        if(!frame){
            record.pendingFrames.emplace_back();
            frame = &record.pendingFrames.back();
            frame->frameIndex = frameIndex;
        }

        if(scopeName == record.firstScope){
            frame->first = range;
            frame->hasFirst = true;
        }
        else{
            frame->second = range;
            frame->hasSecond = true;
        }

        if(!frame->hasFirst || !frame->hasSecond)
            continue;

        u64 overlapTicks = 0u;
        if(TryComputeGpuTimestampOverlap(frame->first, frame->second, overlapTicks)){
            const f64 overlapSeconds = static_cast<f64>(overlapTicks) * frame->first.secondsPerTick;
            m_timing.recordSample(record.outputScope, overlapSeconds, frameIndex);
        }
        for(auto it = record.pendingFrames.begin(); it != record.pendingFrames.end(); ++it){
            if(&*it == frame){
                record.pendingFrames.erase(it);
                break;
            }
        }
    }

    for(auto it = m_pendingPacketEnvelopeMetrics.begin(); it != m_pendingPacketEnvelopeMetrics.end(); ){
        if(
            frameIndex > it->sourceFrameIndex
            && frameIndex - it->sourceFrameIndex
                > __hidden_gpu_timing_metric_correlator::s_PendingMetricRetentionFrames
        ){
            it = m_pendingPacketEnvelopeMetrics.erase(it);
            continue;
        }
        if(frameIndex != it->sourceFrameIndex){
            ++it;
            continue;
        }

        for(PacketEnvelopeMetricScopeRecord& scope : it->scopes){
            if(scope.scopeName != scopeName || scope.physicalQueue != range.physicalQueue)
                continue;
            scope.range = range;
            scope.received = true;
            break;
        }

        bool complete = true;
        for(const PacketEnvelopeMetricScopeRecord& scope : it->scopes)
            complete = complete && scope.received;
        if(!complete){
            ++it;
            continue;
        }

        Vector<GpuComparableTimestampRange, Alloc::ScratchArena> packetRanges{scratchArena};
        packetRanges.reserve(it->scopes.size());
        for(const PacketEnvelopeMetricScopeRecord& scope : it->scopes)
            packetRanges.push_back(scope.range);

        GpuPacketEnvelopeMetrics envelopeMetrics;
        GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
        bool aggregated = TryAggregateGpuPacketEnvelopeMetrics(
            packetRanges.data(),
            packetRanges.size(),
            envelopeMetrics,
            queueMetrics,
            scratchArena
        );
        aggregated = aggregated && queueMetrics.size() == it->queueOutputs.size();
        for(const GpuQueuePacketEnvelopeMetrics& queueMetric : queueMetrics){
            bool hasOutput = false;
            for(const PacketEnvelopeMetricQueueOutputRecord& output : it->queueOutputs)
                hasOutput = hasOutput || output.physicalQueue == queueMetric.physicalQueue;
            aggregated = aggregated && hasOutput;
        }

        if(aggregated){
            const f64 secondsPerTick = envelopeMetrics.secondsPerTick;
            const f64 overlapSeconds = static_cast<f64>(envelopeMetrics.queueOverlapTicks) * secondsPerTick;
            m_timing.recordSample(it->queueOverlapScope, overlapSeconds, it->sourceFrameIndex);
            for(const GpuQueuePacketEnvelopeMetrics& queueMetric : queueMetrics){
                for(const PacketEnvelopeMetricQueueOutputRecord& output : it->queueOutputs){
                    if(output.physicalQueue != queueMetric.physicalQueue)
                        continue;
                    const f64 idleSeconds = static_cast<f64>(queueMetric.internalIdleTicks) * secondsPerTick;
                    m_timing.recordSample(output.internalIdleScope, idleSeconds, it->sourceFrameIndex);
                    break;
                }
            }
        }
        it = m_pendingPacketEnvelopeMetrics.erase(it);
    }
}

bool GpuTimingMetricCorrelator::hasOutputRole(const Name& scopeName)const{
    for(const OverlapRecord& record : m_overlapRecords){
        if(record.outputScopeName == scopeName)
            return true;
    }
    for(const PacketEnvelopeMetricOutputRoleRecord& outputRole : m_packetEnvelopeMetricOutputRoles){
        if(outputRole.scopeName == scopeName)
            return true;
    }
    return false;
}

void GpuTimingMetricCorrelator::discardPendingRanges(){
    for(OverlapRecord& record : m_overlapRecords)
        record.pendingFrames.clear();
    m_pendingPacketEnvelopeMetrics.clear();
}

void GpuTimingMetricCorrelator::reset(){
    m_overlapRecords.clear();
    m_pendingPacketEnvelopeMetrics.clear();
    m_packetEnvelopeMetricOutputRoles.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTimingMetricCorrelator::rememberMetricOutput(
    const Name& name,
    const GpuPhysicalQueueId& queue,
    const bool internalIdle
){
    for(const PacketEnvelopeMetricOutputRoleRecord& outputRole : m_packetEnvelopeMetricOutputRoles){
        if(outputRole.scopeName != name)
            continue;
        NWB_ASSERT(outputRole.physicalQueue == queue);
        NWB_ASSERT(outputRole.queueInternalIdle == internalIdle);
        return;
    }
    m_packetEnvelopeMetricOutputRoles.push_back(PacketEnvelopeMetricOutputRoleRecord{
        .scopeName = name,
        .physicalQueue = queue,
        .queueInternalIdle = internalIdle,
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

