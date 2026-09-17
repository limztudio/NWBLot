// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/optics/secondary_geometry_contract.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// P10 explicit performance profile (outside-corpus class-Q contract, not an enabled optimization).
// current-renderer-v1 stays frozen: its captures, manifests, logs, SHA-256 pins, and per-profile
// limits are never edited in place. A candidate profile lives beside the corpus, reports its actual
// settings, and receives separate results. Claiming reference-workload performance while silently
// disabling features or reducing resolution is a workload-definition violation, not a faster profile.
namespace PerformanceBudgetDimension{
    enum Enum : u8{
        TransparentShadowExtent,
        ReflectionExtent,
        RefractionBudget,
        CausticBudget,
        SurfelUpdateBudget,
        SecondaryRayGeometry,
        LightSpaceTransmission,
        Count,
    };
};


// Static profile only. Reference keeps the agreed scene, resolution, geometry, ray budgets, and
// optical model; every scale at 1.0 keeps full extent. A candidate varies exactly one dimension
// from the table in 15.2 and reports it. Dynamic control stays disabled until a later step qualifies
// hysteresis, bounded rate of change, and priority order. Presentation-only frame generation never
// counts as rendered capacity.
struct PerformanceProfile{
    bool retainsMeshCount = true;
    bool retainsTransparentObjects = true;
    bool retainsReflection = true;
    bool retainsRefraction = true;
    bool retainsCaustics = true;
    bool retainsSurfelGi = true;
    f32 transparentShadowExtent = 1.0f;
    f32 reflectionExtent = 1.0f;
    f32 refractionBudget = 1.0f;
    f32 causticBudget = 1.0f;
    f32 surfelUpdateBudget = 1.0f;
    u32 surfelMaxAgeFrames = 8u;
    bool extentReconstruction = false;
    bool secondaryRayProxyEnabled = false;
    bool lightSpaceTransmissionEnabled = false;
    bool dynamicControlEnabled = false;
    bool isReference = false;
};


[[nodiscard]] inline PerformanceProfile ReferencePerformanceProfile(){
    PerformanceProfile profile;
    profile.isReference = true;
    return profile;
}


// Approved experiment grid from 15.2: full, 0.75x, or 0.5x linear work. Anything else is rejected
// until measured evidence approves it.
[[nodiscard]] inline bool IsApprovedBudgetStep(f32 value){
    return value == 1.0f || value == 0.75f || value == 0.5f;
}


[[nodiscard]] inline u32 ChangedPerformanceDimensionCount(const PerformanceProfile& profile){
    u32 changed = 0u;
    if(profile.transparentShadowExtent != 1.0f)
        ++changed;
    if(profile.reflectionExtent != 1.0f)
        ++changed;
    if(profile.refractionBudget != 1.0f)
        ++changed;
    if(profile.causticBudget != 1.0f)
        ++changed;
    if(profile.surfelUpdateBudget != 1.0f)
        ++changed;
    if(profile.secondaryRayProxyEnabled)
        ++changed;
    if(profile.lightSpaceTransmissionEnabled)
        ++changed;
    return changed;
}


// Reducing the visible mesh count, removing transparent objects, or disabling reflection,
// refraction, caustics, or GI changes the target workload definition (15.2). Such a candidate
// must be renamed and requalified; it must not report reference-workload results.
[[nodiscard]] inline bool RequiresNewWorkloadDefinition(const PerformanceProfile& profile){
    return !profile.retainsMeshCount
        || !profile.retainsTransparentObjects
        || !profile.retainsReflection
        || !profile.retainsRefraction
        || !profile.retainsCaustics
        || !profile.retainsSurfelGi
    ;
}


[[nodiscard]] inline bool ValidatePerformanceProfile(
    const PerformanceProfile& profile,
    const SecondaryEffectGeometryProxyDescriptor& proxy,
    const LightSpaceTransmissionDescriptor& field
){
    if(RequiresNewWorkloadDefinition(profile))
        return false;
    if(
        !IsApprovedBudgetStep(profile.transparentShadowExtent)
        || !IsApprovedBudgetStep(profile.reflectionExtent)
        || !IsApprovedBudgetStep(profile.refractionBudget)
        || !IsApprovedBudgetStep(profile.causticBudget)
        || !IsApprovedBudgetStep(profile.surfelUpdateBudget)
    )
        return false;
    if(profile.surfelMaxAgeFrames == 0u)
        return false;
    const bool extentReduced = profile.transparentShadowExtent < 1.0f || profile.reflectionExtent < 1.0f;
    if(extentReduced && !profile.extentReconstruction)
        return false;
    if(profile.secondaryRayProxyEnabled && !proxy.usable())
        return false;
    if(profile.lightSpaceTransmissionEnabled && !field.usable())
        return false;
    if(profile.dynamicControlEnabled)
        return false;
    if(profile.isReference && ChangedPerformanceDimensionCount(profile) != 0u)
        return false;
    return true;
}


// One dimension at a time (15.2): a valid, same-workload, non-reference profile that changes exactly
// one budgeted dimension. Steady-state and newly-invalidated-history quality gates from 15.3 still
// apply before any candidate claims the workload reaches the frame target.
[[nodiscard]] inline bool IsSingleDimensionCandidate(
    const PerformanceProfile& profile,
    const SecondaryEffectGeometryProxyDescriptor& proxy,
    const LightSpaceTransmissionDescriptor& field
){
    return !profile.isReference
        && ValidatePerformanceProfile(profile, proxy, field)
        && ChangedPerformanceDimensionCount(profile) == 1u
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

