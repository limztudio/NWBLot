// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>

#include <gtest/gtest.h>

#include <global/atomic.h>
#include <global/filesystem/staged_file_writer.h>
#include <global/binary.h>
#include <global/containers.h>
#include <global/filesystem/exclusive_publication.h>
#include <global/filesystem/file_staging.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/filesystem/utility.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem_staging_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, FilesystemStreamAndStageHelpersRejectInvalidAndTruncatedInputs){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::s_TestArena);
    const Path<NWB::Core::Alloc::ScratchArena> emptyPath(scratchArena);
    EXPECT_EQ(PathDepth(emptyPath), 0u);
    const Path<NWB::Core::Alloc::ScratchArena> stageDir(scratchArena, "global_test_artifacts/stage_edge_test");
    ASSERT_TRUE(EnsureEmptyDirectory(stageDir));
    const auto stagedFile = stageDir / "payload.bin";
    const Array<u8, 4u> payload = { 0x10u, 0x20u, 0x30u, 0x40u };
    b8 created = false;
    ASSERT_TRUE(StagedFileWriter(stagedFile).create(payload.data(), payload.size(), created));
    EXPECT_TRUE(created);
    EXPECT_TRUE(VerifyBinaryFileBytes(stagedFile, payload.data(), payload.size()));
    const Array<u8, 4u> mismatched = { 0x10u, 0x20u, 0x30u, 0x41u };
    EXPECT_FALSE(VerifyBinaryFileBytes(stagedFile, mismatched.data(), mismatched.size()));
    EXPECT_FALSE(VerifyBinaryFileBytes(stagedFile, payload.data(), payload.size() - 1u));
    EXPECT_FALSE(StagedFileWriter(stagedFile).create(mismatched.data(), mismatched.size(), created));
    EXPECT_FALSE(created);
    EXPECT_TRUE(VerifyBinaryFileBytes(stagedFile, payload.data(), payload.size()));
    ASSERT_TRUE(ResizeFile(stagedFile, 2u));
    EXPECT_TRUE(VerifyBinaryFileBytes(stagedFile, payload.data(), 2u));
    ASSERT_TRUE(RemoveAllIfExists(stageDir));
}

TEST(Global, UniquePublicationPreservesWinnersAcrossRaces){
    NWB::Core::Alloc::ScratchArena arena(NWB::Tests::s_TestArena);
    const Path<NWB::Core::Alloc::ScratchArena> directory(arena, "global_test_artifacts");
    ASSERT_TRUE(EnsureDirectories(directory));
    const auto suggestedPath = directory / "publication.bin";
    auto reserved = StagedFileWriter(suggestedPath).stageUnique(nullptr, 0u);
    ASSERT_TRUE(reserved);
    const Path<NWB::Core::Alloc::ScratchArena> path = Move(*reserved);
    const auto released = RemoveFile(path);
    ASSERT_TRUE(released && *released);
    const Array<u8, 4u> bytes{ 1u, 2u, 3u, 4u };
    const Array<u8, 4u> different{ 1u, 2u, 3u, 5u };
    const auto first = PublishUniqueFileIfAbsentOrIdentical(path, bytes);
    ASSERT_TRUE(first);
    EXPECT_TRUE(*first);
    const auto repeated = PublishUniqueFileIfAbsentOrIdentical(path, bytes);
    ASSERT_TRUE(repeated);
    EXPECT_FALSE(*repeated);
    const auto mismatch = PublishUniqueFileIfAbsentOrIdentical(path, different);
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error(), MakeErrorCode(ErrorCondition::file_exists));
    EXPECT_TRUE(VerifyBinaryFileBytes(path, bytes.data(), bytes.size()));
    Vector<u8, NWB::Core::Alloc::ScratchArena> borrowedBytes(arena);
    borrowedBytes.assign(different.begin(), different.end());
    EXPECT_FALSE(PublishUniqueFileIfAbsentOrIdentical(path, Span<const u8>(borrowedBytes.data(), borrowedBytes.size())));
    EXPECT_TRUE(BytesMatch(Span<const u8>(borrowedBytes.data(), borrowedBytes.size()), different));
    const auto removed = RemoveFile(path);
    ASSERT_TRUE(removed && *removed);

    Atomic<b8> start{ false };
    Array<u8, 2u> results{};
    Array<Thread, 2u> threads;
    for(usize index = 0u; index < threads.size(); ++index){
        threads[index] = Thread([&, index](){
            NWB::Core::Alloc::ScratchArena threadArena(NWB::Tests::s_TestArena);
            const Path<NWB::Core::Alloc::ScratchArena> threadPath(threadArena, path);
            while(!start.load(MemoryOrder::acquire))
                YieldThread();
            const auto result = PublishUniqueFileIfAbsentOrIdentical(threadPath, bytes);
            results[index] = result ? (*result ? 1u : 2u) : 0u;
        });
    }
    start.store(true, MemoryOrder::release);
    for(Thread& thread : threads)
        thread.join();
    EXPECT_TRUE((results[0u] == 1u && results[1u] == 2u) || (results[0u] == 2u && results[1u] == 1u));
    EXPECT_TRUE(VerifyBinaryFileBytes(path, bytes.data(), bytes.size()));
    const auto cleanup = RemoveFile(path);
    ASSERT_TRUE(cleanup && *cleanup);

    start.store(false, MemoryOrder::relaxed);
    results = {};
    Array<i32, 2u> publicationErrors{};
    for(usize index = 0u; index < threads.size(); ++index){
        threads[index] = Thread([&, index](){
            NWB::Core::Alloc::ScratchArena threadArena(NWB::Tests::s_TestArena);
            const Path<NWB::Core::Alloc::ScratchArena> threadPath(threadArena, path);
            while(!start.load(MemoryOrder::acquire))
                YieldThread();
            const Span<const u8> candidate = index == 0u ? Span<const u8>(bytes) : Span<const u8>(different);
            const auto result = PublishUniqueFileIfAbsentOrIdentical(threadPath, candidate);
            if(result && *result)
                results[index] = 1u;
            else if(!result){
                publicationErrors[index] = result.error().value();
                if(result.error() == MakeErrorCode(ErrorCondition::file_exists))
                    results[index] = 2u;
            }
            else
                results[index] = 3u;
        });
    }
    start.store(true, MemoryOrder::release);
    for(Thread& thread : threads)
        thread.join();
    EXPECT_TRUE((results[0u] == 1u && results[1u] == 2u) || (results[0u] == 2u && results[1u] == 1u)) << "results " << static_cast<u32>(results[0u]) << ", " << static_cast<u32>(results[1u]) << "; errors " << publicationErrors[0u] << ", " << publicationErrors[1u];
    const Span<const u8> winner = results[0u] == 1u ? Span<const u8>(bytes) : Span<const u8>(different);
    EXPECT_TRUE(VerifyBinaryFileBytes(path, winner.data(), winner.size()));
    const auto finalCleanup = RemoveFile(path);
    ASSERT_TRUE(finalCleanup && *finalCleanup);
}

TEST(Global, StagedPairFailureIdentifiesEachWorkPath){
    NWB::Core::Alloc::ScratchArena arena(NWB::Tests::s_TestArena);
    const Path<NWB::Core::Alloc::ScratchArena> primary(arena, "output.bin");
    const Path<NWB::Core::Alloc::ScratchArena> secondary(arena, "output.meta");
    const StagedFilePairPaths paths(arena, primary, secondary);
    const Array<const Path<NWB::Core::Alloc::ScratchArena>*, 4u> expected{ &paths.primaryTemporary, &paths.secondaryTemporary, &paths.primaryBackup, &paths.secondaryBackup };
    for(const auto stage : { StagedFilePairFailureStage::InspectOutput, StagedFilePairFailureStage::NonRegularOutput }){
        StagedFilePairResult result;
        result.failureStage = stage;
        result.failedWorkPath = true;
        for(u32 index = 0u; index < expected.size(); ++index){
            result.failedIndex = index;
            EXPECT_EQ(FailedStagedFilePairPath(paths, result), *expected[index]);
        }
        result.failedWorkPath = false;
        result.failedIndex = 0u;
        EXPECT_EQ(FailedStagedFilePairPath(paths, result), paths.primary);
        result.failedIndex = 1u;
        EXPECT_EQ(FailedStagedFilePairPath(paths, result), paths.secondary);
    }
    StagedFilePairResult state;
    state.backedUp[1u] = true;
    state.temporaryErrors[0u] = MakeErrorCode(ErrorCondition::io_error);
    StagedFilePairResult check;
    EXPECT_TRUE(GlobalFilesystemStagingDetail::AcceptPreCommitValidation(check, state));
    check.failureStage = StagedFilePairFailureStage::WorkPathAlreadyExists;
    EXPECT_TRUE(GlobalFilesystemStagingDetail::AcceptPreCommitValidation(check, state));
    EXPECT_TRUE(state.succeeded());
    check.failureStage = StagedFilePairFailureStage::NonRegularOutput;
    check.failedIndex = 2u;
    check.failedWorkPath = true;
    check.error = MakeErrorCode(ErrorCondition::invalid_argument);
    EXPECT_FALSE(GlobalFilesystemStagingDetail::AcceptPreCommitValidation(check, state));
    EXPECT_EQ(state.failureStage, check.failureStage);
    EXPECT_EQ(state.error, check.error);
    EXPECT_EQ(FailedStagedFilePairPath(paths, state), paths.primaryBackup);
    EXPECT_TRUE(state.backedUp[1u]);
    EXPECT_EQ(state.temporaryErrors[0u], MakeErrorCode(ErrorCondition::io_error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

