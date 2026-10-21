// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_RAYTRACE_OPTICAL_TRANSPORT_CONSTANTS_H
#define NWB_GRAPHICS_RAYTRACE_OPTICAL_TRANSPORT_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_OPTICAL_DEFAULT_QUERIES 16u
#if defined(NWB_CAUSTIC_PHOTON_TRANSPORT)
// Sixteen bounces retain nearest/window queries, upstream completion and bounded analytical refinement.
#define NWB_OPTICAL_MAX_QUERIES (3u * 16u + 2u + 2u * NWB_OPTICAL_MAX_CANDIDATE_EVENTS)
#define NWB_OPTICAL_MAX_MEDIA 16u
#else
#define NWB_OPTICAL_MAX_QUERIES 16u
#define NWB_OPTICAL_MAX_MEDIA 4u
#endif
#define NWB_OPTICAL_MAX_CANDIDATE_EVENTS 32u
#define NWB_OPTICAL_MAX_WINDING 32u

#define NWB_OPTICAL_TERMINATION_OPAQUE 0u
#define NWB_OPTICAL_TERMINATION_MISS_OUTSIDE 1u
#define NWB_OPTICAL_TERMINATION_RANGE 2u
#define NWB_OPTICAL_TERMINATION_UNSUPPORTED 3u
#define NWB_OPTICAL_TERMINATION_QUERY_LIMIT 4u
#define NWB_OPTICAL_TERMINATION_EVENT_LIMIT 5u
#define NWB_OPTICAL_TERMINATION_MEDIUM_LIMIT 6u
#define NWB_OPTICAL_TERMINATION_AMBIGUOUS 7u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

