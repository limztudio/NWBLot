// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CAUSTIC_PHOTON_PUSH_CONSTANTS_H
#define NWB_GRAPHICS_CAUSTIC_PHOTON_PUSH_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared CPU/GPU photon-producer ABI. Consumers provide FIELD(name, defaultValue) to materialize their native scalar
// declarations; keeping this header macro-only lets the C++ renderer and Slang shaders use one authoritative field order.
#define NWB_CAUSTIC_PHOTON_PUSH_CONSTANTS_FIELDS(FIELD) \
    FIELD(width, 0u) \
    FIELD(height, 0u) \
    FIELD(instanceCount, 0u) \
    FIELD(photonCount, 0u) \
    FIELD(emissionTargetCount, 0u) \
    FIELD(gridSide, 0u) \
    FIELD(frameIndex, 0u) \
    FIELD(depthSlot, 0u) \
    FIELD(worldPositionSlot, 0u) \
    FIELD(emissionTargetSlot, 0u) \
    FIELD(viewSlot, 0u) \
    FIELD(deferredResourcesHeapSlot, 0u) \
    FIELD(materialContextSlotsHeapSlot, 0u) \
    FIELD(accumulatorStorageSlot, 0u) \
    FIELD(temporalPhaseCount, 1u)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


