// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_overlap_metric_tracker.h"

#include <global/simd/vector_arithmetic.h>
#include "gpu_timing_metric_correlator.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


b8 GpuOverlapMetricTracker::prepare(const Name& firstScope, const Name& secondScope, const Name& outputScope){
    Name canonicalFirstScope = firstScope;
    Name canonicalSecondScope = secondScope;
    if(canonicalSecondScope < canonicalFirstScope)
        Swap(canonicalFirstScope, canonicalSecondScope);

    for(const OverlapRecord& record : m_overlapRecords){
        if(record.outputScopeName == canonicalFirstScope || record.outputScopeName == canonicalSecondScope)
            return false;
        if(outputScope == record.firstScope || outputScope == record.secondScope)
            return false;

        const b8 sameInputs = record.firstScope == canonicalFirstScope && record.secondScope == canonicalSecondScope;
        if(sameInputs)
            return record.outputScopeName == outputScope && record.outputScope.valid();
        if(record.outputScopeName == outputScope)
            return false;
    }

    const auto output = m_timing.registerScope(outputScope);
    if(!output)
        return false;

    m_overlapRecords.emplace_back(m_arena);
    OverlapRecord& record = m_overlapRecords.back();
    record.firstScope = canonicalFirstScope;
    record.secondScope = canonicalSecondScope;
    record.outputScopeName = outputScope;
    record.outputScope = *output;
    return true;
}

b8 GpuOverlapMetricTracker::conflicts(const Span<const GpuPacketEnvelopeMetricScope> scopeInputs, const Name& queueOverlapScope, const Span<const GpuPacketEnvelopeMetricQueueOutput> queueOutputInputs)const noexcept{
    const auto* const scopes = scopeInputs.data();
    const usize scopeCount = scopeInputs.size();
    const auto* const queueOutputs = queueOutputInputs.data();
    const usize queueOutputCount = queueOutputInputs.size();
    for(const OverlapRecord& overlap : m_overlapRecords){
        for(usize scopeIndex = 0u; scopeIndex < scopeCount; ++scopeIndex){
            if(scopes[scopeIndex].scopeName == overlap.outputScopeName)
                return true;
        }
        if(queueOverlapScope == overlap.firstScope || queueOverlapScope == overlap.secondScope || queueOverlapScope == overlap.outputScopeName)
            return true;
        for(usize outputIndex = 0u; outputIndex < queueOutputCount; ++outputIndex){
            const Name& outputName = queueOutputs[outputIndex].internalIdleScopeName;
            if(outputName == overlap.firstScope || outputName == overlap.secondScope || outputName == overlap.outputScopeName)
                return true;
        }
    }
    return false;
}

void GpuOverlapMetricTracker::record(const Name& scopeName, const u64 frameIndex, const GpuComparableTimestampRange& range, const u64 retentionFrames, GpuTimingSinkSampleVector& performanceSamples){
    for(OverlapRecord& record : m_overlapRecords){
        if(scopeName != record.firstScope && scopeName != record.secondScope)
            continue;

        for(auto it = record.pendingFrames.begin(); it != record.pendingFrames.end(); ){
            if(frameIndex > it->frameIndex && frameIndex - it->frameIndex > retentionFrames)
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

        if(const auto overlapTicks = TryComputeGpuTimestampOverlap(frame->first, frame->second)){
            const f64 overlapSeconds = VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(*overlapTicks)), VectorDoubleReplicate(frame->first.secondsPerTick)));
            performanceSamples.reserve(performanceSamples.size() + 1u);
            performanceSamples.push_back(GpuTimingSinkSample{ .scope = record.outputScope, .durationSeconds = overlapSeconds, .sourceFrameIndex = frameIndex, });
        }
        for(auto it = record.pendingFrames.begin(); it != record.pendingFrames.end(); ++it){
            if(&*it == frame){
                record.pendingFrames.erase(it);
                break;
            }
        }
    }
}

b8 GpuOverlapMetricTracker::hasOutputRole(const Name& scopeName)const noexcept{
    for(const OverlapRecord& record : m_overlapRecords){
        if(record.outputScopeName == scopeName)
            return true;
    }
    return false;
}

void GpuOverlapMetricTracker::discardPendingRanges()noexcept{
    for(OverlapRecord& record : m_overlapRecords)
        record.pendingFrames.clear();
}

void GpuOverlapMetricTracker::reset()noexcept{
    m_overlapRecords.clear();
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

