// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/filesystem_helpers.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/common/log.h>

#include <global/filesystem/executable_paths.h>
#include <global/atomic.h>
#include <global/environment.h>
#include <global/filesystem/exclusive_publication.h>
#include <global/filesystem/operations.h>
#include <global/process.h>
#include <global/sync.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logserver_telemetry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<>;


class LoggerTelemetryPublication : public ::testing::Test{
public:
    LoggerTelemetryPublication()
        : m_loggerRegistration(m_logger)
    {}


private:
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerRegistration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static ::Path<NWB::Core::Alloc::GlobalArena> MakeTelemetryPublicationTestDirectory(TestArena& testArena){
    ::Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena);
    const auto executableDirectory = ExecutablePaths(testArena.arena).directory();
    if(!executableDirectory)
        return root;
    for(u32 attempt = 0u; attempt < 128u; ++attempt){
        const auto name = StringFormat(testArena.arena, "telemetry_publication_test_{}_{}", CurrentProcessId(), attempt);
        root = *executableDirectory / name;
        const auto created = CreateDirectories(root);
        if(!created)
            return ::Path<NWB::Core::Alloc::GlobalArena>(testArena.arena);
        if(*created)
            return root;
    }
    return ::Path<NWB::Core::Alloc::GlobalArena>(testArena.arena);
}

TEST_F(LoggerTelemetryPublication, TelemetryPublicationDoesNotReplaceExistingFinalFile){
    TestArena testArena;
    const auto root = MakeTelemetryPublicationTestDirectory(testArena);
    ASSERT_FALSE(root.empty());
    const auto destination = root / "payload.nwbs";
    constexpr Array<u8, 3u> s_Existing = { 1u, 2u, 3u };
    constexpr Array<u8, 3u> s_New = { 4u, 5u, 6u };
    ASSERT_TRUE(WriteBinaryFile(destination, s_Existing));

    const auto result = PublishNewFileDurable(destination, BinaryByteView{ s_New.data(), s_New.size() });
    EXPECT_FALSE(result.published);
    EXPECT_TRUE(result.stagingRetained);
    EXPECT_TRUE(result.error);
    Vector<u8, NWB::Core::Alloc::GlobalArena> bytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(destination, bytes));
    ASSERT_EQ(bytes.size(), s_Existing.size());
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), s_Existing.data(), s_Existing.size()), 0);
    auto staging = destination;
    staging += ".partial";
    ASSERT_TRUE(ReadBinaryFile(staging, bytes));
    ASSERT_EQ(bytes.size(), s_New.size());
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), s_New.data(), s_New.size()), 0);
}

TEST_F(LoggerTelemetryPublication, TelemetryPublicationDoesNotReplaceExistingStagingFile){
    TestArena testArena;
    const auto root = MakeTelemetryPublicationTestDirectory(testArena);
    ASSERT_FALSE(root.empty());
    const auto destination = root / "payload.nwbs";
    auto staging = destination;
    staging += ".partial";
    constexpr Array<u8, 2u> s_Existing = { 7u, 8u };
    constexpr Array<u8, 2u> s_New = { 9u, 10u };
    ASSERT_TRUE(WriteBinaryFile(staging, s_Existing));

    const auto result = PublishNewFileDurable(destination, BinaryByteView{ s_New.data(), s_New.size() });
    EXPECT_FALSE(result.published);
    EXPECT_FALSE(result.stagingRetained);
    EXPECT_TRUE(result.error);
    Vector<u8, NWB::Core::Alloc::GlobalArena> bytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(staging, bytes));
    ASSERT_EQ(bytes.size(), s_Existing.size());
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), s_Existing.data(), s_Existing.size()), 0);
    const auto finalExists = FileExists(destination);
    ASSERT_TRUE(finalExists);
    EXPECT_FALSE(*finalExists);
}

TEST_F(LoggerTelemetryPublication, ConcurrentTelemetryPublishersCannotReplaceEachOther){
    TestArena rootArena;
    TestArena firstArena;
    TestArena secondArena;
    const auto root = MakeTelemetryPublicationTestDirectory(rootArena);
    ASSERT_FALSE(root.empty());
    const auto destination = root / "payload.nwbs";
    const ::Path<NWB::Core::Alloc::GlobalArena> firstPath(firstArena.arena, destination);
    const ::Path<NWB::Core::Alloc::GlobalArena> secondPath(secondArena.arena, destination);
    constexpr Array<u8, 2u> s_First = { 21u, 22u };
    constexpr Array<u8, 2u> s_Second = { 31u, 32u };
    FilePublicationResult firstResult;
    FilePublicationResult secondResult;
    Atomic<b8> start = false;
    Thread first([&](){
        while(!start.load(MemoryOrder::acquire))
            YieldThread();
        firstResult = PublishNewFileDurable(firstPath, BinaryByteView{ s_First.data(), s_First.size() });
    });
    Thread second([&](){
        while(!start.load(MemoryOrder::acquire))
            YieldThread();
        secondResult = PublishNewFileDurable(secondPath, BinaryByteView{ s_Second.data(), s_Second.size() });
    });
    start.store(true, MemoryOrder::release);
    first.join();
    second.join();
    EXPECT_NE(firstResult.published, secondResult.published);
    Vector<u8, NWB::Core::Alloc::GlobalArena> bytes(rootArena.arena);
    ASSERT_TRUE(ReadBinaryFile(destination, bytes));
    ASSERT_EQ(bytes.size(), s_First.size());
    const auto& winner = firstResult.published ? s_First : s_Second;
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), winner.data(), winner.size()), 0);
}

TEST_F(LoggerTelemetryPublication, TelemetryPublicationMovesCompletedBytesToNewFinalFile){
    TestArena testArena;
    const auto root = MakeTelemetryPublicationTestDirectory(testArena);
    ASSERT_FALSE(root.empty());
    const auto destination = root / "payload.nwbs";
    constexpr Array<u8, 3u> s_Data = { 11u, 12u, 13u };
    const auto result = PublishNewFileDurable(destination, BinaryByteView{ s_Data.data(), s_Data.size() });
    EXPECT_TRUE(result.published);
    EXPECT_FALSE(result.stagingRetained);
    EXPECT_FALSE(result.error);
    Vector<u8, NWB::Core::Alloc::GlobalArena> bytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(destination, bytes));
    ASSERT_EQ(bytes.size(), s_Data.size());
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), s_Data.data(), s_Data.size()), 0);
    auto staging = destination;
    staging += ".partial";
    const auto stagingExists = FileExists(staging);
    ASSERT_TRUE(stagingExists);
    EXPECT_FALSE(*stagingExists);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

