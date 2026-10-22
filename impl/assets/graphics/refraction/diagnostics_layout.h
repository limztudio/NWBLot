// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_REFRACTION_DIAGNOSTICS_LAYOUT_H
#define NWB_GRAPHICS_REFRACTION_DIAGNOSTICS_LAYOUT_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE 128u
#define NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(FIELD) \
    FIELD(hardwarePixels, 0u) \
    FIELD(entryQueries, 4u) \
    FIELD(entryAssociationMisses, 8u) \
    FIELD(entryWrongEntities, 12u) \
    FIELD(entryFailures, 16u) \
    FIELD(associatedEntries, 20u) \
    FIELD(invalidEntryDirections, 24u) \
    FIELD(exitQueries, 28u) \
    FIELD(exitMisses, 32u) \
    FIELD(exitFailures, 36u) \
    FIELD(tirEvents, 40u) \
    FIELD(tirCappedPixels, 44u) \
    FIELD(volumeExits, 48u) \
    FIELD(backgroundQueries, 52u) \
    FIELD(backgroundMisses, 56u) \
    FIELD(backgroundFailures, 60u) \
    FIELD(secondaryGlassPixels, 64u) \
    FIELD(transparentHits, 68u) \
    FIELD(backgroundLayerCapPixels, 72u) \
    FIELD(cachedOpaqueReturns, 76u) \
    FIELD(shadedOpaqueReturns, 80u) \
    FIELD(exitScreenBackgroundReturns, 84u) \
    FIELD(exitStraightFallbackPixels, 88u) \
    FIELD(materialShadeCalls, 92u) \
    FIELD(cachedHitAttempts, 96u) \
    FIELD(cachedHitMisses, 100u) \
    FIELD(physicalQueries, 104u) \
    FIELD(queryBudgetUnits, 108u) \
    FIELD(overflowFlags, 112u)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

