// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_spool_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace CrashTestSupport;
using namespace NWB::Tests;
using CrashTestSupport::TestArena;
using namespace NWB::Tests::CrashTestPaths;
namespace CrashNames = NWB::Core::Crash::PackageNames;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Crash, WriteCrashPackageFailsWhenSpoolPathIsFile){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_package_bad_spool_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(EnsureDirectories(TestCaseDirectory(arena, s_Group)));
    const CrashTestPath spoolPath = SpoolDirectory(arena, s_Group);
    EXPECT_TRUE(WriteTextFile(spoolPath, AStringView("not a directory")));

    NWB::Core::Crash::CrashDetail::CrashRequest request;
    FillPackageRequest(spoolPath, AStringView("crash-package-bad-spool"), arena, request);
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::WriteCrashPackage(request));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, CrashSpoolRetentionPrunesOldestPackages){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash003));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "bad package name"));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_Crash002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_Crash002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_Crash002));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 2u;
    retention.maxUploadedPackages = 1u;
    retention.maxFailedPackages = 1u;
    retention.maxUploadingPackages = 1u;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    EXPECT_TRUE(NWB::Core::Crash::CrashDetail::ApplyCrashSpoolRetention(scratchArena, SpoolDirectory(arena, s_Group), retention));

    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash003));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "bad package name"));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, CrashSpoolRetentionZeroDisablesPruning){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_zero_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 0u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    EXPECT_TRUE(NWB::Core::Crash::CrashDetail::ApplyCrashSpoolRetention(scratchArena, SpoolDirectory(arena, s_Group), retention));

    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, CrashSpoolRetentionProtectsActivePendingPackage){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_protect_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash003));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 1u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    EXPECT_TRUE(NWB::Core::Crash::CrashDetail::ApplyCrashSpoolRetention(scratchArena, SpoolDirectory(arena, s_Group), retention, AStringView("crash-001")));

    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash003));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, FlushReportsFailsWhenUploadingRecoveryIsBlocked){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_uploading_recovery_blocked_test");
    constexpr AStringView s_CrashId("crash-recovery-blocked");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId));
    EXPECT_TRUE(WriteTextFile(BucketDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName), AStringView("blocked")));

    NWB::Core::Crash::CrashDetail::CrashUploadSnapshot snapshot;
    CopyPathText(SpoolDirectory(arena, s_Group), arena, snapshot.spoolDirectory);
    CopyFixedBuffer(snapshot.logServerUrl, AStringView("http://127.0.0.1:1"));

    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::FlushPendingCrashReportsImpl(arena, snapshot));
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsRegularFile(BucketDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    EXPECT_TRUE(TextFileContains(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId) / CrashNames::s_UploadAttemptFileName, AStringView(CrashNames::s_UploadAttemptRetryInterruptedState)));

    RemoveTestArtifacts(arena, s_Group);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
