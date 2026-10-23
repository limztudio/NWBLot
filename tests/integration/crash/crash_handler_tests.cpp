// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_support.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <dbghelp.h>
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_handler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace CrashTestSupport;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
TEST(Crash, ManualContextTimeoutRetainsOwnedDumpAndReusableHandlerAfterSourceStorageExpires){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    constexpr AStringView s_Group("crash_manual_owned_context_test");
    RemoveTestArtifacts(arena, s_Group);
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = AStringView(s_CrashTests);
    config.spoolDirectory = SpoolDirectory(arena, s_Group);
    ASSERT_TRUE(NWB::Core::Crash::InstallCrashHandler(config));
    ScopeExit uninstall([]()noexcept{ NWB::Core::Crash::UninstallCrashHandler(); });

    NWB::Core::Crash::CrashDetail::CrashRequest request;
    u32 capturedContextFlags = 0u;
    u64 capturedInstructionPointer = 0u;
    {
        NWB::Core::Crash::CrashDetail::ManualDumpContextStorage storage;
        NWB::Core::Crash::CrashDetail::CrashDumpRequestOptions options;
        NWB::Core::Crash::CrashDetail::CaptureManualDumpContext(options, storage);
        capturedContextFlags = storage.context.ContextFlags & ~NWB::Core::Crash::CrashDetail::s_WindowsExtendedContextFlag;
        capturedInstructionPointer = options.instructionPointer;
        auto sourceRequest = NWB::Core::Crash::CrashDetail::BuildCrashDumpRequest(NWB::Core::Crash::CrashDetail::CrashReasonKind::ManualDump, 0u, options);
        NWB_MEMCPY(&request, sizeof(request), &sourceRequest, sizeof(sourceRequest));
        sourceRequest = NWB::Core::Crash::CrashDetail::CrashRequest{};
        storage.context = CONTEXT{};
        storage.exceptionRecord = EXCEPTION_RECORD{};
        storage.exceptionPointers = EXCEPTION_POINTERS{};
    }
    ASSERT_TRUE(NWB::Core::Crash::CrashDetail::IsCurrentCrashRequest(request));
    EXPECT_EQ(request.exceptionPointers, 0u);
    EXPECT_EQ(request.manualContext.ContextFlags, capturedContextFlags);
    EXPECT_NE(capturedContextFlags, 0u);
    NWB::Core::Crash::CrashDetail::ManualDumpContextStorage handlerStorage(request);
    EXPECT_EQ(handlerStorage.exceptionPointers.ContextRecord, &handlerStorage.context);
    EXPECT_EQ(handlerStorage.exceptionPointers.ExceptionRecord, &handlerStorage.exceptionRecord);
    EXPECT_EQ(handlerStorage.exceptionRecord.ExceptionRecord, nullptr);
    EXPECT_EQ(handlerStorage.exceptionRecord.ExceptionAddress, reinterpret_cast<void*>(static_cast<usize>(capturedInstructionPointer)));

    const HANDLE handlerThread = NWB::Core::Crash::CrashDetail::g_State.handlerProcessInfo.hThread;
    ASSERT_NE(handlerThread, nullptr);
    const DWORD previousSuspendCount = SuspendThread(handlerThread);
    ASSERT_NE(previousSuspendCount, static_cast<DWORD>(-1));
    ScopeExit resume([handlerThread]()noexcept{
        if(ResumeThread(handlerThread) == static_cast<DWORD>(-1))
            TerminateInvariant();
    }
    );
    ASSERT_EQ(previousSuspendCount, 0u);
    const auto transport = NWB::Core::Crash::CrashDetail::RequestCrashHandler(request, 1u);
    EXPECT_EQ(transport, NWB::Core::Crash::CrashDetail::CrashDumpTransportStatus::TimedOut);
    ASSERT_NE(ResumeThread(handlerThread), static_cast<DWORD>(-1));
    resume.release();
    ASSERT_EQ(WaitForSingleObject(NWB::Core::Crash::CrashDetail::g_State.crashHandledEvent, NWB::Core::Crash::CrashDetail::s_ManualCrashDumpWaitMilliseconds), WAIT_OBJECT_0);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(request.crashId));
    EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_SymbolicationFileName, AStringView("minidump captured;")));
    EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_ManifestFileName, AStringView("\"has_exception_context\": true")));
    Vector<u8, NWB::Core::Alloc::ScratchArena> dumpBytes(scratchArena);
    const auto read = ReadBinaryFile(packageDirectory / CrashNames::s_ProcessDumpFileName, dumpBytes);
    ASSERT_TRUE(read);
    MINIDUMP_DIRECTORY* directory = nullptr;
    void* streamData = nullptr;
    ULONG streamSize = 0u;
    ASSERT_TRUE(MiniDumpReadDumpStream(dumpBytes.data(), ExceptionStream, &directory, &streamData, &streamSize));
    ASSERT_NE(streamData, nullptr);
    ASSERT_GE(streamSize, sizeof(MINIDUMP_EXCEPTION_STREAM));
    const usize dumpBegin = reinterpret_cast<usize>(dumpBytes.data());
    const usize streamAddress = reinterpret_cast<usize>(streamData);
    ASSERT_GE(streamAddress, dumpBegin);
    const usize streamOffset = streamAddress - dumpBegin;
    ASSERT_LE(streamOffset, dumpBytes.size());
    ASSERT_LE(sizeof(MINIDUMP_EXCEPTION_STREAM), dumpBytes.size() - streamOffset);
    MINIDUMP_EXCEPTION_STREAM exceptionStream = {};
    NWB_MEMCPY(&exceptionStream, sizeof(exceptionStream), dumpBytes.data() + streamOffset, sizeof(exceptionStream));
    EXPECT_EQ(exceptionStream.ThreadId, request.threadId);
    EXPECT_EQ(exceptionStream.ExceptionRecord.ExceptionCode, NWB::Core::Crash::CrashDetail::s_ManualDumpExceptionCode);
    EXPECT_EQ(exceptionStream.ExceptionRecord.ExceptionAddress, capturedInstructionPointer);
    ASSERT_GE(exceptionStream.ThreadContext.DataSize, sizeof(CONTEXT));
    ASSERT_LE(static_cast<u64>(exceptionStream.ThreadContext.Rva) + sizeof(CONTEXT), dumpBytes.size());
    CONTEXT dumpedContext = {};
    NWB_MEMCPY(&dumpedContext, sizeof(dumpedContext), dumpBytes.data() + exceptionStream.ThreadContext.Rva, sizeof(dumpedContext));
#if defined(_M_X64) || defined(__x86_64__)
    EXPECT_EQ(dumpedContext.Rip, capturedInstructionPointer);
#elif defined(_M_IX86) || defined(__i386__)
    EXPECT_EQ(dumpedContext.Eip, capturedInstructionPointer);
#elif defined(_M_ARM64) || defined(__aarch64__)
    EXPECT_EQ(dumpedContext.Pc, capturedInstructionPointer);
#endif
    EXPECT_TRUE(NWB::Core::Crash::CaptureCrashDump(AStringView("after_timeout"), AStringView("handler remains reusable")).packageWritten());
    NWB::Core::Crash::UninstallCrashHandler();
    uninstall.release();
    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, ManualNativeContextAdmissionRejectsWrongVersionSizeArchitectureAndLinkedRecords){
    NWB::Core::Crash::CrashDetail::ManualDumpContextStorage storage;
    NWB::Core::Crash::CrashDetail::CrashDumpRequestOptions options;
    NWB::Core::Crash::CrashDetail::CaptureManualDumpContext(options, storage);
    auto request = NWB::Core::Crash::CrashDetail::BuildCrashDumpRequest(NWB::Core::Crash::CrashDetail::CrashReasonKind::ManualDump, 0u, options);
    ASSERT_TRUE(NWB::Core::Crash::CrashDetail::IsCurrentCrashRequest(request));
    request.version = 1u;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::IsCurrentCrashRequest(request));
    request.version = NWB::Core::Crash::CrashDetail::s_RequestVersion + 1u;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::IsCurrentCrashRequest(request));
    request.version = NWB::Core::Crash::CrashDetail::s_RequestVersion;
    --request.manualContextSize;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::IsCurrentCrashRequest(request));
    request.manualContextSize = sizeof(CONTEXT);
    request.manualContextArchitecture ^= 0x00010000u;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::IsCurrentCrashRequest(request));
    request.manualContextArchitecture = NWB::Core::Crash::CrashDetail::s_WindowsContextArchitecture;
    request.manualExceptionRecord.ExceptionRecord = &storage.exceptionRecord;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::IsCurrentCrashRequest(request));
    request.manualExceptionRecord.ExceptionRecord = nullptr;
    request.manualContext.ContextFlags |= NWB::Core::Crash::CrashDetail::s_WindowsExtendedContextFlag;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::IsCurrentCrashRequest(request));
}

TEST(Crash, DesktopHandlerDoesNotRetainUnrelatedInheritableHandles){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    constexpr AStringView s_Group("crash_handler_handle_inheritance_test");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath testDirectory = TestCaseDirectory(arena, s_Group);
    ASSERT_TRUE(EnsureDirectories(testDirectory));

    const CrashTestPath sentinelPath = testDirectory / "inheritable_handle_sentinel.tmp";
    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;
    const HANDLE sentinelHandle = CreateFileW(sentinelPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &securityAttributes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    ASSERT_NE(sentinelHandle, INVALID_HANDLE_VALUE);

    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
    config.applicationName = AStringView(s_CrashTests);
    config.version = AStringView(s_ApplicationVersion);
    config.buildId = AStringView("handle-inheritance-test");
    config.spoolDirectory = SpoolDirectory(arena, s_Group);

    const bool installed = NWB::Core::Crash::InstallCrashHandler(config);
    EXPECT_TRUE(installed);
    EXPECT_TRUE(CloseHandle(sentinelHandle));
    EXPECT_TRUE(DeleteFileW(sentinelPath.c_str()));
    if(installed)
        NWB::Core::Crash::UninstallCrashHandler();

    RemoveTestArtifacts(arena, s_Group);
}
#endif

#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST(Crash, LinuxManualDumpUnwindsBeyondTheLeafWithoutFramePointers){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    constexpr AStringView s_Group("crash_desktop_handler_runtime_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
    config.applicationName = AStringView(s_CrashTests);
    config.version = AStringView(s_ApplicationVersion);
    config.buildId = AStringView("desktop-handler-runtime-test");
    config.spoolDirectory = SpoolDirectory(arena, s_Group);

    {
        ScopedLock lock(NWB::Core::Crash::CrashDetail::g_State.mutex);
        NWB::Core::Crash::CrashDetail::g_State.crashSequence.store(1u, MemoryOrder::relaxed);
    }

    const bool installed = NWB::Core::Crash::InstallCrashHandler(config);
    ASSERT_TRUE(installed);

    NWB::Core::Crash::CrashDetail::CrashDumpRequestOptions options;
    options.waitMilliseconds = NWB::Core::Crash::CrashDetail::s_PlatformCrashHandlerWaitMilliseconds;
    options.triggerCategory = AStringView(s_Test);
    options.triggerMessage = AStringView("desktop handler runtime");
    options.triggerFile = AStringView("tests/integration/crash/crash_package_tests.cpp");
    NWB::Core::Crash::CrashDetail::ManualDumpContextStorage contextStorage;
    NWB::Core::Crash::CrashDetail::CaptureManualDumpContext(options, contextStorage);
    const NWB::Core::Crash::CrashDumpResult result = NWB::Core::Crash::CrashDetail::RequestCrashDump(NWB::Core::Crash::CrashDetail::CrashReasonKind::ManualDump, 0u, options);
    EXPECT_EQ(result.status, NWB::Core::Crash::CrashDumpStatus::PackageWritten);

    char crashId[NWB::Core::Crash::CrashDetail::s_MaxShortText] = {};
    BuildCrashIdForProcess(crashId, CurrentProcessId(), 1u);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
    {
        const auto directoryReady = WaitForDirectory(packageDirectory, s_PackageReadyTimeoutMS);
        ASSERT_TRUE(directoryReady);
        EXPECT_TRUE(*directoryReady);
    }
    {
        const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_CallstackFileName);
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    // A "#1 " frame proves the unwinder walked past the leaf frame; final builds omit the frame pointer, so this guards that .eh_frame-based capture keeps producing a full callstack.
    EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_CallstackFileName, AStringView("#1 0x")));
    NWB::Core::Crash::UninstallCrashHandler();
    RemoveTestArtifacts(arena, s_Group);
}
#endif


TEST(Crash, DesktopInstalledHandlerWritesGpuCrashTextOnlyPackage){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    constexpr AStringView s_Group("crash_gpu_text_only_runtime_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
    config.applicationName = AStringView(s_CrashTests);
    config.version = AStringView(s_ApplicationVersion);
    config.buildId = AStringView("gpu-text-only-runtime-test");
    config.spoolDirectory = SpoolDirectory(arena, s_Group);

    {
        ScopedLock lock(NWB::Core::Crash::CrashDetail::g_State.mutex);
        NWB::Core::Crash::CrashDetail::g_State.crashSequence.store(1u, MemoryOrder::relaxed);
    }

    const bool installed = NWB::Core::Crash::InstallCrashHandler(config);
    ASSERT_TRUE(installed);

    NWB::Core::Crash::CrashDetail::CrashDumpRequestOptions options;
    options.waitMilliseconds = NWB::Core::Crash::CrashDetail::s_PlatformCrashHandlerWaitMilliseconds;
    options.triggerCategory = AStringView(NWB::Core::Crash::CrashDetail::s_GpuCrashCategory);
    options.gpuReport = AStringView("minimal GPU crash report: no vendor GPU dump or device-fault details were available\n");
    options.gpuDumpKind = NWB::Core::Crash::GpuCrashDumpKind::None;
    NWB::Core::Crash::CrashDetail::ManualDumpContextStorage contextStorage;
    NWB::Core::Crash::CrashDetail::CaptureManualDumpContext(options, contextStorage);
    const NWB::Core::Crash::CrashDumpResult result = NWB::Core::Crash::CrashDetail::RequestCrashDump(NWB::Core::Crash::CrashDetail::CrashReasonKind::GpuCrash, 0u, options);
    EXPECT_EQ(result.status, NWB::Core::Crash::CrashDumpStatus::PackageWritten);

    char crashId[NWB::Core::Crash::CrashDetail::s_MaxShortText] = {};
    BuildCrashIdForProcess(crashId, CurrentProcessId(), 1u);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
    {
        const auto directoryReady = WaitForDirectory(packageDirectory, s_PackageReadyTimeoutMS);
        ASSERT_TRUE(directoryReady);
        EXPECT_TRUE(*directoryReady);
    }
    {
        const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_GpuCrashReportFileName);
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(packageDirectory / CrashNames::s_GpuDetectiveCaptureFileName);
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(packageDirectory / CrashNames::s_AftermathGpuDumpFileName);
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_GpuCrashReportFileName, AStringView("minimal GPU crash report")));
    NWB::Core::Crash::UninstallCrashHandler();

    RemoveTestArtifacts(arena, s_Group);
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST(Crash, LinuxSignalHandlerWritesCrashPackage){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_linux_signal_runtime_test");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    const pid_t childPid = fork();
    ASSERT_GE(childPid, 0);
    if(childPid == 0){
        NWB::Core::Alloc::PersistentArena installArena(s_SignalChildInstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
        NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
        config.applicationName = AStringView(s_CrashTests);
        config.version = AStringView(s_ApplicationVersion);
        config.buildId = AStringView("linux-signal-runtime-test");
        config.spoolDirectory = spoolDirectory;

        {
            ScopedLock lock(NWB::Core::Crash::CrashDetail::g_State.mutex);
            NWB::Core::Crash::CrashDetail::g_State.crashSequence.store(1u, MemoryOrder::relaxed);
        }

        if(!NWB::Core::Crash::InstallCrashHandler(config))
            _exit(111);

        raise(SIGSEGV);
        _exit(112);
    }

    if(childPid > 0){
        int status = 0;
        while(waitpid(childPid, &status, 0) < 0){
            if(errno == EINTR)
                continue;
            break;
        }

        EXPECT_TRUE(WIFSIGNALED(status));
        EXPECT_EQ(WTERMSIG(status), SIGSEGV);

        char crashId[NWB::Core::Crash::CrashDetail::s_MaxShortText] = {};
        BuildCrashIdForProcess(crashId, static_cast<u32>(childPid), 1u);

        const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
        {
            constexpr u32 s_MissingPackageReadyTimeoutMS = 1000u;
            const auto directoryReady = WaitForDirectory(packageDirectory, s_MissingPackageReadyTimeoutMS);
            ASSERT_TRUE(directoryReady);
            EXPECT_TRUE(*directoryReady);
        }
        {
            const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_ManifestFileName);
            ASSERT_TRUE(pathQuery);
            EXPECT_TRUE(*pathQuery);
        }
        {
            const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_CallstackFileName);
            ASSERT_TRUE(pathQuery);
            EXPECT_TRUE(*pathQuery);
        }
        // The faulting callstack must reach past the leaf frame even with the frame pointer omitted (final builds).
        EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_CallstackFileName, AStringView("#1 0x")));
        {
            const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_ProcMapsFileName);
            ASSERT_TRUE(pathQuery);
            EXPECT_TRUE(*pathQuery);
        }
        EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_ManifestFileName, AStringView("\"reason_kind\": \"signal\"")));
    }

    RemoveTestArtifacts(arena, s_Group);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

