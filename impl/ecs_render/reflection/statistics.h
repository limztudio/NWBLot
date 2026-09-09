// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "settings.h"

#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A completed accepted reflection copy, independently of whether later presentation work succeeded.
struct ReflectionStatistics{
    u64 sequence = 0u;
    u64 generation = 0u;
    u32 frameIndex = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 requestedHardwareBudget = 0u;
    u32 effectiveHardwareBudget = 0u;
    u32 queueCapacity = 0u;
    ReflectionTraceMode::Enum traceMode = ReflectionTraceMode::Disabled;
    bool hardwareRequested = false;
    bool hardwareAvailable = false;
    bool hardwareReady = false;
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

