// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/test_context.h>

#include <core/crash/internal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using TestArena = NWB::Tests::TestArena<struct CrashTestsTag>;
using CrashTestPath = Path<NWB::Core::Alloc::GlobalArena>;

#define NWB_CRASH_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CrashTestPath CrashRootDirectory(NWB::Core::Alloc::GlobalArena& arena){
    CrashTestPath executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / "crashes";

    return CrashTestPath(arena, "crashes");
}

[[nodiscard]] static CrashTestPath TestRootDirectory(NWB::Core::Alloc::GlobalArena& arena){
    return CrashRootDirectory(arena) / "test_storage";
}

[[nodiscard]] static CrashTestPath TestCaseDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestRootDirectory(arena) / testGroup;
}

[[nodiscard]] static CrashTestPath SpoolDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestCaseDirectory(arena, testGroup) / "spool";
}

[[nodiscard]] static CrashTestPath BucketDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName){
    return SpoolDirectory(arena, testGroup) / bucketName;
}

[[nodiscard]] static CrashTestPath PackageDirectory(
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView testGroup,
    const AStringView bucketName,
    const AStringView packageName
){
    return BucketDirectory(arena, testGroup, bucketName) / packageName;
}

static void RemoveTestArtifacts(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    ErrorCode error;
    static_cast<void>(RemoveAllIfExists(TestCaseDirectory(arena, testGroup), error));
}

[[nodiscard]] static bool CreatePackageDirectory(
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView testGroup,
    const AStringView bucketName,
    const AStringView packageName
){
    const CrashTestPath packageDirectory = PackageDirectory(arena, testGroup, bucketName, packageName);
    ErrorCode error;
    static_cast<void>(EnsureDirectories(packageDirectory, error));
    if(error)
        return false;

    return WriteTextFile(packageDirectory / "marker.txt", AStringView("package"));
}

[[nodiscard]] static bool DirectoryExists(const CrashTestPath& path){
    ErrorCode error;
    return IsDirectory(path, error) && !error;
}

[[nodiscard]] static bool PathIsMissing(const CrashTestPath& path){
    ErrorCode error;
    return !FileExists(path, error) && !error;
}

[[nodiscard]] static bool PathIsFile(const CrashTestPath& path){
    ErrorCode error;
    return FileExists(path, error) && !error;
}

[[nodiscard]] static bool TextFileContains(const CrashTestPath& path, const AStringView needle){
    AString<NWB::Core::Alloc::GlobalArena> text(path.arena());
    if(!ReadTextFile(path, text))
        return false;

    return AStringView(text.data(), text.size()).find(needle) != AStringView::npos;
}

template<usize N>
static void CopyPathText(NWB::Core::Alloc::GlobalArena& arena, char (&outText)[N], const CrashTestPath& path){
    const AString<NWB::Core::Alloc::GlobalArena> text = PathToString<char>(arena, path);
    CopyFixedBuffer(outText, AStringView(text.data(), text.size()));
}

[[nodiscard]] static NWB::Core::Crash::Detail::PlatformKind::Enum CurrentCrashPlatformKind()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return NWB::Core::Crash::Detail::PlatformKind::Windows;
#elif defined(NWB_PLATFORM_ANDROID)
    return NWB::Core::Crash::Detail::PlatformKind::Android;
#elif defined(NWB_PLATFORM_LINUX)
    return NWB::Core::Crash::Detail::PlatformKind::Linux;
#else
    return NWB::Core::Crash::Detail::PlatformKind::Unknown;
#endif
}

static void FillPackageRequest(
    NWB::Core::Alloc::GlobalArena& arena,
    NWB::Core::Crash::Detail::CrashRequest& request,
    const CrashTestPath& spoolDirectory,
    const AStringView crashId
){
    request = NWB::Core::Crash::Detail::CrashRequest{};
    request.platform = CurrentCrashPlatformKind();
    request.reasonKind = NWB::Core::Crash::Detail::CrashReasonKind::ManualDump;
    request.processId = CurrentProcessId();
    request.threadId = CurrentThreadId();
    CopyFixedBuffer(request.crashId, crashId);
    CopyFixedBuffer(request.applicationName, AStringView("crash_tests"));
    CopyFixedBuffer(request.versionText, AStringView("1"));
    CopyFixedBuffer(request.buildId, AStringView("test"));
    CopyFixedBuffer(request.abi, CurrentAbiName());
    CopyPathText(arena, request.spoolDirectory, spoolDirectory);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestWriteCrashPackageCreatesRequiredFiles(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_package_write_test");
    constexpr AStringView s_CrashId("crash-package-required-files");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Core::Crash::Detail::CrashRequest request;
    FillPackageRequest(arena, request, SpoolDirectory(arena, s_Group), s_CrashId);
    request.instructionPointer = 1u;
    request.triggerLine = 7u;
    CopyFixedBuffer(request.triggerCategory, AStringView("test"));
    CopyFixedBuffer(request.triggerMessage, AStringView("required-files"));
    CopyFixedBuffer(request.triggerFile, AStringView("tests/crash/tests.cpp"));

    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::Detail::WriteCrashPackage(request));

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, "pending", s_CrashId);
    NWB_CRASH_TEST_CHECK(context, PathIsFile(packageDirectory / "manifest.json"));
    NWB_CRASH_TEST_CHECK(context, PathIsFile(packageDirectory / "metadata.txt"));
    NWB_CRASH_TEST_CHECK(context, PathIsFile(packageDirectory / "breadcrumbs.txt"));
    NWB_CRASH_TEST_CHECK(context, PathIsFile(packageDirectory / "emergency.txt"));
    NWB_CRASH_TEST_CHECK(context, PathIsFile(packageDirectory / "artifact_strategy.txt"));
    NWB_CRASH_TEST_CHECK(context, PathIsFile(packageDirectory / "cpu_context.txt"));
    NWB_CRASH_TEST_CHECK(context, PathIsFile(packageDirectory / "trigger.txt"));
    NWB_CRASH_TEST_CHECK(context, PathIsFile(packageDirectory / "symbolication.txt"));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestWriteCrashPackageFailsWhenSpoolPathIsFile(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_package_bad_spool_test");
    RemoveTestArtifacts(arena, s_Group);

    ErrorCode error;
    static_cast<void>(EnsureDirectories(TestCaseDirectory(arena, s_Group), error));
    NWB_CRASH_TEST_CHECK(context, !error);
    const CrashTestPath spoolPath = SpoolDirectory(arena, s_Group);
    NWB_CRASH_TEST_CHECK(context, WriteTextFile(spoolPath, AStringView("not a directory")));

    NWB::Core::Crash::Detail::CrashRequest request;
    FillPackageRequest(arena, request, spoolPath, AStringView("crash-package-bad-spool"));
    NWB_CRASH_TEST_CHECK(context, !NWB::Core::Crash::Detail::WriteCrashPackage(request));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashRequestCarriesSpoolRetention(TestContext& context){
    NWB::Core::Crash::Detail::CrashRequest request;
    {
        ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
        const NWB::Core::Crash::CrashSpoolRetentionConfig previousRetention = NWB::Core::Crash::Detail::g_State.spoolRetention;

        NWB::Core::Crash::Detail::g_State.spoolRetention.maxPendingPackages = 7u;
        NWB::Core::Crash::Detail::g_State.spoolRetention.maxUploadedPackages = 8u;
        NWB::Core::Crash::Detail::g_State.spoolRetention.maxFailedPackages = 9u;
        NWB::Core::Crash::Detail::g_State.spoolRetention.maxUploadingPackages = 10u;
        NWB::Core::Crash::Detail::SnapshotCrashState(request, NWB::Core::Crash::Detail::CrashReasonKind::ManualDump, 0u);
        NWB::Core::Crash::Detail::g_State.spoolRetention = previousRetention;
    }

    NWB_CRASH_TEST_CHECK(context, request.spoolRetention.maxPendingPackages == 7u);
    NWB_CRASH_TEST_CHECK(context, request.spoolRetention.maxUploadedPackages == 8u);
    NWB_CRASH_TEST_CHECK(context, request.spoolRetention.maxFailedPackages == 9u);
    NWB_CRASH_TEST_CHECK(context, request.spoolRetention.maxUploadingPackages == 10u);
}

static void TestCrashBreadcrumbPersistsCurrentRing(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena breadcrumbArena(
        NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u),
        "NWB::Tests::Crash::BreadcrumbArena"
    );
    constexpr AStringView s_Group("crash_breadcrumb_persist_test");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    ErrorCode error;
    static_cast<void>(EnsureDirectories(spoolDirectory, error));
    NWB_CRASH_TEST_CHECK(context, !error);

    char previousSpoolDirectory[NWB::Core::Crash::Detail::s_MaxPathText] = {};
    NWB::Core::Crash::Detail::FixedBreadcrumb previousBreadcrumbs[NWB::Core::Crash::Detail::s_MaxBreadcrumbs] = {};
    usize previousNextBreadcrumb = 0u;
    const u64 previousBreadcrumbOrder = NWB::Core::Crash::Detail::g_State.breadcrumbOrder.load(MemoryOrder::relaxed);

    {
        ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
        CopyFixedBuffer(previousSpoolDirectory, NWB::Core::Crash::Detail::g_State.spoolDirectoryText);
        previousNextBreadcrumb = NWB::Core::Crash::Detail::g_State.nextBreadcrumb;
        for(usize i = 0u; i < NWB::Core::Crash::Detail::s_MaxBreadcrumbs; ++i)
            previousBreadcrumbs[i] = NWB::Core::Crash::Detail::g_State.breadcrumbs[i];

        CopyPathText(arena, NWB::Core::Crash::Detail::g_State.spoolDirectoryText, spoolDirectory);
        NWB::Core::Crash::Detail::g_State.nextBreadcrumb = 0u;
        for(NWB::Core::Crash::Detail::FixedBreadcrumb& breadcrumb : NWB::Core::Crash::Detail::g_State.breadcrumbs)
            breadcrumb = NWB::Core::Crash::Detail::FixedBreadcrumb{};
        NWB::Core::Crash::Detail::g_State.breadcrumbOrder.store(1u, MemoryOrder::relaxed);
    }

    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::AddCrashBreadcrumb(breadcrumbArena, AStringView("test"), AStringView("persisted breadcrumb")));
    NWB_CRASH_TEST_CHECK(context, TextFileContains(spoolDirectory / "breadcrumbs_current.txt", AStringView("[test] persisted breadcrumb")));

    {
        ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
        CopyFixedBuffer(NWB::Core::Crash::Detail::g_State.spoolDirectoryText, AStringView(previousSpoolDirectory));
        NWB::Core::Crash::Detail::g_State.nextBreadcrumb = previousNextBreadcrumb;
        for(usize i = 0u; i < NWB::Core::Crash::Detail::s_MaxBreadcrumbs; ++i)
            NWB::Core::Crash::Detail::g_State.breadcrumbs[i] = previousBreadcrumbs[i];
        NWB::Core::Crash::Detail::g_State.breadcrumbOrder.store(previousBreadcrumbOrder, MemoryOrder::relaxed);
    }

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashSpoolRetentionPrunesOldestPackages(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "pending", "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "pending", "crash-002"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "pending", "crash-003"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "pending", "bad package name"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "uploaded", "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "uploaded", "crash-002"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "failed", "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "failed", "crash-002"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "uploading", "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "uploading", "crash-002"));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 2u;
    retention.maxUploadedPackages = 1u;
    retention.maxFailedPackages = 1u;
    retention.maxUploadingPackages = 1u;
    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(arena, SpoolDirectory(arena, s_Group), retention));

    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, "pending", "crash-001")));
    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "pending", "crash-002")));
    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "pending", "crash-003")));
    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "pending", "bad package name")));
    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, "uploaded", "crash-001")));
    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "uploaded", "crash-002")));
    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, "failed", "crash-001")));
    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "failed", "crash-002")));
    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, "uploading", "crash-001")));
    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "uploading", "crash-002")));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashSpoolRetentionZeroDisablesPruning(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_zero_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "pending", "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "pending", "crash-002"));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 0u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(arena, SpoolDirectory(arena, s_Group), retention));

    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "pending", "crash-001")));
    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "pending", "crash-002")));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashSpoolRetentionProtectsActivePendingPackage(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_protect_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "pending", "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "pending", "crash-002"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "pending", "crash-003"));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 1u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(
        arena,
        SpoolDirectory(arena, s_Group),
        retention,
        AStringView("crash-001")
    ));

    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "pending", "crash-001")));
    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, "pending", "crash-002")));
    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "pending", "crash-003")));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestFlushReportsFailsWhenUploadingRecoveryIsBlocked(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_uploading_recovery_blocked_test");
    constexpr AStringView s_CrashId("crash-recovery-blocked");
    RemoveTestArtifacts(arena, s_Group);

    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, "uploading", s_CrashId));
    NWB_CRASH_TEST_CHECK(context, WriteTextFile(BucketDirectory(arena, s_Group, "pending"), AStringView("blocked")));

    NWB::Core::Crash::Detail::CrashUploadSnapshot snapshot;
    CopyPathText(arena, snapshot.spoolDirectory, SpoolDirectory(arena, s_Group));
    CopyFixedBuffer(snapshot.logServerUrl, AStringView("http://127.0.0.1:1"));

    NWB_CRASH_TEST_CHECK(context, !NWB::Core::Crash::Detail::FlushPendingCrashReportsImpl(arena, snapshot));
    NWB_CRASH_TEST_CHECK(context, DirectoryExists(PackageDirectory(arena, s_Group, "uploading", s_CrashId)));
    NWB_CRASH_TEST_CHECK(context, PathIsFile(BucketDirectory(arena, s_Group, "pending")));

    RemoveTestArtifacts(arena, s_Group);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_CRASH_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("crash", [](NWB::Tests::TestContext& context){
    __hidden_crash_tests::TestWriteCrashPackageCreatesRequiredFiles(context);
    __hidden_crash_tests::TestWriteCrashPackageFailsWhenSpoolPathIsFile(context);
    __hidden_crash_tests::TestCrashRequestCarriesSpoolRetention(context);
    __hidden_crash_tests::TestCrashBreadcrumbPersistsCurrentRing(context);
    __hidden_crash_tests::TestCrashSpoolRetentionPrunesOldestPackages(context);
    __hidden_crash_tests::TestCrashSpoolRetentionZeroDisablesPruning(context);
    __hidden_crash_tests::TestCrashSpoolRetentionProtectsActivePendingPackage(context);
    __hidden_crash_tests::TestFlushReportsFailsWhenUploadingRecoveryIsBlocked(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

