// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <global/process.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_RequestMagic = 0x4E574243u; // NWBC
inline constexpr u32 s_AckMagic = 0x4E574241u; // NWBA
inline constexpr u32 s_RequestVersion = 2u;
inline constexpr usize s_MaxMetadata = 32u;
inline constexpr usize s_MaxBreadcrumbs = 128u;
inline constexpr usize s_MaxCallstackFrames = 64u;
inline constexpr usize s_MaxShortText = 64u;
inline constexpr usize s_MaxMediumText = 256u;
inline constexpr usize s_MaxPathText = 1024u;
inline constexpr usize s_MaxUrlText = 1024u;
inline constexpr u32 s_ManualDumpExceptionCode = 0xE0425742u;
#if defined(NWB_PLATFORM_WINDOWS)
inline constexpr u32 s_WindowsContextArchitectureMask = 0x00FF0000u;
inline constexpr u32 s_WindowsContextArchitecture = CONTEXT_CONTROL & s_WindowsContextArchitectureMask;
inline constexpr u32 s_WindowsExtendedContextFlag = CONTEXT_XSTATE & ~s_WindowsContextArchitectureMask;
#endif


namespace PlatformKind{
    enum Enum : u32{
        Unknown,
        Windows,
        Linux,
        Android,
    };
};

namespace CrashReasonKind{
    enum Enum : u32{
        Unknown,
        WindowsException,
        PosixSignal,
        Terminate,
        ManualDump,
        GpuCrash,
    };
};

struct FixedMetadata{
    Array<char, s_MaxShortText> key = {};
    Array<char, s_MaxMediumText> value = {};
    u8 used = 0u;
};

struct FixedBreadcrumb{
    u64 order = 0u;
    Array<char, s_MaxShortText> category = {};
    Array<char, s_MaxMediumText> message = {};
    u8 used = 0u;
};

struct CrashRequest{
    u32 magic = s_RequestMagic;
    u32 version = s_RequestVersion;
    u32 platform = PlatformKind::Unknown;
    u32 reasonKind = CrashReasonKind::Unknown;
    u32 reasonCode = 0u;
    u32 processId = 0u;
    u32 threadId = 0u;
    u32 dumpDetailMode = DumpDetailMode::Small;
    u64 exceptionPointers = 0u;
    u64 faultAddress = 0u;
    u64 instructionPointer = 0u;
    u64 stackPointer = 0u;
    u64 framePointer = 0u;
    u32 triggerLine = 0u;
    u32 callstackFrameCount = 0u;
    u32 callstackFramesToSkip = 0u;
    CrashSpoolRetentionConfig spoolRetention;
    Array<char, s_MaxShortText> crashId = {};
    Array<char, s_MaxShortText> applicationName = {};
    Array<char, s_MaxShortText> versionText = {};
    Array<char, s_MaxMediumText> buildId = {};
    Array<char, s_MaxShortText> abi = {};
    Array<char, s_MaxPathText> spoolDirectory = {};
    Array<char, s_MaxUrlText> logServerUrl = {};
    Array<char, s_MaxMediumText> crashUploadToken = {};
    Array<char, s_MaxShortText> event = {};
    Array<char, s_MaxShortText> triggerCategory = {};
    Array<char, s_MaxMediumText> triggerExpression = {};
    Array<char, s_MaxMediumText> triggerMessage = {};
    Array<char, s_MaxPathText> triggerFile = {};
    u32 metadataCount = 0u;
    u32 breadcrumbCount = 0u;
    Array<FixedMetadata, s_MaxMetadata> metadata = {};
    Array<FixedBreadcrumb, s_MaxBreadcrumbs> breadcrumbs = {};
    Array<u64, s_MaxCallstackFrames> callstackFrames = {};
#if defined(NWB_PLATFORM_WINDOWS)
    u32 manualContextSize = 0u;
    u32 manualExceptionRecordSize = 0u;
    u32 manualContextArchitecture = 0u;
    CONTEXT manualContext = {};
    EXCEPTION_RECORD manualExceptionRecord = {};
#endif
};

static_assert(IsTriviallyCopyable_V<CrashRequest>, "crash request transport requires an owning fixed native value");
#if defined(NWB_PLATFORM_WINDOWS)
static_assert(alignof(CrashRequest) >= alignof(CONTEXT), "crash request must preserve native context alignment");
#endif

struct CrashAck{
    u32 magic = s_AckMagic;
    u32 version = s_RequestVersion;
    u8 packageWritten = 0u;
    Array<char, s_MaxShortText> crashId = {};
};

// This fixed-size native protocol is shared only by the application and its same-build handler.
// Native context admission does not provide mixed-binary or cross-architecture message framing.
class CrashProtocol final{
public:
    [[nodiscard]] static b8 IsCurrentCrashRequest(const CrashRequest& request)noexcept;
    [[nodiscard]] static b8 CrashAckMatchesRequest(const CrashAck& ack, const CrashRequest& request)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

