// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "settings.h"

#include <impl/assets/graphics/caustic/photon_push_constants.h>

#include <global/assert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateCausticQualitySettings(const CausticQualitySettings& settings)noexcept{
    constexpr u32 s_HalfPhotonGridDivisor = 2u;
    constexpr u32 s_QuarterPhotonGridDivisor = 4u;
    return settings.photonGridDivisor == 1u || settings.photonGridDivisor == s_HalfPhotonGridDivisor || settings.photonGridDivisor == s_QuarterPhotonGridDivisor;
}

CausticPhotonBudget MakeCausticPhotonBudget(const CausticQualitySettings& settings, const u32 baseGridSide, const u32 temporalPhaseCount)noexcept{
    NWB_ASSERT(ValidateCausticQualitySettings(settings));
    NWB_ASSERT(baseGridSide == s_CausticReducedBaseGridSide || baseGridSide == s_CausticFullBaseGridSide);
    NWB_ASSERT(temporalPhaseCount == NWB_CAUSTIC_TEMPORAL_DISABLED_PHASE_COUNT || temporalPhaseCount == NWB_CAUSTIC_TEMPORAL_PHASE_COUNT);
    const u32 gridSide = baseGridSide / settings.photonGridDivisor;
    const u32 fullGridCount = gridSide * gridSide;
    return { gridSide, fullGridCount, fullGridCount / temporalPhaseCount };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

