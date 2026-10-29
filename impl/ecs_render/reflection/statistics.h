// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/reflection/settings.adhd.h>
#include "feedback.h"

#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A completed accepted reflection copy, regardless of later presentation outcome.
struct ReflectionStatistics{
    u64 sequence = 0u;
    u64 generation = 0u;
    u64 graphicsFrameIndex = 0u;
    u64 historyEpoch = 0u;
    u64 historyStartGraphicsFrame = 0u;
    // Enabled is writer eligibility; reused is the CPU previous-valid flag.
    u64 feedbackSequence = 0u;
    u64 feedbackEpoch = 0u;
    u64 feedbackStartGraphicsFrame = 0u;
    // Actual hierarchy loads; may exceed u32 at large extents.
    u64 screenIterations = 0u;
    Core::QueueSubmissionToken acceptedToken;
    u32 frameIndex = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 requestedHardwareBudget = 0u;
    u32 effectiveHardwareBudget = 0u;
    u32 queueCapacity = 0u;
    u32 maxOpticalQueries = 0u;
    u32 historySampleCount = 0u;
    u32 sampleIndex = 0u;
    u32 samplingSeed = 0u;
    u32 feedbackProbeIndex = 0u;
    u32 candidates = 0u;
    u32 hardwareRays = 0u;
    // Admitted rays conservatively proven to need no optical continuation; diagnostic only.
    u32 exteriorEligibleRays = 0u;
    u32 hardwareHits = 0u;
    u32 opaquePixels = 0u;
    u32 glassPixels = 0u;
    u32 fallbackPixels = 0u;
    u32 screenAttempts = 0u;
    // Only screen hits meeting the confidence threshold are accepted.
    u32 screenHits = 0u;
    // Reserved traversal units enforce the path limit; physical queries count issued hardware operations.
    u32 queryBudgetUnits = 0u;
    u32 physicalQueries = 0u;
    u32 bootstrapEvents = 0u;
    u32 transparentPaths = 0u;
    u32 unsupportedPaths = 0u;
    u32 limitedPaths = 0u;
    u32 ambiguousPaths = 0u;
    u32 tirEvents = 0u;
    u32 mediumOverflowPaths = 0u;
    u32 potentialReceivers = 0u;
    u32 screenReturns = 0u;
    u32 feedbackBypassedPixels = 0u;
    u32 feedbackProbeTiles = 0u;
    u32 screenLimitMisses = 0u;
    ReflectionTraceMode::Enum traceMode = ReflectionTraceMode::Disabled;
    ReflectionHistoryResetReason::Enum historyResetReason = ReflectionHistoryResetReason::None;
    ReflectionFeedbackResetReason::Enum feedbackResetReason = ReflectionFeedbackResetReason::None;
    b8 hardwareRequested = false;
    b8 hardwareAvailable = false;
    b8 hardwareReady = false;
    b8 csgHardwareEnabled = false;
    b8 opticalTransportEnabled = false;
    b8 historyEligible = false;
    b8 historyReused = false;
    b8 historyReset = false;
    b8 feedbackRequested = false;
    b8 feedbackEnabled = false;
    b8 feedbackReused = false;
    b8 feedbackReset = false;
    b8 schedulingCounterValid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

