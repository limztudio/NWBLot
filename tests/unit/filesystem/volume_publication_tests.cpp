// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_test_fixture.h"

#include <core/filesystem/factory.h>
#include <core/filesystem/volume_file_system.h>
#include <core/filesystem/volume_staging.h>
#include <core/filesystem/volume_naming.h>
#include <core/filesystem/volume_storage_detail.h>

#include <tests/common/test_context.h>
#include <tests/common/capturing_logger.h>

#include <global/filesystem.h>
#include <global/binary.h>
#include <global/filesystem/cleanup.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem_publication_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Filesystem;
using Path = NWB::Path;
using NWB::Tests::FilesystemTest::BlockingRestoreLogger;
using NWB::Tests::FilesystemTest::FilesystemVolumeTest;
using NWB::Tests::FilesystemTest::ThrowingPublishLogger;

inline constexpr Name s_TestArena("tests/filesystem");
constexpr AStringView s_StageDirectory = "stage";
constexpr AStringView s_BackupDirectory = "backup";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(FilesystemVolumeTest, EarlyPublicationFailureCleansOwnedWorkDirectories){
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    NWB::Core::Filesystem::StagedDirectoryPaths paths(m_arena);
    paths.stageDirectory = m_directory / s_StageDirectory;
    paths.backupDirectory = m_directory / s_BackupDirectory;
    ASSERT_TRUE(EnsureDirectories(paths.stageDirectory));
    ASSERT_TRUE(EnsureDirectories(paths.backupDirectory));
    {
        StagedDirectoryCleanupGuard stageCleanup(paths.stageDirectory, "test");
        StagedDirectoryCleanupGuard backupCleanup(paths.backupDirectory, "test", s_BackupDirectoryLabel);
        EXPECT_FALSE(PublishStagedVolume(paths, m_directory, m_desc.volumeName.view(), 0u, scratchArena, {}, &stageCleanup, &backupCleanup));
    }
    const auto stageExists = FileExists(paths.stageDirectory);
    const auto backupExists = FileExists(paths.backupDirectory);
    ASSERT_TRUE(stageExists);
    ASSERT_TRUE(backupExists);
    EXPECT_FALSE(*stageExists);
    EXPECT_FALSE(*backupExists);
}

TEST_F(FilesystemVolumeTest, IncompletePublicationRollbackPreservesRecoveryFiles){
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    NWB::Core::Filesystem::StagedDirectoryPaths paths(m_arena);
    paths.stageDirectory = m_directory / s_StageDirectory;
    paths.backupDirectory = m_directory / s_BackupDirectory;
    ASSERT_TRUE(EnsureDirectories(paths.stageDirectory));
    ASSERT_TRUE(EnsureDirectories(paths.backupDirectory));
    const Path output = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const Path backup = MakeVolumeSegmentPath(paths.backupDirectory, m_desc.volumeName.view(), 0u);
    const Array<u8, 3u> oldBytes{ 1u, 2u, 3u };
    ASSERT_TRUE(WriteBinaryFile(output, oldBytes));
    b8 rollbackFailed = false;
    const VolumePublicationObserver observer{
        .context = &rollbackFailed,
        .rollbackFailed = [](void* context)noexcept{ *static_cast<b8*>(context) = true; },
    };
    {
        StagedDirectoryCleanupGuard stageCleanup(paths.stageDirectory, "test");
        StagedDirectoryCleanupGuard backupCleanup(paths.backupDirectory, "test", s_BackupDirectoryLabel);
        BlockingRestoreLogger logger(m_logger.arena(), output);
        NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
        EXPECT_FALSE(PublishStagedVolume(paths, m_directory, m_desc.volumeName.view(), 1u, scratchArena, observer, &stageCleanup, &backupCleanup));
        EXPECT_TRUE(logger.blocked());
    }
    EXPECT_TRUE(rollbackFailed);
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(backup, loaded));
    ASSERT_EQ(loaded.size(), oldBytes.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), oldBytes.data(), oldBytes.size()), 0);
    const auto stageExists = IsDirectory(paths.stageDirectory);
    ASSERT_TRUE(stageExists);
    EXPECT_TRUE(*stageExists);
}

TEST_F(FilesystemVolumeTest, ThrowingDiagnosticStillRetainsIncompleteRollback){
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    NWB::Core::Filesystem::StagedDirectoryPaths paths(m_arena);
    paths.stageDirectory = m_directory / s_StageDirectory;
    paths.backupDirectory = m_directory / s_BackupDirectory;
    ASSERT_TRUE(EnsureDirectories(paths.stageDirectory));
    ASSERT_TRUE(EnsureDirectories(paths.backupDirectory));
    const Path output = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const Path backup = MakeVolumeSegmentPath(paths.backupDirectory, m_desc.volumeName.view(), 0u);
    const Array<u8, 3u> oldBytes{ 1u, 2u, 3u };
    ASSERT_TRUE(WriteBinaryFile(output, oldBytes));
    b8 rollbackFailed = false;
    const VolumePublicationObserver observer{
        .context = &rollbackFailed,
        .rollbackFailed = [](void* context)noexcept{ *static_cast<b8*>(context) = true; },
    };
    {
        StagedDirectoryCleanupGuard stageCleanup(paths.stageDirectory, "test");
        StagedDirectoryCleanupGuard backupCleanup(paths.backupDirectory, "test", s_BackupDirectoryLabel);
        BlockingRestoreLogger logger(m_logger.arena(), output, true);
        NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
        EXPECT_THROW(PublishStagedVolume(paths, m_directory, m_desc.volumeName.view(), 1u, scratchArena, observer, &stageCleanup, &backupCleanup), i32);
        EXPECT_TRUE(logger.blocked());
    }
    EXPECT_TRUE(rollbackFailed);
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(backup, loaded));
    ASSERT_EQ(loaded.size(), oldBytes.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), oldBytes.data(), oldBytes.size()), 0);
    const auto stageExists = IsDirectory(paths.stageDirectory);
    ASSERT_TRUE(stageExists);
    EXPECT_TRUE(*stageExists);
}

TEST_F(FilesystemVolumeTest, FailedLaterSegmentPromotionRestoresPublishedBytes){
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    NWB::Core::Filesystem::StagedDirectoryPaths stagedPaths(m_arena);
    stagedPaths.stageDirectory = m_directory / s_StageDirectory;
    stagedPaths.backupDirectory = m_directory / s_BackupDirectory;
    ASSERT_TRUE(EnsureDirectories(stagedPaths.stageDirectory));
    const Path firstOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const Path secondOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 1u);
    const Path absentOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 2u);
    const Path firstStaged = MakeVolumeSegmentPath(stagedPaths.stageDirectory, m_desc.volumeName.view(), 0u);
    const Array<u8, 3u> firstOld{ 1u, 2u, 3u };
    const Array<u8, 4u> secondOld{ 4u, 5u, 6u, 7u };
    const Array<u8, 2u> replacement{ 8u, 9u };
    ASSERT_TRUE(WriteBinaryFile(firstOutput, firstOld));
    ASSERT_TRUE(WriteBinaryFile(secondOutput, secondOld));
    ASSERT_TRUE(WriteBinaryFile(firstStaged, replacement));

    EXPECT_FALSE(PublishStagedVolume(stagedPaths, m_directory, m_desc.volumeName.view(), 3u, scratchArena));
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("failed to promote staged segment")));
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(firstOutput, loaded));
    ASSERT_EQ(loaded.size(), firstOld.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), firstOld.data(), firstOld.size()), 0);
    ASSERT_TRUE(ReadBinaryFile(secondOutput, loaded));
    ASSERT_EQ(loaded.size(), secondOld.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), secondOld.data(), secondOld.size()), 0);
    const auto absent = FileExists(absentOutput);
    ASSERT_TRUE(absent);
    EXPECT_FALSE(*absent);
    const auto backup = FileExists(stagedPaths.backupDirectory);
    ASSERT_TRUE(backup);
    EXPECT_FALSE(*backup);
}

TEST_F(FilesystemVolumeTest, ThrowingPromotionDiagnosticRestoresPublishedBytesDuringUnwind){
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    NWB::Core::Filesystem::StagedDirectoryPaths stagedPaths(m_arena);
    stagedPaths.stageDirectory = m_directory / s_StageDirectory;
    stagedPaths.backupDirectory = m_directory / s_BackupDirectory;
    ASSERT_TRUE(EnsureDirectories(stagedPaths.stageDirectory));
    const Path firstOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const Path secondOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 1u);
    const Path firstStaged = MakeVolumeSegmentPath(stagedPaths.stageDirectory, m_desc.volumeName.view(), 0u);
    const Array<u8, 3u> firstOld{ 1u, 2u, 3u };
    const Array<u8, 4u> secondOld{ 4u, 5u, 6u, 7u };
    const Array<u8, 2u> replacement{ 8u, 9u };
    ASSERT_TRUE(WriteBinaryFile(firstOutput, firstOld));
    ASSERT_TRUE(WriteBinaryFile(secondOutput, secondOld));
    ASSERT_TRUE(WriteBinaryFile(firstStaged, replacement));

    {
        ThrowingPublishLogger throwingLogger(m_logger.arena());
        NWB::Core::Common::LoggerRegistrationGuard throwingLoggerGuard(throwingLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
        EXPECT_THROW({ const b8 published = PublishStagedVolume(stagedPaths, m_directory, m_desc.volumeName.view(), 3u, scratchArena); EXPECT_FALSE(published); }, i32);
    }
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(firstOutput, loaded));
    ASSERT_EQ(loaded.size(), firstOld.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), firstOld.data(), firstOld.size()), 0);
    ASSERT_TRUE(ReadBinaryFile(secondOutput, loaded));
    ASSERT_EQ(loaded.size(), secondOld.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), secondOld.data(), secondOld.size()), 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

