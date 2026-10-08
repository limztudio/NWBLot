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


TEST(GpuCommandIrUploadCapture, OwnsLargeGraphBlobAfterCallerMutationAndGraphReset){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    constexpr usize s_ByteCount = 70u * 1024u + 4u;
    Graphics::GraphicsBytes caller(testArena.arena);
    Graphics::GraphicsBytes expected(testArena.arena);
    caller.resize(s_ByteCount);
    expected.resize(s_ByteCount);
    for(usize index = 0u; index < s_ByteCount; ++index){
        caller[index] = static_cast<u8>((index * 17u + 3u) & 0xffu);
        expected[index] = caller[index];
    }

    const Graphics::GpuUploadBlobId blob = graph.copyUploadData(caller.data(), s_ByteCount, alignof(u32));
    ASSERT_TRUE(blob.valid());
    const Graphics::GpuTaskId task{ .generation = blob.generation, .index = 4u };
    const Graphics::GpuSubmissionPacketId packet{ .generation = 19u, .index = 2u };
    const Graphics::GpuGraphResourceId destination{ .generation = blob.generation, .index = 6u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    for(usize index = 0u; index < s_ByteCount; ++index)
        caller[index] = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const auto stored = declarations.uploadBlobData(blob);
        ASSERT_TRUE(stored);
        ASSERT_EQ(stored->size(), s_ByteCount);
        ASSERT_TRUE(capture.captureUploadBuffer(
            task, packet, s_CommandIrQueue, blob, destination, 64u,
            *stored, Graphics::ResourceStates::CopyDest
        ));
    }

    graph.reset();
    const auto owned = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(owned);
    ASSERT_TRUE(Graphics::ValidateGpuCommandIrStream(owned->bytes()).valid());
    Graphics::GpuCommandIrStreamReader reader(owned->bytes());
    Expected<Graphics::GpuCommandIrBuiltinTaskRecord, Graphics::GpuCommandIrStreamReadStatus::Enum> record = MakeUnexpected(Graphics::GpuCommandIrStreamReadStatus::End);
    ASSERT_TRUE((record = reader.nextBuiltinTask()));
    EXPECT_EQ(record->blobSizeBytes, s_ByteCount);
    const BinaryByteView blobBytes = reader.blobBytes();
    ASSERT_EQ(blobBytes.size(), s_ByteCount);
    EXPECT_EQ(NWB_MEMCMP(blobBytes.data(), expected.data(), s_ByteCount), 0);
    const auto terminal1 = reader.nextBuiltinTask();
    ASSERT_FALSE(terminal1);
    EXPECT_EQ(terminal1.error(), Graphics::GpuCommandIrStreamReadStatus::End);

    capture.reset();
    ASSERT_TRUE(Graphics::ValidateGpuCommandIrStream(owned->bytes()).valid());
    Graphics::GpuCommandIrStreamReader retainedReader(owned->bytes());
    ASSERT_TRUE((record = retainedReader.nextBuiltinTask()));
    EXPECT_EQ(NWB_MEMCMP(retainedReader.blobBytes().data(), expected.data(), s_ByteCount), 0);
}

TEST(GpuCommandIrUploadCapture, CheckpointRollbackKeepsExactCommandAndBlobPrefixThenRefills){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    constexpr u8 s_FirstBytes[] = { 0x11u, 0x22u, 0x33u, 0x44u };
    constexpr u8 s_SecondBytes[] = { 0x55u, 0x66u, 0x77u, 0x88u, 0x99u, 0xaau, 0xbbu, 0xccu };
    const Graphics::GpuUploadBlobId firstBlob{ .generation = s_CommandIrTask.generation, .index = 0u };
    const Graphics::GpuUploadBlobId secondBlob{ .generation = s_CommandIrTask.generation, .index = 1u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, firstBlob, s_CommandIrDestination, 0u,
        BinaryByteView{ s_FirstBytes, sizeof(s_FirstBytes) }, Graphics::ResourceStates::CopyDest
    ));
    const Graphics::GpuCommandIrCaptureCheckpoint checkpoint = capture.checkpoint();
    const auto prefix = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(prefix);
    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, secondBlob, s_CommandIrDestination, 16u,
        BinaryByteView{ s_SecondBytes, sizeof(s_SecondBytes) }, Graphics::ResourceStates::ShaderResource
    ));
    const auto full = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(full);
    ASSERT_EQ(capture.recordCount(), 2u);
    ASSERT_TRUE(capture.rollback(checkpoint));
    ASSERT_EQ(capture.recordCount(), 1u);
    const auto rolledBack = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(rolledBack);
    ASSERT_EQ(rolledBack->bytes().size(), prefix->bytes().size());
    EXPECT_EQ(NWB_MEMCMP(rolledBack->bytes().data(), prefix->bytes().data(), prefix->bytes().size()), 0);
    ASSERT_TRUE(Graphics::ValidateGpuCommandIrStream(rolledBack->bytes()).valid());

    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, secondBlob, s_CommandIrDestination, 16u,
        BinaryByteView{ s_SecondBytes, sizeof(s_SecondBytes) }, Graphics::ResourceStates::ShaderResource
    ));
    const auto refilled = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(refilled);
    ASSERT_EQ(refilled->bytes().size(), full->bytes().size());
    EXPECT_EQ(NWB_MEMCMP(refilled->bytes().data(), full->bytes().data(), full->bytes().size()), 0);
    EXPECT_TRUE(Graphics::ValidateGpuCommandIrStream(refilled->bytes()).valid());
}

TEST(GpuCommandIrUploadCapture, RejectedInputsAndForeignOrStaleCheckpointsPreserveAcceptedBytes){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuCommandIrCapture foreign(testArena.arena);
    constexpr u8 s_Bytes[] = { 0x11u, 0x22u, 0x33u, 0x44u };
    const Graphics::GpuUploadBlobId blob{ .generation = s_CommandIrTask.generation, .index = 0u };
    const Graphics::GpuUploadBlobId staleBlob{ .generation = s_CommandIrTask.generation + 1u, .index = 0u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, blob, s_CommandIrDestination, 0u,
        BinaryByteView{ s_Bytes, sizeof(s_Bytes) }, Graphics::ResourceStates::CopyDest
    ));
    const Graphics::GpuCommandIrCaptureCheckpoint checkpoint = capture.checkpoint();
    const auto before = capture.exportOwned(testArena.arena);
    ASSERT_TRUE(before);
    EXPECT_FALSE(capture.beginRecordingAttempt(32u));
    EXPECT_FALSE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, staleBlob, s_CommandIrDestination, 16u,
        BinaryByteView{ s_Bytes, sizeof(s_Bytes) }, Graphics::ResourceStates::CopyDest
    ));
    EXPECT_FALSE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, blob, s_CommandIrDestination, 16u,
        BinaryByteView{ nullptr, sizeof(s_Bytes) }, Graphics::ResourceStates::CopyDest
    ));
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
    EXPECT_TRUE(Graphics::ValidateGpuCommandIrStream(before->bytes()).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

