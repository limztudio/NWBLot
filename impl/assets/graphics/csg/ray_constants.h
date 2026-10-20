// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CSG_RAY_CONSTANTS_H
#define NWB_GRAPHICS_CSG_RAY_CONSTANTS_H

#ifndef NWB_CSG_RAY_OPTICAL_REUSE
#define NWB_CSG_RAY_OPTICAL_REUSE 0
#endif
#if NWB_CSG_RAY_OPTICAL_REUSE != 0 && NWB_CSG_RAY_OPTICAL_REUSE != 1
#error NWB_CSG_RAY_OPTICAL_REUSE must be zero or one
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CSG_RAY_RECEIVER_ACTIVE 1u
#define NWB_CSG_RAY_RECEIVER_UNSUPPORTED 2u
#define NWB_CSG_RAY_SHAPE_UNSUPPORTED 0u
#define NWB_CSG_RAY_SHAPE_PLANE 1u
#define NWB_CSG_RAY_SHAPE_BOX 2u
#define NWB_CSG_RAY_SHAPE_SPHERE 3u
#define NWB_CSG_RAY_SHAPE_CAPSULE 4u
#define NWB_CSG_RAY_MAX_CUTTERS 8u
#define NWB_CSG_RAY_CONTEXT_BYTES 32u
#define NWB_CSG_RAY_RANGE_BYTES 16u
#define NWB_CSG_RAY_CUTTER_BYTES 96u
#define NWB_CSG_RAY_INSTANCE_BYTES 96u
#define NWB_CSG_RAY_ACTIVE_INDEX_BYTES 4u
#define NWB_CSG_RAY_LANE_BYTES 16u

// Context header word byte offsets; each *_BASE word holds the byte offset of its array.
#define NWB_CSG_RAY_CONTEXT_CUTTER_BASE_OFFSET 8u
#define NWB_CSG_RAY_CONTEXT_CUTTER_COUNT_OFFSET 12u
#define NWB_CSG_RAY_CONTEXT_INSTANCE_BASE_OFFSET 16u
#define NWB_CSG_RAY_CONTEXT_ACTIVE_BASE_OFFSET 20u
#define NWB_CSG_RAY_CONTEXT_ACTIVE_COUNT_OFFSET 24u

// Cutter and instance record field byte offsets.
#define NWB_CSG_RAY_CUTTER_ROW0_OFFSET 16u
#define NWB_CSG_RAY_CUTTER_ROW1_OFFSET 32u
#define NWB_CSG_RAY_CUTTER_ROW2_OFFSET 48u
#define NWB_CSG_RAY_CUTTER_PARAMETER0_OFFSET 64u
#define NWB_CSG_RAY_CUTTER_PARAMETER1_OFFSET 80u
#define NWB_CSG_RAY_INSTANCE_ROW1_OFFSET 16u
#define NWB_CSG_RAY_INSTANCE_ROW2_OFFSET 32u
#define NWB_CSG_RAY_INSTANCE_TAIL_OFFSET 48u
#define NWB_CSG_RAY_INSTANCE_LOCAL_MIN_OFFSET 64u

#define NWB_CSG_RAY_MEMBERSHIP_WORD_BITS 32u
#define NWB_CSG_RAY_MEMBERSHIP_WORD_BYTES 4u
#define NWB_CSG_RAY_CROSSING_CAPACITY 64u
#define NWB_CSG_RAY_INVALID_INDEX 0xffffffffu
#define NWB_CSG_RAY_FAILURE_UNSUPPORTED 1u
#define NWB_CSG_RAY_FAILURE_CAPACITY 2u
#define NWB_CSG_RAY_FAILURE_TOPOLOGY 4u
#define NWB_CSG_RAY_FAILURE_AMBIGUOUS_NORMAL 8u
#define NWB_CSG_RAY_FAILURE_OPEN_CAPACITY 16u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

