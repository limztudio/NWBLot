// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_BINDLESS_TLAS
#error "NWB_BINDLESS_TLAS is fixed by this hardware shader source"
#endif
#define NWB_BINDLESS_TLAS 1
#define NWB_CSG_SHADOW_HW_TRACE 1

#ifndef NWB_LIGHT_SPACE_CSG_ENABLED
#define NWB_LIGHT_SPACE_CSG_ENABLED 1
#endif
#if !NWB_LIGHT_SPACE_CSG_ENABLED
#error "hardware light-space resolve requires CSG receivers"
#endif

#include "light_space_resolve_cs.slang"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

