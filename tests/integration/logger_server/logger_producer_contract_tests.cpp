// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_helpers.h"

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <logger/common.h>
#include <logger/server/crash_ingest.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_producer_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::LoggerServerCrash;
using TestArena = NWB::Tests::TestArena<struct LoggerProducerContractsTag>;

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
    const auto built = NWB::Log::BuildMessagePayload(source, payload);
    ASSERT_FALSE(built);
    EXPECT_EQ(built.error(), NWB::Log::MessagePayloadError::InvalidType);
    EXPECT_TRUE(payload.empty());
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
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("failed to read crash archive")));
    const auto native = MakeDiagnosticEventText("native_category='{}'; native_code={}", missing.error().category().name(), missing.error().value());
    constexpr Name s_FailureTextScratchName("tests/integration/logger_server/producer_contracts/scratch");
    NWB::Core::Alloc::ScratchArena scratchArena(s_FailureTextScratchName);
    const auto nativeText = StringConvert(scratchArena, native.view());
    EXPECT_TRUE(ContainsMessage(result.message, nativeText));
    RemoveTestArtifacts(arena, s_Group);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

