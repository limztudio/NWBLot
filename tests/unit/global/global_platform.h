// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <tests/common/gtest_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases{

#if defined(FOUNDATION_PLATFORM_WINDOWS)
inline constexpr b8 s_WindowsPath = true;
#else
inline constexpr b8 s_WindowsPath = false;
#endif

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_WINDOWS)
#define NWB_GLOBAL_WINDOWS_CASE(Suite, Case, Fn) NWB_GTEST_CASE(Suite, Case, Fn)
#else
#define NWB_GLOBAL_WINDOWS_CASE(Suite, Case, Fn)
#endif
#if defined(FOUNDATION_PLATFORM_LINUX)
#define NWB_GLOBAL_LINUX_CASE(Suite, Case, Fn) NWB_GTEST_CASE(Suite, Case, Fn)
#else
#define NWB_GLOBAL_LINUX_CASE(Suite, Case, Fn)
#endif
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
#define NWB_GLOBAL_POSIX_CASE(Suite, Case, Fn) NWB_GTEST_CASE(Suite, Case, Fn)
#else
#define NWB_GLOBAL_POSIX_CASE(Suite, Case, Fn)
#endif
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
#define NWB_GLOBAL_NONWINDOWS_CASE(Suite, Case, Fn) NWB_GTEST_CASE(Suite, Case, Fn)
#else
#define NWB_GLOBAL_NONWINDOWS_CASE(Suite, Case, Fn)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

