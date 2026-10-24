// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_REFLECTION_FRAME_CONSTANTS_H
#define NWB_GRAPHICS_REFLECTION_FRAME_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../raytrace/optical_transport_constants.h"
#include "feedback_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_REFLECTION_CLASSIFY_GROUP_SIZE 8
#define NWB_REFLECTION_TRACE_GROUP_SIZE 64
#define NWB_REFLECTION_MAX_GROUP_COUNT_X 65535u
// Bound each retained-CSG dispatch without changing the complete frame ray budget.
#define NWB_REFLECTION_TRACE_SLICE_RAYS 8192u
#define NWB_REFLECTION_INDIRECT_ARGUMENT_BYTES 12u
#define NWB_REFLECTION_QUEUE_ENTRY_BYTES 4u
#define NWB_REFLECTION_CSG_PUSH_CONSTANT_BYTES 8u
#define NWB_REFLECTION_CSG_PUSH_UINT_FIELDS(FIELD) \
    FIELD(frameParametersSlot) \
    FIELD(rayOffset)
#define NWB_REFLECTION_MODE_DISABLED 0u
#define NWB_REFLECTION_MODE_SCREEN 1u
#define NWB_REFLECTION_MODE_HARDWARE 2u
#define NWB_REFLECTION_MODE_HYBRID 3u

// Debug view values match ReflectionDebugView in C++.
#define NWB_REFLECTION_DEBUG_VIEW_TRACE_SOURCE 1u
#define NWB_REFLECTION_DEBUG_VIEW_CONFIDENCE 2u

// Stored source alpha: fallback, screen trace plus scaled confidence, or a hardware hit.
#define NWB_REFLECTION_SOURCE_FALLBACK 1.0
#define NWB_REFLECTION_SOURCE_SCREEN 2.0
#define NWB_REFLECTION_SOURCE_HARDWARE 3.0
#define NWB_REFLECTION_SOURCE_CONFIDENCE_ENCODE_SCALE 0.5
#define NWB_REFLECTION_SOURCE_CONFIDENCE_DECODE_SCALE 2.0

// FP16 raster positions carry at most this relative error.
#define NWB_REFLECTION_POSITION_RELATIVE_ERROR 0.0009765625

// Counter byte offsets: candidates and the feedback receiver bound drive scheduling.
#define NWB_REFLECTION_COUNTER_CANDIDATES 0u
#define NWB_REFLECTION_COUNTER_HARDWARE_RAYS 4u
#define NWB_REFLECTION_COUNTER_HARDWARE_HITS 8u
#define NWB_REFLECTION_COUNTER_OPAQUE_PIXELS 12u
#define NWB_REFLECTION_COUNTER_GLASS_PIXELS 16u
#define NWB_REFLECTION_COUNTER_FALLBACK_PIXELS 20u
#define NWB_REFLECTION_COUNTER_SCREEN_ATTEMPTS 24u
#define NWB_REFLECTION_COUNTER_SCREEN_HITS 28u
#define NWB_REFLECTION_COUNTER_QUERY_BUDGET_UNITS 32u
#define NWB_REFLECTION_COUNTER_BOOTSTRAP_EVENTS 36u
#define NWB_REFLECTION_COUNTER_TRANSPARENT_PATHS 40u
#define NWB_REFLECTION_COUNTER_UNSUPPORTED_PATHS 44u
#define NWB_REFLECTION_COUNTER_LIMITED_PATHS 48u
#define NWB_REFLECTION_COUNTER_AMBIGUOUS_PATHS 52u
#define NWB_REFLECTION_COUNTER_TIR_EVENTS 56u
#define NWB_REFLECTION_COUNTER_MEDIUM_OVERFLOW_PATHS 60u
#define NWB_REFLECTION_COUNTER_POTENTIAL_RECEIVERS 64u
#define NWB_REFLECTION_COUNTER_SCREEN_RETURNS 68u
#define NWB_REFLECTION_COUNTER_FEEDBACK_BYPASSED_PIXELS 72u
#define NWB_REFLECTION_COUNTER_FEEDBACK_PROBE_TILES 76u
#define NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_LOW 80u
#define NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_HIGH 84u
#define NWB_REFLECTION_COUNTER_SCREEN_LIMIT_MISSES 88u
#define NWB_REFLECTION_COUNTER_EXTERIOR_ELIGIBLE_RAYS 92u
#define NWB_REFLECTION_COUNTER_PHYSICAL_QUERIES 96u
#define NWB_REFLECTION_COUNTER_SIZE 100u

// Adding this wrapped value subtracts one from a counter.
#define NWB_REFLECTION_COUNTER_SUBTRACT_ONE 0xffffffffu

#define NWB_REFLECTION_DEFAULT_SCREEN_MAX_STEPS 96u
#define NWB_REFLECTION_DEFAULT_MAX_RAY_DISTANCE 100.f
#define NWB_REFLECTION_DEFAULT_DISTANCE_FADE_START 80.f
#define NWB_REFLECTION_DEFAULT_ROUGHNESS_CUTOFF 0.8f
#define NWB_REFLECTION_DEFAULT_ENV_TOP_R 0.15f
#define NWB_REFLECTION_DEFAULT_ENV_TOP_G 0.2f
#define NWB_REFLECTION_DEFAULT_ENV_TOP_B 0.3f
#define NWB_REFLECTION_DEFAULT_ENV_BOTTOM_RGB 0.04f
#define NWB_REFLECTION_DEFAULT_SCREEN_THICKNESS 0.03f
#define NWB_REFLECTION_DEFAULT_SCREEN_CONFIDENCE_THRESHOLD 0.8f
#define NWB_REFLECTION_DEFAULT_SCREEN_EDGE_FADE 0.05f

// Eleven std140 lanes: targets own descriptors; this frame payload only borrows selectors.
#define NWB_REFLECTION_FRAME_ROUTE_UINT_FIELDS(FIELD) \
    FIELD(width, 0u) \
    FIELD(height, 0u) \
    FIELD(traceMode, 0u) \
    FIELD(hardwareEnabled, 0u)

#define NWB_REFLECTION_FRAME_RESOURCE_UINT_FIELDS(FIELD) \
    FIELD(opaqueSpecularSlot, 0u) \
    FIELD(glassSpecularSlot, 0u) \
    FIELD(opaqueOutputSlot, 0u) \
    FIELD(glassOutputSlot, 0u) \
    FIELD(opaqueRadianceSlot, 0u) \
    FIELD(glassRadianceSlot, 0u) \
    FIELD(queueSlot, 0u) \
    FIELD(counterSlot, 0u) \
    FIELD(argsSlot, 0u) \
    FIELD(queueCapacity, 0u) \
    FIELD(maxHardwareRays, 0u) \
    FIELD(sampleIndex, 0u) \
    FIELD(deferredResourcesSlot, 0u) \
    FIELD(viewSlot, 0u) \
    FIELD(materialContextSlot, 0u) \
    FIELD(debugView, 0u) \
    FIELD(depthPyramidSlot, 0u) \
    FIELD(depthMipCount, 0u) \
    FIELD(screenMaxSteps, NWB_REFLECTION_DEFAULT_SCREEN_MAX_STEPS) \
    FIELD(diagnosticsEnabled, 0u) \
    FIELD(samplingSeed, 0u) \
    FIELD(sampleBaseX, 0u) \
    FIELD(sampleBaseY, 0u) \
    FIELD(maxOpticalQueries, NWB_OPTICAL_DEFAULT_QUERIES) \
    FIELD(feedbackReadSlot, 0u) \
    FIELD(feedbackWriteSlot, 0u) \
    FIELD(feedbackFlags, 0u) \
    FIELD(feedbackProbeIndex, 0u)

#define NWB_REFLECTION_FRAME_UINT_FIELDS(FIELD) \
    NWB_REFLECTION_FRAME_ROUTE_UINT_FIELDS(FIELD) \
    NWB_REFLECTION_FRAME_RESOURCE_UINT_FIELDS(FIELD)

#define NWB_REFLECTION_FRAME_FLOAT_FIELDS(FIELD) \
    FIELD(maxRayDistance, NWB_REFLECTION_DEFAULT_MAX_RAY_DISTANCE) \
    FIELD(distanceFadeStart, NWB_REFLECTION_DEFAULT_DISTANCE_FADE_START) \
    FIELD(roughnessCutoff, NWB_REFLECTION_DEFAULT_ROUGHNESS_CUTOFF) \
    FIELD(environmentTopR, NWB_REFLECTION_DEFAULT_ENV_TOP_R) \
    FIELD(environmentTopG, NWB_REFLECTION_DEFAULT_ENV_TOP_G) \
    FIELD(environmentTopB, NWB_REFLECTION_DEFAULT_ENV_TOP_B) \
    FIELD(environmentBottomR, NWB_REFLECTION_DEFAULT_ENV_BOTTOM_RGB) \
    FIELD(environmentBottomG, NWB_REFLECTION_DEFAULT_ENV_BOTTOM_RGB) \
    FIELD(environmentBottomB, NWB_REFLECTION_DEFAULT_ENV_BOTTOM_RGB) \
    FIELD(screenThickness, NWB_REFLECTION_DEFAULT_SCREEN_THICKNESS) \
    FIELD(screenConfidenceThreshold, NWB_REFLECTION_DEFAULT_SCREEN_CONFIDENCE_THRESHOLD) \
    FIELD(screenEdgeFade, NWB_REFLECTION_DEFAULT_SCREEN_EDGE_FADE)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

