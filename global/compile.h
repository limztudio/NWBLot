// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(PROP_DBG)
#define NWB_DEBUG
#elif defined(PROP_OPT)
#define NWB_OPTIMIZE
#elif defined(PROP_FIN)
#define NWB_FINAL
#endif

#if !defined(NWB_DEBUG) && (defined(DEBUG) || defined(_DEBUG))
#define NWB_DEBUG
#endif

#if !defined(NWB_OPTIMIZE) && !defined(NWB_FINAL) && (defined(NDEBUG) || defined(_NDEBUG))
#define NWB_OPTIMIZE
#endif

#if defined(NWB_DEBUG)
#define NWB_INLINE
#elif defined(NWB_OPTIMIZE) || defined(NWB_FINAL)
#ifdef _MSC_VER
#define NWB_INLINE __forceinline
#else
#define NWB_INLINE inline
#endif
#endif

#ifdef _MSC_VER
#define NWB_VECTORCALL __vectorcall
#else
#define NWB_VECTORCALL
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

