// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{

#if defined(FOUNDATION_DEBUG)
inline constexpr b8 s_Debug = true;
#else
inline constexpr b8 s_Debug = false;
#endif

#if defined(FOUNDATION_OPTIMIZE)
inline constexpr b8 s_Optimize = true;
#else
inline constexpr b8 s_Optimize = false;
#endif

#if defined(FOUNDATION_FINAL)
inline constexpr b8 s_Final = true;
#else
inline constexpr b8 s_Final = false;
#endif

#if defined(FOUNDATION_PLATFORM_WINDOWS)
inline constexpr b8 s_Windows = true;
#else
inline constexpr b8 s_Windows = false;
#endif

#if defined(FOUNDATION_PLATFORM_LINUX)
inline constexpr b8 s_Linux = true;
#else
inline constexpr b8 s_Linux = false;
#endif

#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
inline constexpr b8 s_DesktopLinux = true;
#else
inline constexpr b8 s_DesktopLinux = false;
#endif

#if defined(FOUNDATION_HAS_SSE2)
inline constexpr b8 s_HasSse2 = true;
#else
inline constexpr b8 s_HasSse2 = false;
#endif

#if defined(FOUNDATION_HAS_AVX)
inline constexpr b8 s_HasAvx = true;
#else
inline constexpr b8 s_HasAvx = false;
#endif

#if defined(FOUNDATION_HAS_AVX2)
inline constexpr b8 s_HasAvx2 = true;
#else
inline constexpr b8 s_HasAvx2 = false;
#endif

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
