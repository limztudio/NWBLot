#ifndef NWB_PROJECT_BXDF
#define NWB_PROJECT_BXDF 0
#endif

#ifndef NWB_PROJECT_TINT
#define NWB_PROJECT_TINT 0
#endif


float3 nwbProjectApplyBxdf(float3 color){
#if NWB_PROJECT_BXDF == 0
    return color;
#elif NWB_PROJECT_BXDF == 1
    return sqrt(max(color, 0.0.xxx));
#elif NWB_PROJECT_BXDF == 2
    return color * color;
#else
    return color;
#endif
}

float3 nwbProjectApplyTint(float3 color){
#if NWB_PROJECT_TINT == 1
    return color * float3(1.0, 0.92, 0.85);
#else
    return color;
#endif
}

float3 nwbProjectShadeVertex(float3 color){
    return clamp(nwbProjectApplyBxdf(color), 0.0, 1.0);
}

float3 nwbProjectShadePixel(float3 color){
    return clamp(nwbProjectApplyTint(nwbProjectApplyBxdf(color)), 0.0, 1.0);
}

