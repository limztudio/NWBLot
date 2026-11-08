// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_helpers.h"
#include <logger/message_payload_codec.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/integration/logger_server/name_cases.adhd.h>

#include <logger/common.h>
#include <logger/processed_message_file.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_producer_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::CrashTestPaths;
using namespace NWB::Tests::LoggerServerCrash;
using TestArena = NWB::Tests::TestArena<>;

class LoggerProducerContracts : public testing::Test{
public:
    LoggerProducerContracts()
        : m_registration(m_logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly)
    {}


private:
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_registration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LoggerProducerContracts, MessagePayloadRejectsInvalidTypeWithoutPublishingStaleBytes){
    TestArena testArena;
    NWB::Log::LogBytes payload(testArena.arena);
    payload.push_back(42u);
    const NWB::Log::MessageType source = MakeTuple(Timer{}, static_cast<NWB::Core::Common::LogType::Enum>(255u), NWB::Log::LogString(NWB_TEXT("invalid"), testArena.arena));
    const auto built = NWB::Log::MessagePayloadCodec::Build(source, payload);
    ASSERT_FALSE(built);
    EXPECT_EQ(built.error(), NWB::Log::MessagePayloadError::InvalidType);
    EXPECT_TRUE(payload.empty());
}

TEST_F(LoggerProducerContracts, SimultaneousProcessedFilesKeepIndependentRecords){
    TestArena testArena;
    NWB::Log::ProcessedMessageFile first(testArena.arena);
    NWB::Log::ProcessedMessageFile second(testArena.arena);
    ASSERT_TRUE(first.open(NWB_TEXT("processed_file_isolation")));
    ASSERT_TRUE(second.open(NWB_TEXT("processed_file_isolation")));
    EXPECT_NE(first.path(), second.path());
    ASSERT_TRUE(first.writeLine(NWB_TEXT("first record")));
    ASSERT_TRUE(second.writeLine(NWB_TEXT("second record")));
    first.close();
    second.close();
    AString<NWB::Log::LogArena> firstText(testArena.arena);
    AString<NWB::Log::LogArena> secondText(testArena.arena);
    ASSERT_TRUE(ReadTextFile(first.path(), firstText));
    ASSERT_TRUE(ReadTextFile(second.path(), secondText));
    EXPECT_TRUE(Contains(firstText, "first record"));
    EXPECT_FALSE(Contains(firstText, "second record"));
    EXPECT_TRUE(Contains(secondText, "second record"));
    EXPECT_FALSE(Contains(secondText, "first record"));
}

TEST_F(LoggerProducerContracts, MissingArchiveRetainsNativeReadFailureInOwningRejection){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_missing_archive_native_error_test");
    constexpr AStringView s_Stem("missing_archive_001");
    RemoveTestArtifacts(arena, s_Group);
    const auto config = MakeIngestConfig(arena, s_Group);
    const auto path = ArchivePath(arena, s_Group, s_Stem);
    const auto missing = FileSize(path);
    ASSERT_FALSE(missing);
    const auto result = NWB::Log::ProcessCrashUpload(arena, path, config);
    EXPECT_FALSE(result.accepted);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("failed to read crash archive")));
    const auto native = MakeDiagnosticEventText("native_category='{}'; native_code={}", missing.error().category().name(), missing.error().value());
    constexpr Name s_FailureTextScratchName = NWB::Tests::LoggerCrashCases::s_LoggerCrashTestNameTestsIntegrationLoggerServerProducerContractsScratch;
    NWB::Core::Alloc::ScratchArena scratchArena(s_FailureTextScratchName);
    const auto nativeText = ConvertString(scratchArena, native.view());
    EXPECT_TRUE(Contains(result.message, nativeText));
    RemoveTestArtifacts(arena, s_Group);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

