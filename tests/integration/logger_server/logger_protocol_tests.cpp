// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <logger/message_payload_codec.h>
#include <tests/common/test_context.h>

#include <core/common/log.h>

#include <logger/common.h>
#include <logger/server/crash_auth.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_protocol_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<>;
static constexpr AStringView s_SecretToken = "secret-token";


class LoggerProtocolTests : public ::testing::Test{
public:
    LoggerProtocolTests()
        : m_loggerRegistration(m_logger)
    {}


private:
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerRegistration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LoggerProtocolTests, MessagePayloadReadsUnalignedBytesAndPreservesEmbeddedNulls){
    TestArena testArena;
    constexpr Array<tchar, 3u> s_Message = { static_cast<tchar>('A'), 0, static_cast<tchar>('Z') };
    const TStringView message(s_Message.data(), s_Message.size());
    const NWB::Log::MessageType source = MakeTuple(Timer{}, NWB::Core::Common::LogType::Warning, NWB::Log::LogString(message, testArena.arena));
    NWB::Log::LogBytes payload(testArena.arena);
    ASSERT_TRUE(NWB::Log::MessagePayloadCodec::Build(source, payload));

    constexpr usize s_PrefixBytes = sizeof(tchar);
    NWB::Log::LogBytes shifted(testArena.arena);
    shifted.resize(s_PrefixBytes + payload.size());
    NWB_MEMCPY(shifted.data() + s_PrefixBytes, payload.size(), payload.data(), payload.size());
    const auto parsed = NWB::Log::MessagePayloadCodec::Parse(testArena.arena, shifted.data() + s_PrefixBytes, payload.size());
    ASSERT_TRUE(parsed);
    EXPECT_EQ(TStringView(Get<2u>(*parsed)), message);

    shifted.back() = 1u;
    const auto malformed = NWB::Log::MessagePayloadCodec::Parse(testArena.arena, shifted.data() + s_PrefixBytes, payload.size());
    ASSERT_FALSE(malformed);
    EXPECT_FALSE(malformed.error().empty());
    EXPECT_EQ(TStringView(Get<2u>(*parsed)), message);
}

TEST_F(LoggerProtocolTests, CrashUploadAuthorizationMatchesBearerToken){
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(AStringView(), AStringView()));
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(AStringView(), "bad"));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, AStringView()));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, s_SecretToken));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, "Bearer wrong"));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, "Bearer secret-token "));

    constexpr AStringView s_HeaderBytes = "Bearer secret-token trailing";
    const AStringView header(s_HeaderBytes.data(), LengthOf("Bearer secret-token") - 1u);
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, header));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SecretToken, header.substr(0u, header.size() - 1u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

