// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_RAYTRACE_OPTICAL_BOUNDS_FINALIZE_CONSTANTS_H
#define NWB_GRAPHICS_RAYTRACE_OPTICAL_BOUNDS_FINALIZE_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE 64u
#define NWB_OPTICAL_BOUNDS_FINALIZE_PUSH_BYTES 24u
#define NWB_OPTICAL_BOUNDS_RUNTIME_INPUT_BYTES 64u
#define NWB_OPTICAL_BOUNDS_RUNTIME_SLOT_OFFSET 48u
#define NWB_OPTICAL_BOUNDS_RUNTIME_INSTANCE_OFFSET 52u

// Integer affine bounds use a common fixed-point grid and an outward binary32 error budget.
#define NWB_OPTICAL_BOUNDS_SIGN_BIT 0x80000000u
#define NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK 0x7fffffffu
#define NWB_OPTICAL_BOUNDS_FRACTION_MASK 0x007fffffu
#define NWB_OPTICAL_BOUNDS_HIDDEN_BIT 0x00800000u
#define NWB_OPTICAL_BOUNDS_MANTISSA_BITS 23u
#define NWB_OPTICAL_BOUNDS_EXPONENT_BIAS 127
#define NWB_OPTICAL_BOUNDS_ONE_BITS 0x3f800000u
#define NWB_OPTICAL_BOUNDS_MIN_INPUT_EXPONENT (-100)
#define NWB_OPTICAL_BOUNDS_MAX_INPUT_EXPONENT 124
#define NWB_OPTICAL_BOUNDS_MAX_MAGNITUDE_EXPONENT 125
#define NWB_OPTICAL_BOUNDS_ZERO_EXPONENT (-256)
#define NWB_OPTICAL_BOUNDS_FIXED_BITS 24
#define NWB_OPTICAL_BOUNDS_RELATIVE_MARGIN_SHIFT 19
#define NWB_OPTICAL_BOUNDS_MIN_MARGIN_EXPONENT (-120)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

