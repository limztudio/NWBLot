// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus)
#pragma once
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_AVBOIT_BINDING_SLOTS_H
#define NWB_AVBOIT_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_AVBOIT_TRANSPARENT_SET 1
#define NWB_AVBOIT_COMPUTE_SET 0

#define NWB_AVBOIT_BINDING_OPAQUE_DEPTH 0
#define NWB_AVBOIT_BINDING_POINT_SAMPLER 1

#define NWB_AVBOIT_OCCUPANCY_BINDING_COVERAGE_WORDS 2

#define NWB_AVBOIT_DEPTH_WARP_BINDING_COVERAGE_WORDS 0
#define NWB_AVBOIT_DEPTH_WARP_BINDING_DEPTH_WARP 1
#define NWB_AVBOIT_DEPTH_WARP_BINDING_CONTROL 2

#define NWB_AVBOIT_EXTINCTION_BINDING_DEPTH_WARP 2
#define NWB_AVBOIT_EXTINCTION_BINDING_CONTROL 3
#define NWB_AVBOIT_EXTINCTION_BINDING_EXTINCTION 4
#define NWB_AVBOIT_EXTINCTION_BINDING_OVERFLOW_DEPTH 5

#define NWB_AVBOIT_INTEGRATE_BINDING_EXTINCTION 0
#define NWB_AVBOIT_INTEGRATE_BINDING_TRANSMITTANCE 1
#define NWB_AVBOIT_INTEGRATE_BINDING_CONTROL 2
#define NWB_AVBOIT_INTEGRATE_BINDING_OVERFLOW_DEPTH 3

#define NWB_AVBOIT_ACCUMULATE_BINDING_DEPTH_WARP 0
#define NWB_AVBOIT_ACCUMULATE_BINDING_TRANSMITTANCE 1
#define NWB_AVBOIT_ACCUMULATE_BINDING_CONTROL 2
#define NWB_AVBOIT_ACCUMULATE_BINDING_LINEAR_SAMPLER 3

#define NWB_AVBOIT_ACCUM_COLOR_LOCATION 0
#define NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION 1
#define NWB_AVBOIT_ACCUM_TARGET_COUNT 2


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
