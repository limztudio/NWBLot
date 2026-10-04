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
    Graphics::GpuCommandIrOwnedStream owned(testArena.arena);
    constexpr usize byteCount = 70u * 1024u + 4u;
    Graphics::GraphicsBytes caller(testArena.arena);
    Graphics::GraphicsBytes expected(testArena.arena);
    caller.resize(byteCount);
    expected.resize(byteCount);
    for(usize index = 0u; index < byteCount; ++index){
        caller[index] = static_cast<u8>((index * 17u + 3u) & 0xffu);
        expected[index] = caller[index];
    }

    const Graphics::GpuUploadBlobId blob = graph.copyUploadData(caller.data(), byteCount, alignof(u32));
    ASSERT_TRUE(blob.valid());
    const Graphics::GpuTaskId task{ .generation = blob.generation, .index = 4u };
    const Graphics::GpuSubmissionPacketId packet{ .generation = 19u, .index = 2u };
    const Graphics::GpuGraphResourceId destination{ .generation = blob.generation, .index = 6u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    for(usize index = 0u; index < byteCount; ++index)
        caller[index] = 0u;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        usize storedSize = 0u;
        const void* const stored = declarations.uploadBlobData(blob, storedSize);
        ASSERT_NE(stored, nullptr);
        ASSERT_EQ(storedSize, byteCount);
        ASSERT_TRUE(capture.captureUploadBuffer(
            task, packet, s_CommandIrQueue, blob, destination, 64u,
            BinaryByteView{ static_cast<const u8*>(stored), storedSize }, Graphics::ResourceStates::CopyDest
        ));
    }

    graph.reset();
    ASSERT_TRUE(capture.exportOwned(owned));
    ASSERT_TRUE(Graphics::ValidateGpuCommandIrStream(owned.bytes()).valid());
    Graphics::GpuCommandIrStreamReader reader(owned.bytes());
    Graphics::GpuCommandIrBuiltinTaskRecord record;
    ASSERT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(record.opcode, Graphics::GpuCommandIrOpcode::UploadBuffer);
    EXPECT_EQ(record.sourceUploadBlob, blob);
    EXPECT_EQ(record.destination, destination);
    EXPECT_EQ(record.destinationOffsetBytes, 64u);
    EXPECT_EQ(record.blobSizeBytes, byteCount);
    const BinaryByteView blobBytes = reader.blobBytes();
    ASSERT_EQ(blobBytes.size(), byteCount);
    EXPECT_EQ(GLB_MEMCMP(blobBytes.data(), expected.data(), byteCount), 0);
    EXPECT_EQ(reader.next(record), Graphics::GpuCommandIrStreamReadStatus::End);

    capture.reset();
    ASSERT_TRUE(Graphics::ValidateGpuCommandIrStream(owned.bytes()).valid());
    Graphics::GpuCommandIrStreamReader retainedReader(owned.bytes());
    ASSERT_EQ(retainedReader.next(record), Graphics::GpuCommandIrStreamReadStatus::Record);
    EXPECT_EQ(GLB_MEMCMP(retainedReader.blobBytes().data(), expected.data(), byteCount), 0);
}

TEST(GpuCommandIrUploadCapture, CheckpointRollbackKeepsExactCommandAndBlobPrefixThenRefills){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuCommandIrOwnedStream prefix(testArena.arena);
    Graphics::GpuCommandIrOwnedStream full(testArena.arena);
    Graphics::GpuCommandIrOwnedStream rolledBack(testArena.arena);
    Graphics::GpuCommandIrOwnedStream refilled(testArena.arena);
    constexpr u8 firstBytes[] = { 0x11u, 0x22u, 0x33u, 0x44u };
    constexpr u8 secondBytes[] = { 0x55u, 0x66u, 0x77u, 0x88u, 0x99u, 0xaau, 0xbbu, 0xccu };
    const Graphics::GpuUploadBlobId firstBlob{ .generation = s_CommandIrTask.generation, .index = 0u };
    const Graphics::GpuUploadBlobId secondBlob{ .generation = s_CommandIrTask.generation, .index = 1u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, firstBlob, s_CommandIrDestination, 0u,
        BinaryByteView{ firstBytes, sizeof(firstBytes) }, Graphics::ResourceStates::CopyDest
    ));
    const Graphics::GpuCommandIrCaptureCheckpoint checkpoint = capture.checkpoint();
    ASSERT_TRUE(capture.exportOwned(prefix));
    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, secondBlob, s_CommandIrDestination, 16u,
        BinaryByteView{ secondBytes, sizeof(secondBytes) }, Graphics::ResourceStates::ShaderResource
    ));
    ASSERT_TRUE(capture.exportOwned(full));
    ASSERT_EQ(capture.recordCount(), 2u);
    ASSERT_TRUE(capture.rollback(checkpoint));
    ASSERT_EQ(capture.recordCount(), 1u);
    ASSERT_TRUE(capture.exportOwned(rolledBack));
    ASSERT_EQ(rolledBack.bytes().size(), prefix.bytes().size());
    EXPECT_EQ(GLB_MEMCMP(rolledBack.bytes().data(), prefix.bytes().data(), prefix.bytes().size()), 0);
    ASSERT_TRUE(Graphics::ValidateGpuCommandIrStream(rolledBack.bytes()).valid());

    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, secondBlob, s_CommandIrDestination, 16u,
        BinaryByteView{ secondBytes, sizeof(secondBytes) }, Graphics::ResourceStates::ShaderResource
    ));
    ASSERT_TRUE(capture.exportOwned(refilled));
    ASSERT_EQ(refilled.bytes().size(), full.bytes().size());
    EXPECT_EQ(GLB_MEMCMP(refilled.bytes().data(), full.bytes().data(), full.bytes().size()), 0);
    EXPECT_TRUE(Graphics::ValidateGpuCommandIrStream(refilled.bytes()).valid());
}

TEST(GpuCommandIrUploadCapture, RejectedInputsAndForeignOrStaleCheckpointsPreserveAcceptedBytes){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    Graphics::GpuCommandIrCapture foreign(testArena.arena);
    Graphics::GpuCommandIrOwnedStream before(testArena.arena);
    Graphics::GpuCommandIrOwnedStream after(testArena.arena);
    constexpr u8 bytes[] = { 0x11u, 0x22u, 0x33u, 0x44u };
    const Graphics::GpuUploadBlobId blob{ .generation = s_CommandIrTask.generation, .index = 0u };
    const Graphics::GpuUploadBlobId staleBlob{ .generation = s_CommandIrTask.generation + 1u, .index = 0u };
    ASSERT_TRUE(capture.beginRecordingAttempt(31u));
    ASSERT_TRUE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, blob, s_CommandIrDestination, 0u,
        BinaryByteView{ bytes, sizeof(bytes) }, Graphics::ResourceStates::CopyDest
    ));
    const Graphics::GpuCommandIrCaptureCheckpoint checkpoint = capture.checkpoint();
    ASSERT_TRUE(capture.exportOwned(before));
    EXPECT_FALSE(capture.beginRecordingAttempt(32u));
    EXPECT_FALSE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, staleBlob, s_CommandIrDestination, 16u,
        BinaryByteView{ bytes, sizeof(bytes) }, Graphics::ResourceStates::CopyDest
    ));
    EXPECT_FALSE(capture.captureUploadBuffer(
        s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, blob, s_CommandIrDestination, 16u,
        BinaryByteView{ nullptr, sizeof(bytes) }, Graphics::ResourceStates::CopyDest
    ));
    EXPECT_FALSE(foreign.rollback(checkpoint));
    ASSERT_TRUE(capture.exportOwned(after));
    ASSERT_EQ(after.bytes().size(), before.bytes().size());
    EXPECT_EQ(GLB_MEMCMP(after.bytes().data(), before.bytes().data(), before.bytes().size()), 0);
    EXPECT_EQ(capture.recordCount(), 1u);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 31u);

    capture.reset();
    EXPECT_FALSE(capture.rollback(checkpoint));
    EXPECT_EQ(capture.recordCount(), 0u);
    EXPECT_EQ(capture.recordingAttemptGeneration(), 0u);
    EXPECT_TRUE(Graphics::ValidateGpuCommandIrStream(before.bytes()).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

