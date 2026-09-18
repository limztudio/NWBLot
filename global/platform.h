// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(WIN32) || defined(_WIN32)
#define NWB_PLATFORM_WINDOWS
#endif
#if defined(__linux__)
#define NWB_PLATFORM_LINUX
#endif
#if defined(__unix__)
#define NWB_PLATFORM_UNIX
#endif
#if defined(__ANDROID__)
#define NWB_PLATFORM_ANDROID
#endif
#if defined(__APPLE__)
#define NWB_PLATFORM_APPLE
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline const char* CurrentAbiName()noexcept{
    static constexpr char s_Arm64AbiName[] = "arm64";
    static constexpr char s_ArmAbiName[] = "arm";
    static constexpr char s_X86_64AbiName[] = "x86_64";
    static constexpr char s_X86AbiName[] = "x86";
    static constexpr char s_UnknownAbiName[] = "unknown";
#if defined(__aarch64__) || defined(_M_ARM64)
    return s_Arm64AbiName;
#elif defined(__arm__) || defined(_M_ARM)
    return s_ArmAbiName;
#elif defined(__x86_64__) || defined(_M_X64)
    return s_X86_64AbiName;
#elif defined(__i386__) || defined(_M_IX86)
    return s_X86AbiName;
#else
    return s_UnknownAbiName;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

