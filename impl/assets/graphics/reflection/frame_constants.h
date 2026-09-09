// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_REFLECTION_FRAME_CONSTANTS_H
#define NWB_GRAPHICS_REFLECTION_FRAME_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_REFLECTION_CLASSIFY_GROUP_SIZE 8
#define NWB_REFLECTION_TRACE_GROUP_SIZE 64
#define NWB_REFLECTION_MAX_GROUP_COUNT_X 65535u
#define NWB_REFLECTION_MODE_DISABLED 0u
#define NWB_REFLECTION_MODE_SCREEN 1u
#define NWB_REFLECTION_MODE_HARDWARE 2u
#define NWB_REFLECTION_MODE_HYBRID 3u

// Counter byte offsets. The candidate count drives bounded indirect work; the other counters describe executed work.
#define NWB_REFLECTION_COUNTER_CANDIDATES 0u
#define NWB_REFLECTION_COUNTER_HARDWARE_RAYS 4u
#define NWB_REFLECTION_COUNTER_HARDWARE_HITS 8u
#define NWB_REFLECTION_COUNTER_OPAQUE_PIXELS 12u
#define NWB_REFLECTION_COUNTER_GLASS_PIXELS 16u
#define NWB_REFLECTION_COUNTER_FALLBACK_PIXELS 20u
#define NWB_REFLECTION_COUNTER_SCREEN_ATTEMPTS 24u
#define NWB_REFLECTION_COUNTER_SCREEN_HITS 28u
#define NWB_REFLECTION_COUNTER_SIZE 32u

// Ten std140 lanes. Targets own their descriptors; this immutable frame payload only borrows their selectors.
#define NWB_REFLECTION_FRAME_UINT_FIELDS(FIELD) \
    FIELD(width, 0u) \
    FIELD(height, 0u) \
    FIELD(traceMode, 0u) \
    FIELD(hardwareEnabled, 0u) \
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
    FIELD(frameIndex, 0u) \
    FIELD(deferredResourcesSlot, 0u) \
    FIELD(viewSlot, 0u) \
    FIELD(materialContextSlot, 0u) \
    FIELD(debugView, 0u) \
    FIELD(depthPyramidSlot, 0u) \
    FIELD(depthMipCount, 0u) \
    FIELD(screenMaxSteps, 96u) \
    FIELD(_screenPad, 0u)

#define NWB_REFLECTION_FRAME_FLOAT_FIELDS(FIELD) \
    FIELD(maxRayDistance, 100.f) \
    FIELD(distanceFadeStart, 80.f) \
    FIELD(roughnessCutoff, 0.8f) \
    FIELD(_limitsPad, 0.f) \
    FIELD(environmentTopR, 0.15f) \
    FIELD(environmentTopG, 0.2f) \
    FIELD(environmentTopB, 0.3f) \
    FIELD(_topPad, 0.f) \
    FIELD(environmentBottomR, 0.04f) \
    FIELD(environmentBottomG, 0.04f) \
    FIELD(environmentBottomB, 0.04f) \
    FIELD(_bottomPad, 0.f) \
    FIELD(screenThickness, 0.03f) \
    FIELD(screenConfidenceThreshold, 0.8f) \
    FIELD(screenEdgeFade, 0.05f) \
    FIELD(_screenFloatPad, 0.f)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

