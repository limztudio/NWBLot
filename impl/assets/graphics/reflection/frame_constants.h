// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_REFLECTION_FRAME_CONSTANTS_H
#define NWB_GRAPHICS_REFLECTION_FRAME_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_REFLECTION_FEEDBACK_GROUP_SIZE 8u
#define NWB_REFLECTION_FEEDBACK_SURFACE_COUNT 2u
#define NWB_REFLECTION_FEEDBACK_HEADER_BYTES 16u
#define NWB_REFLECTION_FEEDBACK_ENTRY_BYTES 4u
#define NWB_REFLECTION_FEEDBACK_HEADER_VALID 1u
#define NWB_REFLECTION_FEEDBACK_WRITE_ENABLED 1u
#define NWB_REFLECTION_FEEDBACK_PREVIOUS_VALID 2u
#define NWB_REFLECTION_FEEDBACK_ACTIVE 0u
#define NWB_REFLECTION_FEEDBACK_DORMANT 2u
#define NWB_REFLECTION_FEEDBACK_PROBE_PERIOD 16u


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

// Eleven std140 lanes: targets own descriptors; this frame payload only borrows selectors.
#define NWB_REFLECTION_FRAME_RESOURCE_UINT_FIELDS(FIELD) \
    FIELD(opaqueSpecularSlot) \
    FIELD(glassSpecularSlot) \
    FIELD(opaqueOutputSlot) \
    FIELD(glassOutputSlot) \
    FIELD(opaqueRadianceSlot) \
    FIELD(glassRadianceSlot) \
    FIELD(queueSlot) \
    FIELD(counterSlot) \
    FIELD(argsSlot) \
    FIELD(queueCapacity) \
    FIELD(maxHardwareRays) \
    FIELD(sampleIndex) \
    FIELD(deferredResourcesSlot) \
    FIELD(viewSlot) \
    FIELD(materialContextSlot) \
    FIELD(debugView) \
    FIELD(depthPyramidSlot) \
    FIELD(depthMipCount) \
    FIELD(screenMaxSteps) \
    FIELD(diagnosticsEnabled) \
    FIELD(samplingSeed) \
    FIELD(sampleBaseX) \
    FIELD(sampleBaseY) \
    FIELD(maxOpticalQueries) \
    FIELD(feedbackReadSlot) \
    FIELD(feedbackWriteSlot) \
    FIELD(feedbackFlags) \
    FIELD(feedbackProbeIndex)

#define NWB_REFLECTION_FRAME_FLOAT_FIELDS(FIELD) \
    FIELD(maxRayDistance) \
    FIELD(distanceFadeStart) \
    FIELD(roughnessCutoff) \
    FIELD(environmentTopR) \
    FIELD(environmentTopG) \
    FIELD(environmentTopB) \
    FIELD(environmentBottomR) \
    FIELD(environmentBottomG) \
    FIELD(environmentBottomB) \
    FIELD(screenThickness) \
    FIELD(screenConfidenceThreshold) \
    FIELD(screenEdgeFade)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

