// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#ifndef NWB_GRAPHICS_MESH_MESHLET_CONSTANTS_H
#define NWB_GRAPHICS_MESH_MESHLET_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_MESH_SHADER_GROUP_SIZE_X
#define NWB_MESH_SHADER_GROUP_SIZE_X 128
#endif

#define NWB_MESH_SHADER_MAX_VERTICES 96
#define NWB_MESH_SHADER_MAX_TRIANGLES 126

#define NWB_MESHLET_BOUNDS_STRIDE 24u
#define NWB_MESHLET_BOUNDS_SPHERE_BYTE_OFFSET 0u
#define NWB_MESHLET_BOUNDS_CONE_BYTE_OFFSET 16u
#define NWB_MESHLET_BOUNDS_PADDING_BYTE_OFFSET 20u
#define NWB_MESHLET_CONE_FLAG_ENABLED 1u
#define NWB_MESHLET_COUNT_MASK 0xffu
#define NWB_MESHLET_PRIMITIVE_COUNT_SHIFT 8u
#define NWB_MESHLET_POSITION_COUNT_SHIFT 16u
#define NWB_MESHLET_ATTRIBUTE_COUNT_SHIFT 24u
#define NWB_MESHLET_CONE_AXIS_X_SHIFT 0u
#define NWB_MESHLET_CONE_AXIS_Y_SHIFT 8u
#define NWB_MESHLET_CONE_CUTOFF_SHIFT 16u
#define NWB_MESHLET_CONE_FLAG_SHIFT 24u
#define NWB_MESH_MISSING_STREAM_INDEX 0xffffffffu
#define NWB_MESHLET_PACKED_BYTE_BITS 8u
#define NWB_MESHLET_PACKED_BYTE_MASK 0xffu
#define NWB_MESHLET_PACKED_WORD_BYTE_MASK 0x3u
#define NWB_MESHLET_PACKED_HALFWORD_BITS 16u
#define NWB_MESHLET_PACKED_HALFWORD_MASK 0xffffu
#define NWB_MESHLET_TRIANGLE_INDEX_COUNT 3u
#define NWB_MESHLET_CONE_AXIS_FALLBACK 0x8080u
#define NWB_MESHLET_CONE_AXIS_LENGTH_EPSILON 0.000001
#define NWB_MESHLET_CONE_AXIS_LENGTH_SQUARED_EPSILON 0.00000001
#define NWB_MESHLET_REF_WIDTH_U8 0u
#define NWB_MESHLET_REF_WIDTH_U16 1u
#define NWB_MESHLET_REF_WIDTH_U32 2u
#define NWB_MESHLET_REF_ENCODING_WIDTH_MASK 0x3u
#define NWB_MESHLET_REF_ENCODING_POSITION_SHIFT 0u
#define NWB_MESHLET_REF_ENCODING_SKIN_SHIFT 2u
#define NWB_MESHLET_REF_ENCODING_NORMAL_SHIFT 4u
#define NWB_MESHLET_REF_ENCODING_TANGENT_SHIFT 6u
#define NWB_MESHLET_REF_ENCODING_UV0_SHIFT 8u
#define NWB_MESHLET_REF_ENCODING_COLOR_SHIFT 10u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

