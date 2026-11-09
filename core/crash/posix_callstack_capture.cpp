// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "posix_callstack_capture.h"

#include <global/algorithm.h>
#include <ucontext.h>
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
#include <execinfo.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_posix_callstack_capture{


inline constexpr usize s_AArch64FramePointerRegisterIndex = 29u;
inline constexpr usize s_MaxFramePointerWalkBytes = 8u * 1024u * 1024u;


struct FramePointerRecord{
    const FramePointerRecord* previous = nullptr;
    const void* returnAddress = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendCallstackFrame(CrashDetail::CrashDumpRequestOptions& options, const u64 address)noexcept{
    if(address == 0u || options.callstackFrameCount >= CrashDetail::s_MaxCallstackFrames)
        return;
    if(options.callstackFrameCount != 0u && options.callstackFrames[options.callstackFrameCount - 1u] == address)
        return;

    options.callstackFrames[options.callstackFrameCount++] = address;
}

static void CaptureFramePointerCallstack(CrashDetail::CrashDumpRequestOptions& options, const u64 instructionPointer, const u64 stackPointer, const u64 framePointer)noexcept{
    AppendCallstackFrame(options, instructionPointer);

    if(stackPointer == 0u || framePointer == 0u || !IsAligned(framePointer, static_cast<u64>(sizeof(void*))))
        return;
    if(framePointer < stackPointer || framePointer - stackPointer > s_MaxFramePointerWalkBytes)
        return;

    u64 currentFrame = framePointer;
    while(options.callstackFrameCount < CrashDetail::s_MaxCallstackFrames){
        const auto* const frame = reinterpret_cast<const FramePointerRecord*>(static_cast<usize>(currentFrame));
        const u64 nextFrame = static_cast<u64>(reinterpret_cast<usize>(frame->previous));
        const u64 returnAddress = static_cast<u64>(reinterpret_cast<usize>(frame->returnAddress));
        AppendCallstackFrame(options, returnAddress);

        if(nextFrame <= currentFrame || !IsAligned(nextFrame, static_cast<u64>(sizeof(void*))))
            break;
        if(nextFrame < stackPointer || nextFrame - stackPointer > s_MaxFramePointerWalkBytes)
            break;

        currentFrame = nextFrame;
    }
}





};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using __hidden_posix_callstack_capture::AppendCallstackFrame;
using __hidden_posix_callstack_capture::CaptureFramePointerCallstack;


#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
// backtrace() unwinds via .eh_frame without frame pointers; warmed up at install so the crash-time call stays allocation-free.
// Pre-2.35 glibc may take the loader lock (dl_iterate_phdr); newer glibc uses lock-free __dl_find_object.
// When the trigger IP is known, capture starts there (robust to inlining); otherwise only that IP is emitted.
// Returns false when nothing unwound so the caller falls back to the frame-pointer walk.
b8 PosixCallstackCapture::CaptureEhFrame(CrashDetail::CrashDumpRequestOptions& options, const u64 alignmentInstructionPointer)noexcept{
    Array<void*, CrashDetail::s_MaxCallstackFrames> unwoundAddresses;
    const int unwoundFrameCount = backtrace(unwoundAddresses.data(), static_cast<int>(CrashDetail::s_MaxCallstackFrames));
    if(unwoundFrameCount <= 0)
        return false;

    if(alignmentInstructionPointer != 0u){
        for(int i = 0; i < unwoundFrameCount; ++i){
            if(static_cast<u64>(reinterpret_cast<usize>(unwoundAddresses[i])) != alignmentInstructionPointer)
                continue;

            for(usize frame = static_cast<usize>(i); frame < static_cast<usize>(unwoundFrameCount); ++frame)
                AppendCallstackFrame(options, static_cast<u64>(reinterpret_cast<usize>(unwoundAddresses[frame])));
            return options.callstackFrameCount != 0u;
        }

        AppendCallstackFrame(options, alignmentInstructionPointer);
        return true;
    }

    for(int i = 0; i < unwoundFrameCount; ++i)
        AppendCallstackFrame(options, static_cast<u64>(reinterpret_cast<usize>(unwoundAddresses[i])));
    return options.callstackFrameCount != 0u;
}

void PosixCallstackCapture::WarmUp()noexcept{
    Array<void*, CrashDetail::s_MaxCallstackFrames> warmUpAddresses;
    const int warmedFrameCount = backtrace(warmUpAddresses.data(), static_cast<int>(CrashDetail::s_MaxCallstackFrames));
    if(warmedFrameCount <= 0)
        return;
}
#endif

void PosixCallstackCapture::CaptureCurrent(CrashDetail::CrashDumpRequestOptions& outOptions, const void* const stackPointer, const void* const framePointer, const void* const instructionPointer)noexcept{
    outOptions.stackPointer = static_cast<u64>(reinterpret_cast<usize>(stackPointer));
    outOptions.framePointer = static_cast<u64>(reinterpret_cast<usize>(framePointer));
    outOptions.instructionPointer = static_cast<u64>(reinterpret_cast<usize>(instructionPointer));
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    const u64 alignmentInstructionPointer = outOptions.triggerInstructionPointer != 0u ? outOptions.triggerInstructionPointer : outOptions.instructionPointer;
    if(CaptureEhFrame(outOptions, alignmentInstructionPointer)){
        // backtrace already trimmed crash-handler-internal frames, so the downstream skip must not drop user frames.
        outOptions.callstackFramesToSkip = 0u;
        return;
    }
#endif
    CaptureFramePointerCallstack(outOptions, outOptions.instructionPointer, outOptions.stackPointer, outOptions.framePointer);
}

namespace __hidden_posix_callstack_capture{


static void CaptureSignalContext(CrashDetail::CrashDumpRequestOptions& options, const siginfo_t* signalInfo, const void* signalContext)noexcept{
    if(signalInfo)
        options.faultAddress = static_cast<u64>(reinterpret_cast<usize>(signalInfo->si_addr));

    const ucontext_t* context = static_cast<const ucontext_t*>(signalContext);
    if(!context)
        return;

#if defined(__x86_64__) && defined(REG_RIP) && defined(REG_RSP) && defined(REG_RBP)
    options.instructionPointer = static_cast<u64>(context->uc_mcontext.gregs[REG_RIP]);
    options.stackPointer = static_cast<u64>(context->uc_mcontext.gregs[REG_RSP]);
    options.framePointer = static_cast<u64>(context->uc_mcontext.gregs[REG_RBP]);
#elif defined(__aarch64__)
    options.instructionPointer = static_cast<u64>(context->uc_mcontext.pc);
    options.stackPointer = static_cast<u64>(context->uc_mcontext.sp);
    options.framePointer = static_cast<u64>(context->uc_mcontext.regs[s_AArch64FramePointerRegisterIndex]);
#endif

#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    // backtrace() crosses the signal trampoline into the interrupted frame, so the fault IP aligns the trace even in frame-pointer-omitting final builds.
    if(PosixCallstackCapture::CaptureEhFrame(options, options.instructionPointer))
        return;
#endif
    CaptureFramePointerCallstack(options, options.instructionPointer, options.stackPointer, options.framePointer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PosixCallstackCapture::SignalHandler(const int signalNumber, siginfo_t* signalInfo, void* signalContext)noexcept{
    if(!CrashDetail::TryConsumeSuppressedPlatformCrashCapture()){
        CrashDetail::CrashDumpRequestOptions options;
        __hidden_posix_callstack_capture::CaptureSignalContext(options, signalInfo, signalContext);
        CrashDetail::NotifyCrashHandler(CrashDetail::CrashReasonKind::PosixSignal, static_cast<u32>(signalNumber), options);
    }

    struct sigaction action = {};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signalNumber, &action, nullptr);
    raise(signalNumber);
}

[[noreturn]] void PosixCallstackCapture::Terminate()noexcept{
    CrashDetail::CrashDumpRequestOptions options;
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    // std::terminate runs in a normal thread context, so unwind a callstack directly instead of relying on the follow-up SIGABRT to carry it.
    if(!CaptureEhFrame(options, 0u))
        options.callstackFrameCount = 0u;
#endif
    CrashDetail::NotifyCrashHandler(CrashDetail::CrashReasonKind::Terminate, 0u, options);
    abort();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

