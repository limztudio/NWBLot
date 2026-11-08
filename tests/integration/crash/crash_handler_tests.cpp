// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_support.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <dbghelp.h>
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_handler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace CrashTestSupport;
using namespace NWB::Tests;
using CrashTestSupport::TestArena;
using namespace NWB::Tests::CrashTestPaths;
namespace CrashNames = NWB::Core::Crash::PackageNames;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Crash, RequestAdmissionRejectsCorruptCountsAndUnterminatedText){
    using namespace NWB::Core::Crash::CrashDetail;
    CrashRequest request;
    CopyFixedBuffer(request.crashId, "crash-1-1");
    ASSERT_TRUE(CrashProtocol::IsCurrentCrashRequest(request));

    request.metadataCount = static_cast<u32>(s_MaxMetadata + 1u);
    EXPECT_FALSE(CrashProtocol::IsCurrentCrashRequest(request));
    request.metadataCount = 1u;
    request.breadcrumbCount = static_cast<u32>(s_MaxBreadcrumbs + 1u);
    EXPECT_FALSE(CrashProtocol::IsCurrentCrashRequest(request));
    request.breadcrumbCount = 1u;
    request.callstackFrameCount = static_cast<u32>(s_MaxCallstackFrames + 1u);
    EXPECT_FALSE(CrashProtocol::IsCurrentCrashRequest(request));
    request.callstackFrameCount = 0u;

    const auto rejectUnterminated = [&request](auto& text){
        const auto original = text;
        text.fill('x');
        EXPECT_FALSE(CrashProtocol::IsCurrentCrashRequest(request));
        text = original;
    };
    rejectUnterminated(request.crashId);
    rejectUnterminated(request.applicationName);
    rejectUnterminated(request.versionText);
    rejectUnterminated(request.buildId);
    rejectUnterminated(request.abi);
    rejectUnterminated(request.spoolDirectory);
    rejectUnterminated(request.logServerUrl);
    rejectUnterminated(request.crashUploadToken);
    rejectUnterminated(request.event);
    rejectUnterminated(request.triggerCategory);
    rejectUnterminated(request.triggerExpression);
    rejectUnterminated(request.triggerMessage);
    rejectUnterminated(request.triggerFile);
    rejectUnterminated(request.metadata[0u].key);
    rejectUnterminated(request.metadata[0u].value);
    rejectUnterminated(request.breadcrumbs[0u].category);
    rejectUnterminated(request.breadcrumbs[0u].message);
    EXPECT_TRUE(CrashProtocol::IsCurrentCrashRequest(request));
    CopyFixedBuffer(request.crashId, "../outside");
    EXPECT_FALSE(CrashProtocol::IsCurrentCrashRequest(request));
    CopyFixedBuffer(request.crashId, "..\\outside");
    EXPECT_FALSE(CrashProtocol::IsCurrentCrashRequest(request));
}

TEST(Crash, AckAdmissionRequiresBoundedTerminatedIdentity){
    using namespace NWB::Core::Crash::CrashDetail;
    CrashRequest request;
    CrashAck ack;
    CopyFixedBuffer(request.crashId, "crash-1-1");
    CopyFixedBuffer(ack.crashId, "crash-1-1");
    EXPECT_TRUE(CrashProtocol::CrashAckMatchesRequest(ack, request));
    ack.packageWritten = 2u;
    EXPECT_FALSE(CrashProtocol::CrashAckMatchesRequest(ack, request));
    ack.packageWritten = 0u;
    ack.crashId.fill('x');
    EXPECT_FALSE(CrashProtocol::CrashAckMatchesRequest(ack, request));
    request.crashId.fill('x');
    EXPECT_FALSE(CrashProtocol::CrashAckMatchesRequest(ack, request));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
TEST(Crash, InstallRejectsUnrepresentableConfigurationBeforeCreatingSpool){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    const auto group = StringFormat(arena, "crash_install_text_{}", CurrentProcessId());
    const auto root = TestCaseDirectory(arena, group);
    const auto existing = IsDirectory(root);
    ASSERT_TRUE(existing);
    ASSERT_FALSE(*existing);
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = s_CrashTests;
    config.spoolDirectory = SpoolDirectory(arena, group);
    Array<char, NWB::Core::Crash::CrashDetail::s_MaxUrlText> longUrl;
    longUrl.fill('u');
    config.logServerUrl = AStringView(longUrl.data(), longUrl.size());
    const b8 installed = NWB::Core::Crash::InstallCrashHandler(config);
    if(installed)
        NWB::Core::Crash::UninstallCrashHandler();
    EXPECT_FALSE(installed);
    const auto rootAfter = IsDirectory(root);
    ASSERT_TRUE(rootAfter);
    EXPECT_FALSE(*rootAfter);

    const auto pathGroup = StringFormat(arena, "crash_install_path_{}", CurrentProcessId());
    const auto pathRoot = TestCaseDirectory(arena, pathGroup);
    const auto pathExisting = IsDirectory(pathRoot);
    ASSERT_TRUE(pathExisting);
    ASSERT_FALSE(*pathExisting);
    config.logServerUrl = {};
    config.spoolDirectory = SpoolDirectory(arena, pathGroup);
    Array<char, 96u> segment;
    segment.fill('p');
    for(u32 i = 0u; i < 12u; ++i)
        config.spoolDirectory /= AStringView(segment.data(), segment.size());
    const b8 longPathInstalled = NWB::Core::Crash::InstallCrashHandler(config);
    if(longPathInstalled)
        NWB::Core::Crash::UninstallCrashHandler();
    EXPECT_FALSE(longPathInstalled);
    const auto pathRootAfter = IsDirectory(pathRoot);
    ASSERT_TRUE(pathRootAfter);
    EXPECT_FALSE(*pathRootAfter);

    const auto nulGroup = StringFormat(arena, "crash_install_nul_{}", CurrentProcessId());
    const auto nulRoot = TestCaseDirectory(arena, nulGroup);
    const auto nulExisting = IsDirectory(nulRoot);
    ASSERT_TRUE(nulExisting);
    ASSERT_FALSE(*nulExisting);
    const Array<char, 3u> embedded{ 'a', '\0', 'b' };
    config.spoolDirectory = SpoolDirectory(arena, nulGroup);
    config.applicationName = AStringView(embedded.data(), embedded.size());
    const b8 nulInstalled = NWB::Core::Crash::InstallCrashHandler(config);
    if(nulInstalled)
        NWB::Core::Crash::UninstallCrashHandler();
    EXPECT_FALSE(nulInstalled);
    const auto nulRootAfter = IsDirectory(nulRoot);
    ASSERT_TRUE(nulRootAfter);
    EXPECT_FALSE(*nulRootAfter);

    const auto pathNulGroup = StringFormat(arena, "crash_install_path_nul_{}", CurrentProcessId());
    const auto pathNulRoot = TestCaseDirectory(arena, pathNulGroup);
    const auto pathNulExisting = IsDirectory(pathNulRoot);
    ASSERT_TRUE(pathNulExisting);
    ASSERT_FALSE(*pathNulExisting);
    config.applicationName = s_CrashTests;
    config.spoolDirectory = SpoolDirectory(arena, pathNulGroup);
    config.spoolDirectory /= AStringView(embedded.data(), embedded.size());
    const b8 pathNulInstalled = NWB::Core::Crash::InstallCrashHandler(config);
    if(pathNulInstalled)
        NWB::Core::Crash::UninstallCrashHandler();
    EXPECT_FALSE(pathNulInstalled);
    const auto pathNulRootAfter = IsDirectory(pathNulRoot);
    ASSERT_TRUE(pathNulRootAfter);
    EXPECT_FALSE(*pathNulRootAfter);

    const auto handlerNulGroup = StringFormat(arena, "crash_install_handler_nul_{}", CurrentProcessId());
    const auto handlerNulRoot = TestCaseDirectory(arena, handlerNulGroup);
    const auto handlerNulExisting = IsDirectory(handlerNulRoot);
    ASSERT_TRUE(handlerNulExisting);
    ASSERT_FALSE(*handlerNulExisting);
    config.spoolDirectory = SpoolDirectory(arena, handlerNulGroup);
    config.handlerExecutablePath = AStringView(embedded.data(), embedded.size());
    const b8 handlerNulInstalled = NWB::Core::Crash::InstallCrashHandler(config);
    if(handlerNulInstalled)
        NWB::Core::Crash::UninstallCrashHandler();
    EXPECT_FALSE(handlerNulInstalled);
    const auto handlerNulRootAfter = IsDirectory(handlerNulRoot);
    ASSERT_TRUE(handlerNulRootAfter);
    EXPECT_FALSE(*handlerNulRootAfter);
}

TEST(Crash, AttachmentFailureDoesNotClaimTheHandlerAcceptedARequest){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    const auto group = StringFormat(arena, "crash_attachment_admission_{}", CurrentProcessId());
    const auto existing = IsDirectory(TestCaseDirectory(arena, group));
    ASSERT_TRUE(existing);
    ASSERT_FALSE(*existing);
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = s_CrashTests;
    config.spoolDirectory = SpoolDirectory(arena, group);
    ASSERT_TRUE(NWB::Core::Crash::InstallCrashHandler(config));
    ScopeExit uninstall([]()noexcept{ NWB::Core::Crash::UninstallCrashHandler(); });

    const auto noDumpKind = NWB::Core::Crash::CaptureGpuCrashDump("report", "binary", NWB::Core::Crash::GpuCrashDumpKind::None);
    EXPECT_EQ(noDumpKind.status, NWB::Core::Crash::CrashDumpStatus::RequestFailed);
    EXPECT_FALSE(noDumpKind.requestAccepted());
    const auto invalidDumpKind = NWB::Core::Crash::CaptureGpuCrashDump("report", "binary", static_cast<NWB::Core::Crash::GpuCrashDumpKind::Enum>(255u));
    EXPECT_EQ(invalidDumpKind.status, NWB::Core::Crash::CrashDumpStatus::RequestFailed);
    EXPECT_FALSE(invalidDumpKind.requestAccepted());
    const Array<NWB::Core::Crash::GpuCrashDumpKind::Enum, 2u> kinds{ NWB::Core::Crash::GpuCrashDumpKind::Aftermath, NWB::Core::Crash::GpuCrashDumpKind::RadeonGpuDetective };
    const Array<AStringView, 2u> names{ CrashNames::s_AftermathGpuDumpFileName, CrashNames::s_GpuDetectiveCaptureFileName };
    const Array<u8, 4u> binary{ 0x61u, 0x00u, 0x62u, 0xffu };
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    for(usize index = 0u; index < kinds.size(); ++index){
        Array<char, NWB::Core::Crash::CrashDetail::s_MaxShortText> crashId = {};
        const u64 sequence = NWB::Core::Crash::CrashDetail::g_State.crashSequence.load(MemoryOrder::relaxed);
        BuildCrashIdForProcess(CurrentProcessId(), sequence, crashId);
        const auto captured = NWB::Core::Crash::CaptureGpuCrashDump("report", AStringView(reinterpret_cast<const char*>(binary.data()), binary.size()), kinds[index]);
        ASSERT_TRUE(captured.packageWritten());
        Vector<u8, NWB::Core::Alloc::ScratchArena> data(scratchArena);
        ASSERT_TRUE(ReadBinaryFile(PackageDirectory(arena, group, CrashNames::s_PendingDirectoryName, FixedBufferView(crashId)) / names[index], data));
        ASSERT_EQ(data.size(), binary.size());
        for(usize byte = 0u; byte < binary.size(); ++byte)
            EXPECT_EQ(data[byte], binary[byte]);
    }

    ASSERT_TRUE(NWB::Core::Crash::SetCrashUploadDestination("http://localhost", "token"));
    Array<char, NWB::Core::Crash::CrashDetail::s_MaxUrlText> longUrl;
    longUrl.fill('u');
    Array<char, NWB::Core::Crash::CrashDetail::s_MaxMediumText> longToken;
    longToken.fill('t');
    const Array<char, 3u> embedded{ 'a', '\0', 'b' };
    ASSERT_TRUE(NWB::Core::Crash::SetCrashUploadDestination(AStringView(longUrl.data(), longUrl.size() - 1u), AStringView(longToken.data(), longToken.size() - 1u)));
    ASSERT_TRUE(NWB::Core::Crash::SetCrashUploadDestination("http://localhost", "token"));
    EXPECT_FALSE(NWB::Core::Crash::SetCrashUploadDestination(AStringView(longUrl.data(), longUrl.size()), "new"));
    EXPECT_FALSE(NWB::Core::Crash::SetCrashUploadDestination("new", AStringView(longToken.data(), longToken.size())));
    EXPECT_FALSE(NWB::Core::Crash::SetCrashUploadDestination(AStringView(embedded.data(), embedded.size()), "new"));
    const auto snapshot = NWB::Core::Crash::CrashDetail::BuildCrashDumpRequest(NWB::Core::Crash::CrashDetail::CrashReasonKind::ManualDump, 0u, {});
    EXPECT_EQ(FixedBufferView(snapshot.logServerUrl), AStringView("http://localhost"));
    EXPECT_EQ(FixedBufferView(snapshot.crashUploadToken), AStringView("token"));

    const auto blockNextPackage = [&](){
        Array<char, NWB::Core::Crash::CrashDetail::s_MaxShortText> crashId = {};
        const u64 sequence = NWB::Core::Crash::CrashDetail::g_State.crashSequence.load(MemoryOrder::relaxed);
        BuildCrashIdForProcess(CurrentProcessId(), sequence, crashId);
        return WriteTextFile(PackageDirectory(arena, group, CrashNames::s_PendingDirectoryName, FixedBufferView(crashId)), AStringView("block package directory"));
    };
    ASSERT_TRUE(blockNextPackage());
    const auto attachmentFailure = NWB::Core::Crash::CaptureGpuCrashDump("gpu report", {}, NWB::Core::Crash::GpuCrashDumpKind::None);
    EXPECT_EQ(attachmentFailure.status, NWB::Core::Crash::CrashDumpStatus::AttachmentWriteFailed);
    EXPECT_FALSE(attachmentFailure.requestAccepted());

    ASSERT_TRUE(blockNextPackage());
    const auto handlerFailure = NWB::Core::Crash::CaptureCrashDump("manual", "blocked handler package");
    EXPECT_EQ(handlerFailure.status, NWB::Core::Crash::CrashDumpStatus::PackageWriteFailed);
    EXPECT_TRUE(handlerFailure.requestAccepted());
}

TEST(Crash, ConcurrentManualCaptureDoesNotClaimAnUnsentRequestWasQueued){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    const auto group = StringFormat(arena, "crash_manual_contention_{}", CurrentProcessId());
    const auto existing = IsDirectory(TestCaseDirectory(arena, group));
    ASSERT_TRUE(existing);
    ASSERT_FALSE(*existing);
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = s_CrashTests;
    config.spoolDirectory = SpoolDirectory(arena, group);
    ASSERT_TRUE(NWB::Core::Crash::InstallCrashHandler(config));
    ScopeExit uninstall([]()noexcept{ NWB::Core::Crash::UninstallCrashHandler(); });

    const HANDLE handlerThread = NWB::Core::Crash::CrashDetail::g_State.handlerProcessInfo.hThread;
    ASSERT_NE(handlerThread, nullptr);
    const DWORD previousSuspendCount = SuspendThread(handlerThread);
    ASSERT_NE(previousSuspendCount, static_cast<DWORD>(-1));
    ScopeExit resume([handlerThread]()noexcept{
        if(ResumeThread(handlerThread) == static_cast<DWORD>(-1))
            TerminateInvariant();
    });
    ASSERT_EQ(previousSuspendCount, 0u);
    NWB::Core::Crash::CrashDumpResult first;
    JoiningThread writer([&](){ first = NWB::Core::Crash::CaptureCrashDump("first", "held in transport"); });
    for(u32 attempt = 0u; attempt < 2000u && NWB::Core::Crash::CrashDetail::g_State.transportInFlight.load(MemoryOrder::acquire) == 0u; ++attempt)
        SleepMS(1u);
    const b8 active = NWB::Core::Crash::CrashDetail::g_State.transportInFlight.load(MemoryOrder::acquire) != 0u;
    NWB::Core::Crash::CrashDumpResult second;
    const u32 suppressedBefore = NWB::Core::Crash::CrashDetail::g_State.suppressedPlatformCrashCaptures.load(MemoryOrder::acquire);
    if(active){
        second = NWB::Core::Crash::CaptureCrashDump("second", "transport already owned");
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Fatal, .category = "contention", .message = "not sent", .terminatesProcess = true });
    }
    const u32 suppressedWhileBusy = NWB::Core::Crash::CrashDetail::g_State.suppressedPlatformCrashCaptures.load(MemoryOrder::acquire);
    const DWORD resumeResult = ResumeThread(handlerThread);
    resume.release();
    writer.join();
    ASSERT_NE(resumeResult, static_cast<DWORD>(-1));
    ASSERT_TRUE(active);
    EXPECT_TRUE(first.packageWritten());
    EXPECT_EQ(second.status, NWB::Core::Crash::CrashDumpStatus::RequestFailed);
    EXPECT_FALSE(second.requestAccepted());
    EXPECT_EQ(suppressedWhileBusy, suppressedBefore);
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Fatal, .category = "available", .message = "sent", .terminatesProcess = true });
    EXPECT_EQ(NWB::Core::Crash::CrashDetail::g_State.suppressedPlatformCrashCaptures.load(MemoryOrder::acquire), suppressedBefore + 1u);
    EXPECT_TRUE(NWB::Core::Crash::CrashDetail::TryConsumeSuppressedPlatformCrashCapture());
}

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
    ASSERT_TRUE(NWB::Core::Crash::CrashDetail::CrashProtocol::IsCurrentCrashRequest(request));
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
    });
    ASSERT_EQ(previousSuspendCount, 0u);
    const auto transport = NWB::Core::Crash::CrashDetail::RequestCrashHandler(request, 1u);
    EXPECT_EQ(transport, NWB::Core::Crash::CrashDetail::CrashDumpTransportStatus::TimedOut);
    ASSERT_NE(ResumeThread(handlerThread), static_cast<DWORD>(-1));
    resume.release();
    ASSERT_EQ(WaitForSingleObject(NWB::Core::Crash::CrashDetail::g_State.crashHandledEvent, NWB::Core::Crash::CrashDetail::s_ManualCrashDumpWaitMilliseconds), WAIT_OBJECT_0);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(request.crashId.data()));
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
    ASSERT_TRUE(NWB::Core::Crash::CrashDetail::CrashProtocol::IsCurrentCrashRequest(request));
    request.version = 1u;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::CrashProtocol::IsCurrentCrashRequest(request));
    request.version = NWB::Core::Crash::CrashDetail::s_RequestVersion + 1u;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::CrashProtocol::IsCurrentCrashRequest(request));
    request.version = NWB::Core::Crash::CrashDetail::s_RequestVersion;
    --request.manualContextSize;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::CrashProtocol::IsCurrentCrashRequest(request));
    request.manualContextSize = sizeof(CONTEXT);
    request.manualContextArchitecture ^= 0x00010000u;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::CrashProtocol::IsCurrentCrashRequest(request));
    request.manualContextArchitecture = NWB::Core::Crash::CrashDetail::s_WindowsContextArchitecture;
    request.manualExceptionRecord.ExceptionRecord = &storage.exceptionRecord;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::CrashProtocol::IsCurrentCrashRequest(request));
    request.manualExceptionRecord.ExceptionRecord = nullptr;
    request.manualContext.ContextFlags |= NWB::Core::Crash::CrashDetail::s_WindowsExtendedContextFlag;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::CrashProtocol::IsCurrentCrashRequest(request));
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

    const b8 installed = NWB::Core::Crash::InstallCrashHandler(config);
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

    const b8 installed = NWB::Core::Crash::InstallCrashHandler(config);
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

    Array<char, NWB::Core::Crash::CrashDetail::s_MaxShortText> crashId = {};
    BuildCrashIdForProcess(CurrentProcessId(), 1u, crashId);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, FixedBufferView(crashId));
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
    // Frame #1 proves unwinding walked past the leaf frame without frame pointers.
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

    const b8 installed = NWB::Core::Crash::InstallCrashHandler(config);
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

    Array<char, NWB::Core::Crash::CrashDetail::s_MaxShortText> crashId = {};
    BuildCrashIdForProcess(CurrentProcessId(), 1u, crashId);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, FixedBufferView(crashId));
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

        Array<char, NWB::Core::Crash::CrashDetail::s_MaxShortText> crashId = {};
        BuildCrashIdForProcess(static_cast<u32>(childPid), 1u, crashId);

        const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, FixedBufferView(crashId));
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
        // The faulting callstack must reach past the leaf frame without frame pointers.
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

