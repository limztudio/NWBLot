// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "settings.h"
#include "feedback.h"

#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ReflectionHistoryResetReason{
    enum Enum : u8{
        None,
        FirstSample,
        SceneChanged,
        ViewChanged,
        SettingsChanged,
        HardwareChanged,
        UntrustedScene,
        Disabled,
        ResourcesChanged,
    };
};

// A completed accepted reflection copy, regardless of later presentation outcome.
struct ReflectionStatistics{
    u64 sequence = 0u;
    u64 generation = 0u;
    u64 graphicsFrameIndex = 0u;
    u32 frameIndex = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 requestedHardwareBudget = 0u;
    u32 effectiveHardwareBudget = 0u;
    u32 queueCapacity = 0u;
    u32 maxOpticalQueries = 0u;
    ReflectionTraceMode::Enum traceMode = ReflectionTraceMode::Disabled;
    bool hardwareRequested = false;
    bool hardwareAvailable = false;
    bool hardwareReady = false;
    bool opticalTransportEnabled = false;
    u64 historyEpoch = 0u;
    u64 historyStartGraphicsFrame = 0u;
    u32 historySampleCount = 0u;
    u32 sampleIndex = 0u;
    u32 samplingSeed = 0u;
    bool historyEligible = false;
    bool historyReused = false;
    bool historyReset = false;
    ReflectionHistoryResetReason::Enum historyResetReason = ReflectionHistoryResetReason::None;
    // Enabled is the accepted complete writer's actual eligibility; reused is the CPU previous-valid flag.
    // GPU budget/header checks may still reject bypass, which is reported by the executed-work counters below.
    u64 feedbackSequence = 0u;
    u64 feedbackEpoch = 0u;
    u64 feedbackStartGraphicsFrame = 0u;
    u32 feedbackProbeIndex = 0u;
    bool feedbackRequested = false;
    bool feedbackEnabled = false;
    bool feedbackReused = false;
    bool feedbackReset = false;
    bool schedulingCounterValid = false;
    ReflectionFeedbackResetReason::Enum feedbackResetReason = ReflectionFeedbackResetReason::None;
    Core::QueueSubmissionToken acceptedToken;
    u32 candidates = 0u;
    u32 hardwareRays = 0u;
    u32 hardwareHits = 0u;
    u32 opaquePixels = 0u;
    u32 glassPixels = 0u;
    u32 fallbackPixels = 0u;
    u32 screenAttempts = 0u;
    // Only screen hits meeting the configured confidence threshold are accepted.
    u32 screenHits = 0u;
    // Admitted paths and actual scene queries remain separate; bootstrap and continuations consume query limits.
    u32 hardwareQueries = 0u;
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
    // Actual hierarchy loads, excluding projection rejection and bypass. May exceed u32 at large native extents.
    u64 screenIterations = 0u;
    u32 screenLimitMisses = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

