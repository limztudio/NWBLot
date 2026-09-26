// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_LIGHT_SPACE_CONSTANTS_H
#define NWB_GRAPHICS_SHADOW_LIGHT_SPACE_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LIGHT_SPACE_EVENTS_PER_TEXEL 16u
#define NWB_LIGHT_SPACE_EVENT_BYTES 20u
#define NWB_LIGHT_SPACE_EVENT_INSTANCE_MASK 0x7fffffffu
#define NWB_LIGHT_SPACE_EVENT_ENTERING 0x80000000u
#define NWB_LIGHT_SPACE_EVENT_INVALID 0xffffffffu
#define NWB_LIGHT_SPACE_COUNT_INVALID 0xffffffffu
#define NWB_LIGHT_SPACE_VIEW_BYTES 128u
#define NWB_LIGHT_SPACE_DRAW_ARGUMENT_BYTES 20u
#define NWB_LIGHT_SPACE_PUSH_BYTES 64u
#define NWB_LIGHT_SPACE_FLAG_POINT (1u << 0u)
#define NWB_LIGHT_SPACE_FLAG_ELIGIBLE (1u << 1u)
#define NWB_LIGHT_SPACE_FLAG_FITTED_COVERAGE (1u << 2u)
#define NWB_LIGHT_SPACE_FLAG_COMPACT_BLOCKERS (1u << 3u)
#define NWB_LIGHT_SPACE_GROUP_SIZE 8u
#define NWB_LIGHT_SPACE_VIEW_GROUP_SIZE 64u
#define NWB_LIGHT_SPACE_SHADE_GROUP_SIZE 64u
#define NWB_LIGHT_SPACE_SHADE_MAX_GROUPS_X 65535u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

