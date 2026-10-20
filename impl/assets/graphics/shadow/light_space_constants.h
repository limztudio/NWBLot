// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_LIGHT_SPACE_CONSTANTS_H
#define NWB_GRAPHICS_SHADOW_LIGHT_SPACE_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LIGHT_SPACE_EVENTS_PER_TEXEL 16u
#define NWB_LIGHT_SPACE_EVENT_BYTES 20u
#define NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK 0x7fffffffu
#define NWB_LIGHT_SPACE_EVENT_ENTERING 0x80000000u
// The last event word holds the captured primitive first, then the packed blue absorption and interface transmission.
#define NWB_LIGHT_SPACE_EVENT_TAIL_OFFSET 16u
// One uint per texel in the counts and opaque-depth buffers.
#define NWB_LIGHT_SPACE_PIXEL_WORD_BYTES 4u
#define NWB_LIGHT_SPACE_VIEW_BYTES 128u
#define NWB_LIGHT_SPACE_VIEW_ROW_BYTES 16u
#define NWB_LIGHT_SPACE_VIEW_ORIGIN_OFFSET 64u
#define NWB_LIGHT_SPACE_VIEW_DEPTH_RANGE_OFFSET 80u
#define NWB_LIGHT_SPACE_VIEW_MAP_OFFSET 96u
#define NWB_LIGHT_SPACE_VIEW_LIGHT_OFFSET 112u
#define NWB_LIGHT_SPACE_CUBE_FACE_COUNT 6u
#define NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES 20u
#define NWB_LIGHT_SPACE_DRAW_FIRST_INSTANCE_OFFSET 16u
#define NWB_LIGHT_SPACE_PUSH_BYTES 108u
#define NWB_LIGHT_SPACE_FLAG_POINT (1u << 0u)
#define NWB_LIGHT_SPACE_FLAG_ELIGIBLE (1u << 1u)
#define NWB_LIGHT_SPACE_FLAG_FITTED_COVERAGE (1u << 2u)
#define NWB_LIGHT_SPACE_FLAG_COMPACT_BLOCKERS (1u << 3u)
#define NWB_LIGHT_SPACE_FLAG_CENTER_BLOCKER (1u << 4u)
#define NWB_LIGHT_SPACE_GROUP_SIZE 8u
#define NWB_LIGHT_SPACE_VIEW_GROUP_SIZE 64u
#define NWB_LIGHT_SPACE_CULL_GROUP_SIZE 64u
#define NWB_LIGHT_SPACE_CULL_MAX_GROUPS_X 65535u
#define NWB_LIGHT_SPACE_MESHLETS_PER_DRAW 32u
#define NWB_LIGHT_SPACE_SHADE_GROUP_SIZE 64u
#define NWB_LIGHT_SPACE_SHADE_MAX_GROUPS_X 65535u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

