// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_command_ir_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_upload_capture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuCommandIrUploadCapture, CapturesBorrowedCommandBytesAcrossGrowthForUploadAndPushConstants){
    TaskGraphTestUtils::TestArena testArena;
    for(const b8 pushConstants : { false, true }){
        Core::GpuCommandIrCapture capture(testArena.arena);
        const BinaryByteView commands = capture.commandBytes();
        ASSERT_EQ(commands.size(), sizeof(Core::GpuCommandIrStreamHeader));
        const BinaryByteView source{ commands.data() + sizeof(u32), commands.size() - sizeof(u32) };
        Array<u8, sizeof(Core::GpuCommandIrStreamHeader) - sizeof(u32)> expected{};
        NWB_MEMCPY(expected.data(), expected.size(), source.data(), source.size());
        if(pushConstants)
            ASSERT_TRUE(capture.capturePushConstants(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, source));
        else{
            const Core::GpuUploadBlobId blob{ .generation = s_CommandIrTask.generation, .index = 0u };
            ASSERT_TRUE(capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, blob, s_CommandIrDestination, 0u, source, Core::ResourceStates::CopyDest));
        }
        const auto stream = capture.exportOwned(testArena.arena);
        ASSERT_TRUE(stream);
        Core::GpuCommandIrStreamReader reader(stream->bytes());
        ASSERT_TRUE(reader.next());
        const BinaryByteView captured = reader.blobBytes();
        ASSERT_EQ(captured.size(), expected.size());
        EXPECT_EQ(NWB_MEMCMP(captured.data(), expected.data(), expected.size()), 0);
    }
}


TEST(GpuCommandIrUploadCapture, OwnsLargeGraphBlobAfterCallerMutationAndGraphReset){
    TaskGraphTestUtils::TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuCommandIrCapture capture(testArena.arena);
    constexpr usize s_ByteCount = 70u * 1024u + 4u;
    Core::GraphicsBytes caller(testArena.arena);
    Core::GraphicsBytes expected(testArena.arena);
    caller.resize(s_ByteCount);
    expected.resize(s_ByteCount);
    for(usize index = 0u; index < s_ByteCount; ++index){
        caller[index] = static_cast<u8>((index * 17u + 3u) & 0xffu);
        expected[index] = caller[index];
    }

    const auto blobResult = graph.copyUploadData(caller.data(), s_ByteCount, alignof(u32));
    ASSERT_TRUE(blobResult);
    const Core::GpuUploadBlobId blob = *blobResult;
    ASSERT_TRUE(blob.valid());
    const Core::GpuTaskId task{ .generation = blob.generation, .index = 4u };
    const Core::GpuSubmissionPacketId packet{ .generation = 19u, .index = 2u };
    const Core::GpuGraphResourceId destination{ .generation = blob.generation, .index = 6u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    for(usize index = 0u; index < s_ByteCount; ++index)
        caller[index] = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        const auto stored = declarations.uploadBlobData(blob);
        ASSERT_TRUE(stored);
        ASSERT_EQ(stored->size(), s_ByteCount);
        ASSERT_TRUE(capture.captureUploadBuffer(task, packet, s_CommandIrQueue, blob, destination, 64u, *stored, Core::ResourceStates::CopyDest));
    }

    graph.reset();
    const auto owned = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(owned);
    ASSERT_TRUE(Core::ValidateGpuCommandIrStream(owned->bytes()).valid());
    Core::GpuCommandIrStreamReader reader(owned->bytes());
    Expected<Core::GpuCommandIrBuiltinTaskRecord, Core::GpuCommandIrStreamReadStatus::Enum> record = MakeUnexpected(Core::GpuCommandIrStreamReadStatus::End);
    ASSERT_TRUE((record = reader.nextBuiltinTask()));
    EXPECT_EQ(record->blobSizeBytes, s_ByteCount);
    const BinaryByteView blobBytes = reader.blobBytes();
    ASSERT_EQ(blobBytes.size(), s_ByteCount);
    EXPECT_EQ(NWB_MEMCMP(blobBytes.data(), expected.data(), s_ByteCount), 0);
    const auto terminal1 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal1);
    EXPECT_EQ(terminal1.error(), Core::GpuCommandIrStreamReadStatus::End);

    capture.reset();
    ASSERT_TRUE(Core::ValidateGpuCommandIrStream(owned->bytes()).valid());
    Core::GpuCommandIrStreamReader retainedReader(owned->bytes());
    ASSERT_TRUE((record = retainedReader.nextBuiltinTask()));
    EXPECT_EQ(NWB_MEMCMP(retainedReader.blobBytes().data(), expected.data(), s_ByteCount), 0);
}

TEST(GpuCommandIrUploadCapture, CheckpointRollbackKeepsExactCommandAndBlobPrefixThenRefills){
    TaskGraphTestUtils::TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    constexpr Array<u8, 4> s_FirstBytes = { 0x11u, 0x22u, 0x33u, 0x44u };
    constexpr Array<u8, 8> s_SecondBytes = { 0x55u, 0x66u, 0x77u, 0x88u, 0x99u, 0xaau, 0xbbu, 0xccu };
    const Core::GpuUploadBlobId firstBlob{ .generation = s_CommandIrTask.generation, .index = 0u };
    const Core::GpuUploadBlobId secondBlob{ .generation = s_CommandIrTask.generation, .index = 1u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    ASSERT_TRUE(capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, firstBlob, s_CommandIrDestination, 0u, BinaryByteView{ s_FirstBytes.data(), sizeof(s_FirstBytes) }, Core::ResourceStates::CopyDest));
    const Core::GpuCommandIrCaptureCheckpoint checkpoint = capture.checkpoint();
    const auto prefix = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(prefix);
    ASSERT_TRUE(capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, secondBlob, s_CommandIrDestination, 16u, BinaryByteView{ s_SecondBytes.data(), sizeof(s_SecondBytes) }, Core::ResourceStates::ShaderResource));
    const auto full = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(full);
    ASSERT_EQ(capture.recordCount(), 2u);
    ASSERT_TRUE(capture.rollback(checkpoint));
    ASSERT_EQ(capture.recordCount(), 1u);
    const auto rolledBack = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(rolledBack);
    ASSERT_EQ(rolledBack->bytes().size(), prefix->bytes().size());
    EXPECT_EQ(NWB_MEMCMP(rolledBack->bytes().data(), prefix->bytes().data(), prefix->bytes().size()), 0);
    ASSERT_TRUE(Core::ValidateGpuCommandIrStream(rolledBack->bytes()).valid());

    ASSERT_TRUE(capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, secondBlob, s_CommandIrDestination, 16u, BinaryByteView{ s_SecondBytes.data(), sizeof(s_SecondBytes) }, Core::ResourceStates::ShaderResource));
    const auto refilled = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(refilled);
    ASSERT_EQ(refilled->bytes().size(), full->bytes().size());
    EXPECT_EQ(NWB_MEMCMP(refilled->bytes().data(), full->bytes().data(), full->bytes().size()), 0);
    EXPECT_TRUE(Core::ValidateGpuCommandIrStream(refilled->bytes()).valid());
}

TEST(GpuCommandIrUploadCapture, RejectedInputsAndForeignOrStaleCheckpointsPreserveAcceptedBytes){
    TaskGraphTestUtils::TestArena testArena;
    Core::GpuCommandIrCapture capture(testArena.arena);
    Core::GpuCommandIrCapture foreign(testArena.arena);
    constexpr Array<u8, 4> s_Bytes = { 0x11u, 0x22u, 0x33u, 0x44u };
    const Core::GpuUploadBlobId blob{ .generation = s_CommandIrTask.generation, .index = 0u };
    const Core::GpuUploadBlobId staleBlob{ .generation = s_CommandIrTask.generation + 1u, .index = 0u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    ASSERT_TRUE(capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, blob, s_CommandIrDestination, 0u, BinaryByteView{ s_Bytes.data(), sizeof(s_Bytes) }, Core::ResourceStates::CopyDest));
    const Core::GpuCommandIrCaptureCheckpoint checkpoint = capture.checkpoint();
    const auto before = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(before);
    EXPECT_FALSE(capture.beginRecordingAttempt(32u));
    EXPECT_FALSE(capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, staleBlob, s_CommandIrDestination, 16u, BinaryByteView{ s_Bytes.data(), sizeof(s_Bytes) }, Core::ResourceStates::CopyDest));
    EXPECT_FALSE(capture.captureUploadBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, blob, s_CommandIrDestination, 16u, BinaryByteView{ nullptr, sizeof(s_Bytes) }, Core::ResourceStates::CopyDest));
    EXPECT_FALSE(foreign.rollback(checkpoint));
    const auto after = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(after);
    ASSERT_EQ(after->bytes().size(), before->bytes().size());
    EXPECT_EQ(NWB_MEMCMP(after->bytes().data(), before->bytes().data(), before->bytes().size()), 0);
    EXPECT_EQ(capture.recordCount(), 1u);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 31u);

    capture.reset();
    EXPECT_FALSE(capture.rollback(checkpoint));
    EXPECT_EQ(capture.recordCount(), 0u);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 0u);
    EXPECT_TRUE(Core::ValidateGpuCommandIrStream(before->bytes()).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

