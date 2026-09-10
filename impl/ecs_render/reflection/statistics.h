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
// Layout is size-sorted (8-byte, then 4-byte, then 1-byte) to avoid padding.
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
    u32 hardwareHits = 0u;
    u32 opaquePixels = 0u;
    u32 glassPixels = 0u;
    u32 fallbackPixels = 0u;
    u32 screenAttempts = 0u;
    // Only screen hits meeting the confidence threshold are accepted.
    u32 screenHits = 0u;
    // Admitted paths and scene queries stay separate; bootstrap consumes query limits.
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
    u32 screenLimitMisses = 0u;
    ReflectionTraceMode::Enum traceMode = ReflectionTraceMode::Disabled;
    ReflectionHistoryResetReason::Enum historyResetReason = ReflectionHistoryResetReason::None;
    ReflectionFeedbackResetReason::Enum feedbackResetReason = ReflectionFeedbackResetReason::None;
    bool hardwareRequested = false;
    bool hardwareAvailable = false;
    bool hardwareReady = false;
    bool opticalTransportEnabled = false;
    bool historyEligible = false;
    bool historyReused = false;
    bool historyReset = false;
    bool feedbackRequested = false;
    bool feedbackEnabled = false;
    bool feedbackReused = false;
    bool feedbackReset = false;
    bool schedulingCounterValid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

