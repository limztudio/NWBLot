// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#include "gpu_timing.h"

#include "backend_selection.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTimingAccumulator::collect(
    Device& device,
    GpuTimingRecorder& recorder,
    const u32 epoch,
    const u64 subscriptionIdentityLimit,
    const bool publishPerformanceSamples,
    SampleDispatchVector& completedSamples,
    Alloc::ScratchArena& scratchArena
){
    if(!m_enabled)
        return;

    for(QueryRecord& record : m_queries){
        if(
            (record.state != QueryState::PendingAccepted && record.state != QueryState::PendingRetirementAccepted)
            || !record.query
        )
            continue;
        if(record.retirementNotificationPending)
            continue;
        if(
            !record.acceptedSubmission.valid()
            || !record.acceptedSubmission.hasPhysicalQueueIdentity()
            || record.acceptedSubmission.queue != record.queueClass
            || !record.acceptedSubmission.matchesPhysicalQueue(
                record.physicalQueue.index,
                record.physicalQueue.deviceGeneration
            )
            || !device.matchesPhysicalQueueIdentity(
                record.acceptedSubmission.queue,
                record.acceptedSubmission.physicalQueueIndex,
                record.acceptedSubmission.deviceGeneration
            )
        ){
            const bool retirementPending = quarantineRecord(record, subscriptionIdentityLimit);
            if(retirementPending){
                completedSamples.push_back(SampleDispatch{
                    .sample = GpuTimingSample{
                        .scopeName = m_scopeName,
                        .sourceFrameIndex = record.frameIndex,
                        .physicalQueue = record.physicalQueue,
                        .attribution = record.attribution,
                        .comparableRange = {},
                    },
                    .subscriptionIdentityLimit = record.retirementSubscriptionIdentityLimit,
                });
                record.attribution = s_NoGpuTimingSampleAttribution;
                record.retirementSubscriptionIdentityLimit = 0u;
                record.retirementNotificationPending = false;
            }
            NWB_LOGGER_ERROR(NWB_TEXT("GPU timing query quarantined after its accepted submission lost exact queue identity"));
            continue;
        }
        if(!recorder.submissionCompleted(device, record.acceptedSubmission))
            continue;

        if(record.state == QueryState::PendingRetirementAccepted){
            ++m_unpublishedSampleCount;
            if(record.attribution != s_NoGpuTimingSampleAttribution){
                completedSamples.push_back(SampleDispatch{
                    .sample = GpuTimingSample{
                        .scopeName = m_scopeName,
                        .sourceFrameIndex = record.frameIndex,
                        .physicalQueue = record.physicalQueue,
                        .attribution = record.attribution,
                        .published = false,
                        .comparableRange = {},
                    },
                    .subscriptionIdentityLimit = subscriptionIdentityLimit,
                });
            }
            releaseQuery(record);
            continue;
        }

        TimerQueryResult result;
        if(!device.getTimerQueryResult(record.query.get(), result))
            continue;

        const bool publishSample = record.epoch == epoch && record.publishSample;
        const f64 durationSeconds = result.durationSeconds();
        GpuComparableTimestampRange comparableRange;
        if(publishSample){
            if(result.hasComparableRange()){
                comparableRange = GpuComparableTimestampRange{
                    result.beginTicks,
                    result.endTicks,
                    result.secondsPerTick,
                    result.physicalQueue,
                };
            }
        }
        if(record.attribution != s_NoGpuTimingSampleAttribution){
            completedSamples.push_back(SampleDispatch{
                .sample = GpuTimingSample{
                    .scopeName = m_scopeName,
                    .sourceFrameIndex = record.frameIndex,
                    .durationSeconds = publishSample ? durationSeconds : 0.0,
                    .physicalQueue = record.physicalQueue,
                    .attribution = record.attribution,
                    .published = publishSample,
                    .comparableRange = comparableRange,
                },
                .subscriptionIdentityLimit = subscriptionIdentityLimit,
            });
        }
        releaseQuery(record);

        if(publishSample)
            ++m_publishedSampleCount;
        else
            ++m_unpublishedSampleCount;
        if(!publishSample || !publishPerformanceSamples)
            continue;

        recorder.m_timing.recordSample(m_timingScope, durationSeconds, record.frameIndex);
        if(comparableRange.valid()){
            recorder.m_metricCorrelator.recordTimestampRange(
                m_scopeName,
                record.frameIndex,
                comparableRange,
                scratchArena
            );
        }
    }
}

void GpuTimingAccumulator::recordFrameReset(CommandList& commandList){
    if(!m_enabled)
        return;

    const bool canReset = commandList.canResetTimerQueryHere();
    const CommandListParameters commandListDescription = commandList.getResolvedDescription();
    // Retain pending pools until their result is observable instead of erasing a late GPU sample. Pools that are
    // already available this frame are reset on the device timeline, but do not become usable by dynamic-rendering
    // scopes until the caller confirms this command list submitted successfully.
    for(QueryRecord& record : m_queries){
        record.frameResetRecorded = false;
        record.frameResetRecordingQueue = {};
        // A newly recorded frame preamble supersedes any dependency retained from a rejected scope in an older
        // frame. Outside-render-pass scopes reset inline and do not need that stale dependency if this preamble
        // fails; render-pass scopes remain unavailable until the new reset submission is accepted below.
        if(record.state == QueryState::Available)
            record.frameResetSubmission = {};
        // A render-pass scope must observe this frame's reset, not merely a reset that happened during an earlier
        // frame. Leave the pool unavailable until confirmFrameReset() observes a successful preamble submission.
        record.deviceReady = false;
        if(!canReset || !record.query || record.state != QueryState::Available)
            continue;

        if(!commandList.resetTimerQuery(record.query.get()))
            continue;
        record.frameResetRecorded = true;
        record.frameResetRecordingQueue = commandListDescription.physicalQueue;
    }
}

void GpuTimingAccumulator::confirmFrameReset(const QueueSubmissionToken& token){
    for(QueryRecord& record : m_queries){
        if(!record.frameResetRecorded)
            continue;

        record.frameResetRecorded = false;
        const bool matchesResetQueue = token.valid()
            && token.hasPhysicalQueueIdentity()
            && token.matchesPhysicalQueue(
                record.frameResetRecordingQueue.index,
                record.frameResetRecordingQueue.deviceGeneration
            )
        ;
        record.frameResetRecordingQueue = {};
        if(m_enabled && record.state == QueryState::Available && matchesResetQueue){
            record.frameResetSubmission = token;
            record.deviceReady = true;
        }
        else if(!matchesResetQueue)
            NWB_LOGGER_ERROR(NWB_TEXT("GPU timing frame reset rejected an accepted token from a different physical queue"));
    }
}

void GpuTimingAccumulator::discardFrameReset()noexcept{
    for(QueryRecord& record : m_queries){
        record.frameResetRecorded = false;
        record.frameResetRecordingQueue = {};
        // A failed (or not-yet-recorded) preamble must not let a dynamic-rendering scope reuse a previous frame's
        // reset. Outside a render pass beginTimerQuery() still performs its own device-timeline reset.
        record.deviceReady = false;
    }
}

void GpuTimingAccumulator::requestQueries(const u32 queryCount){
    m_requestedQueryCount = Max(m_requestedQueryCount, queryCount);
}

bool GpuTimingAccumulator::materializeRequestedQueries(Device& device){
    return m_requestedQueryCount == 0u || reserveQueries(device, m_requestedQueryCount);
}

bool GpuTimingAccumulator::beginQuery(
    CommandList& commandList,
    const u64 frameIndex,
    const u32 epoch,
    const GpuTimingSampleAttribution attribution,
    GpuTimingScope& outScope,
    QueueSubmissionToken& outResetSubmission
){
    outScope = {};
    outResetSubmission = {};
    if(!m_enabled){
        ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::CollectionInactive];
        return true;
    }

    const u32 index = findAvailableQuery();
    if(index == Limit<u32>::s_Max){
        ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::QueryCapacityUnavailable];
        return true;
    }

    QueryRecord& record = m_queries[index];
    // beginTimerQuery self-resets an already prepared pool when recording outside a render pass. Inside a render pass
    // that reset is illegal, so only pools that recordFrameReset() made deviceReady are eligible. Under-reserved or
    // undeclared scopes skip their sample instead of allocating persistent query pools from a recording path.
    if(!commandList.isRecording() || !commandList.hasCommandBuffer() || commandList.commandRecordingFailed())
        return false;
    if(!commandList.canRecordTimerQueryHere()){
        ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::RecordingPositionUnavailable];
        return true;
    }
    if(commandList.isRenderPassActive()){
        if(!record.deviceReady){
            ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::RecordingPositionUnavailable];
            return true;
        }
    }
    else if(!commandList.canResetTimerQueryHere()){
        ++m_skippedScopeCountByReason[GpuTimingScopeSkipReason::RecordingPositionUnavailable];
        return true;
    }

    // A device-timeline reset authorizes exactly one timestamp pair. Consume it as soon as the reservation records
    // its begin endpoint, even if that command buffer is later discarded before submission.
    TimerQueryRecordingToken timerQueryRecording;
    if(!commandList.beginTimerQuery(record.query.get(), timerQueryRecording))
        return false;

    const CommandListParameters commandListDescription = commandList.getResolvedDescription();
    record.physicalQueue = commandListDescription.physicalQueue;
    record.acceptedSubmission = {};
    record.queueClass = commandListDescription.queueType;
    record.state = QueryState::Recording;
    record.publishSample = true;
    record.frameResetRecorded = false;
    record.deviceReady = false;
    record.frameIndex = frameIndex;
    record.attribution = attribution;
    record.epoch = epoch;
    record.retirementNotificationPending = false;
    outResetSubmission = record.frameResetSubmission;
    ++m_nextReservation;
    if(m_nextReservation == 0u)
        ++m_nextReservation;
    record.reservation = m_nextReservation;
    outScope = GpuTimingScope{
        .scopeName = m_scopeName,
        .index = index,
        .epoch = epoch,
        .reservation = record.reservation,
        .timerQueryRecording = timerQueryRecording,
    };
    ++m_recordedScopeCount;
    return true;
}

GpuTimingAccumulator::QueryEndResult GpuTimingAccumulator::endQuery(
    CommandList& commandList,
    const GpuTimingScope& scope
){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return QueryEndResult::Invalid;

    QueryRecord& record = m_queries[scope.index];
    if(
        record.epoch != scope.epoch
        || record.reservation != scope.reservation
        || record.state != QueryState::Recording
    )
        return QueryEndResult::Invalid;
    if(!commandList.endTimerQuery(record.query.get(), scope.timerQueryRecording)){
        record.state = QueryState::EndFailedUnaccepted;
        record.publishSample = false;
        NWB_LOGGER_ERROR(NWB_TEXT("GPU timing end timestamp failed; retaining ownership until submission resolution"));
        return QueryEndResult::RetirementRequired;
    }
    record.state = QueryState::EndedUnaccepted;
    return QueryEndResult::Ended;
}

bool GpuTimingAccumulator::recordQueryEnd(CommandList& commandList, const GpuTimingScope& scope){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(
        record.epoch != scope.epoch
        || record.reservation != scope.reservation
        || record.state != QueryState::Recording
    )
        return false;

    if(!commandList.endTimerQuery(record.query.get(), scope.timerQueryRecording))
        return false;

    record.state = QueryState::EndedUnaccepted;
    return true;
}

bool GpuTimingAccumulator::validateQuerySubmission(
    const GpuTimingScope& scope,
    const QueueSubmissionToken& token
)const{
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    const QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return false;
    if(
        record.state != QueryState::Recording
        && record.state != QueryState::EndedUnaccepted
        && record.state != QueryState::EndFailedUnaccepted
    )
        return false;

    return token.valid()
        && token.hasPhysicalQueueIdentity()
        && token.queue == record.queueClass
        && token.matchesPhysicalQueue(record.physicalQueue.index, record.physicalQueue.deviceGeneration)
    ;
}

bool GpuTimingAccumulator::confirmQuery(
    const GpuTimingScope& scope,
    const QueueSubmissionToken& token,
    const bool publishSample
){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return false;
    if(!validateQuerySubmission(scope, token))
        return false;

    record.acceptedSubmission = token;
    if(record.state == QueryState::EndedUnaccepted){
        record.state = QueryState::PendingAccepted;
        record.publishSample = publishSample;
    }
    else if(record.state == QueryState::EndFailedUnaccepted){
        record.state = QueryState::PendingRetirementAccepted;
        record.publishSample = false;
    }
    else
        return false;
    ++m_acceptedScopeCount;
    return true;
}

bool GpuTimingAccumulator::retireQuery(const GpuTimingScope& scope, const QueueSubmissionToken& token){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(
        record.epoch != scope.epoch
        || record.reservation != scope.reservation
        || record.state != QueryState::Recording
        || !validateQuerySubmission(scope, token)
    )
        return false;

    record.acceptedSubmission = token;
    record.state = QueryState::PendingRetirementAccepted;
    record.publishSample = false;
    ++m_acceptedScopeCount;
    return true;
}

bool GpuTimingAccumulator::prepareQueryForRecovery(const GpuTimingScope& scope){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return false;
    if(record.state != QueryState::EndedUnaccepted)
        return false;

    record.state = QueryState::Recording;
    return true;
}

bool GpuTimingAccumulator::discardQuery(
    const GpuTimingScope& scope,
    const u64 subscriptionIdentityLimit
){
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return false;

    if(record.state == QueryState::PendingAccepted || record.state == QueryState::PendingRetirementAccepted){
        const bool retirementPending = quarantineQuery(scope, subscriptionIdentityLimit);
        NWB_LOGGER_ERROR(NWB_TEXT("GPU timing query quarantined because accepted work attempted rollback release"));
        return retirementPending;
    }
    if(record.state == QueryState::Quarantined)
        return quarantineQuery(scope, subscriptionIdentityLimit);

    if(!record.query || !record.query->discardUnacceptedRecording(scope.timerQueryRecording)){
        const bool retirementPending = quarantineQuery(scope, subscriptionIdentityLimit);
        NWB_LOGGER_ERROR(NWB_TEXT("GPU timing query quarantined because its unaccepted native recording could not be revoked"));
        return retirementPending;
    }

    releaseUnacceptedQuery(record);
    ++m_discardedScopeCount;
    return false;
}

bool GpuTimingAccumulator::quarantineQuery(
    const GpuTimingScope& scope,
    const u64 subscriptionIdentityLimit
)noexcept{
    if(!scope.valid() || scope.scopeName != m_scopeName || scope.index >= m_queries.size())
        return false;

    QueryRecord& record = m_queries[scope.index];
    if(record.epoch != scope.epoch || record.reservation != scope.reservation)
        return false;
    return quarantineRecord(record, subscriptionIdentityLimit);
}

bool GpuTimingAccumulator::quarantineRecord(
    QueryRecord& record,
    const u64 subscriptionIdentityLimit
)noexcept{
    if(record.state != QueryState::Quarantined){
        record.state = QueryState::Quarantined;
        record.publishSample = false;
        record.frameResetRecorded = false;
        record.deviceReady = false;
        ++m_quarantinedScopeCount;
    }

    if(record.attribution != s_NoGpuTimingSampleAttribution && !record.retirementNotificationPending){
        record.retirementNotificationPending = true;
        record.retirementSubscriptionIdentityLimit = subscriptionIdentityLimit;
    }
    return record.retirementNotificationPending;
}

bool GpuTimingAccumulator::reserveQueries(Device& device, const u32 queryCount){
    while(m_queries.size() < static_cast<usize>(queryCount)){
        if(appendQuery(device) == Limit<u32>::s_Max)
            return false;
    }
    return true;
}

u32 GpuTimingAccumulator::findAvailableQuery()const{
    for(usize i = 0u; i < m_queries.size(); ++i){
        if(m_queries[i].state == QueryState::Available && !m_queries[i].retirementNotificationPending)
            return static_cast<u32>(i);
    }

    return Limit<u32>::s_Max;
}

u32 GpuTimingAccumulator::appendQuery(Device& device){
    QueryRecord record;
    record.query = device.createTimerQuery();
    if(!record.query)
        return Limit<u32>::s_Max;

    m_queries.push_back(Move(record));
    return static_cast<u32>(m_queries.size() - 1u);
}

void GpuTimingAccumulator::releaseQuery(QueryRecord& record){
    // A discarded command buffer may already contain timestamp writes. Require another accepted preamble reset
    // before a dynamic-rendering scope reuses this pool; outside a render pass beginTimerQuery() resets it itself.
    const GpuPhysicalQueueId retirementPhysicalQueue = record.physicalQueue;
    const u64 retirementFrameIndex = record.frameIndex;
    const GpuTimingSampleAttribution retirementAttribution = record.attribution;
    const u64 retirementSubscriptionIdentityLimit = record.retirementSubscriptionIdentityLimit;
    const bool retirementNotificationPending = record.retirementNotificationPending;

    record.physicalQueue = {};
    record.acceptedSubmission = {};
    record.frameResetSubmission = {};
    record.frameResetRecordingQueue = {};
    record.frameIndex = 0u;
    record.epoch = 0u;
    record.reservation = 0u;
    record.retirementSubscriptionIdentityLimit = 0u;
    record.queueClass = CommandQueue::kCount;
    record.state = QueryState::Available;
    record.publishSample = true;
    record.attribution = s_NoGpuTimingSampleAttribution;
    record.frameResetRecorded = false;
    record.deviceReady = false;
    record.retirementNotificationPending = false;

    // An unsubscribe may mark an unaccepted scope and stream its notification after releasing the recorder lock.
    // Preserve that payload in the now-available pool, and keep findAvailableQuery() from reusing it until drained.
    if(retirementNotificationPending){
        NWB_ASSERT(retirementAttribution != s_NoGpuTimingSampleAttribution);
        record.physicalQueue = retirementPhysicalQueue;
        record.frameIndex = retirementFrameIndex;
        record.attribution = retirementAttribution;
        record.retirementSubscriptionIdentityLimit = retirementSubscriptionIdentityLimit;
        record.retirementNotificationPending = true;
    }
}

void GpuTimingAccumulator::releaseUnacceptedQuery(QueryRecord& record){
    // Never restore deviceReady: even though the timing ticket rejected this submission, the recorded command
    // buffer still contains query writes and must not authorize a render-pass retry without a fresh preamble. Keep
    // the accepted reset token only so an outside-render-pass retry on another queue remains ordered after it.
    const QueueSubmissionToken frameResetSubmission = record.frameResetSubmission;
    releaseQuery(record);
    record.frameResetSubmission = frameResetSubmission;
}

usize GpuTimingAccumulator::pendingAttributionCount()const noexcept{
    usize result = 0u;
    for(const QueryRecord& record : m_queries){
        if(record.attribution != s_NoGpuTimingSampleAttribution)
            ++result;
    }
    return result;
}

void GpuTimingAccumulator::markAttributionsForRetirement(const u64 subscriptionIdentityLimit)noexcept{
    for(QueryRecord& record : m_queries){
        if(record.attribution != s_NoGpuTimingSampleAttribution && !record.retirementNotificationPending){
            record.retirementNotificationPending = true;
            record.retirementSubscriptionIdentityLimit = subscriptionIdentityLimit;
        }
    }
}

bool GpuTimingAccumulator::retireMarkedAttribution(SampleDispatch& outDispatch)noexcept{
    for(QueryRecord& record : m_queries){
        if(!record.retirementNotificationPending || record.attribution == s_NoGpuTimingSampleAttribution)
            continue;

        outDispatch = SampleDispatch{
            .sample = GpuTimingSample{
                .scopeName = m_scopeName,
                .sourceFrameIndex = record.frameIndex,
                .physicalQueue = record.physicalQueue,
                .attribution = record.attribution,
                .comparableRange = {},
            },
            .subscriptionIdentityLimit = record.retirementSubscriptionIdentityLimit,
        };
        record.attribution = s_NoGpuTimingSampleAttribution;
        record.retirementSubscriptionIdentityLimit = 0u;
        record.retirementNotificationPending = false;
        if(record.state == QueryState::Available){
            record.physicalQueue = {};
            record.frameIndex = 0u;
        }
        return true;
    }
    return false;
}

void GpuTimingAccumulator::discardMarkedAttributions()noexcept{
    for(QueryRecord& record : m_queries){
        if(!record.retirementNotificationPending)
            continue;

        record.attribution = s_NoGpuTimingSampleAttribution;
        record.retirementSubscriptionIdentityLimit = 0u;
        record.retirementNotificationPending = false;
        if(record.state == QueryState::Available){
            record.physicalQueue = {};
            record.frameIndex = 0u;
        }
    }
}

void GpuTimingAccumulator::retireAttributions(
    SampleDispatchVector& outSamples,
    const u64 subscriptionIdentityLimit
){
    for(QueryRecord& record : m_queries){
        if(record.attribution == s_NoGpuTimingSampleAttribution)
            continue;

        outSamples.push_back(SampleDispatch{
            .sample = GpuTimingSample{
                .scopeName = m_scopeName,
                .sourceFrameIndex = record.frameIndex,
                .physicalQueue = record.physicalQueue,
                .attribution = record.attribution,
                .comparableRange = {},
            },
            .subscriptionIdentityLimit = record.retirementNotificationPending
                ? record.retirementSubscriptionIdentityLimit
                : subscriptionIdentityLimit
            ,
        });
        record.attribution = s_NoGpuTimingSampleAttribution;
        record.retirementSubscriptionIdentityLimit = 0u;
        record.retirementNotificationPending = false;
        if(record.state == QueryState::Available){
            record.physicalQueue = {};
            record.frameIndex = 0u;
        }
    }
}

void GpuTimingAccumulator::appendStatistics(GpuTimingRecorderStatistics& outStatistics)const noexcept{
    outStatistics.requestedQueryCount += static_cast<u64>(m_requestedQueryCount);
    outStatistics.materializedQueryCount += static_cast<u64>(m_queries.size());
    outStatistics.recordedScopeCount += m_recordedScopeCount;
    outStatistics.acceptedScopeCount += m_acceptedScopeCount;
    outStatistics.publishedSampleCount += m_publishedSampleCount;
    outStatistics.unpublishedSampleCount += m_unpublishedSampleCount;
    outStatistics.discardedScopeCount += m_discardedScopeCount;
    outStatistics.quarantinedScopeCount += m_quarantinedScopeCount;
    for(u8 reason = 0u; reason < GpuTimingScopeSkipReason::kCount; ++reason)
        outStatistics.skippedScopeCountByReason[reason] += m_skippedScopeCountByReason[reason];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

