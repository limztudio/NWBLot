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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_CRASH_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("crash", [](NWB::Tests::TestContext& context){
    __hidden_crash_tests::TestCrashSpoolRetentionPrunesOldestPackages(context);
    __hidden_crash_tests::TestCrashSpoolRetentionZeroDisablesPruning(context);
    __hidden_crash_tests::TestCrashSpoolRetentionProtectsActivePendingPackage(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

