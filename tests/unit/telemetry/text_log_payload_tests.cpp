// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "telemetry_test_helpers.h"

#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_log_payload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TelemetryTestDetail;
namespace LogType = NWB::Core::Common::LogType;

void CheckPayload(const Span<const u8> payload, const Span<const u8> expected = {}){
    ASSERT_EQ(payload.size(), sizeof(Telemetry::EncodedTextLogPayloadHeader) + expected.size());
    usize cursor = 0u;
    const auto readHeader = ReadPOD<Telemetry::EncodedTextLogPayloadHeader>(payload, cursor);
    ASSERT_TRUE(readHeader);
    Telemetry::EncodedTextLogPayloadHeader header = *readHeader;
    EXPECT_EQ(header.messageBytes, expected.size());
    for(usize index = 0u; index < expected.size(); ++index)
        EXPECT_EQ(payload[cursor + index], expected[index]) << index;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, TextLogPayloadPreservesUnicodeAndEmbeddedNullBytes){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    constexpr TStringView s_Message(
        NWB_TEXT("A\u00e9\ud55c\U0001f642\0Z"),
        sizeof(NWB_TEXT("A\u00e9\ud55c\U0001f642\0Z")) / sizeof(tchar) - 1u
    );
    constexpr Array<u8, 12> s_Expected = { 0x41u, 0xc3u, 0xa9u, 0xedu, 0x95u, 0x9cu, 0xf0u, 0x9fu, 0x99u, 0x82u, 0u, 0x5au };
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Warning, s_Message, testArena.scratchArena, payload));
    CheckPayload(payload, s_Expected);
}

TEST(Telemetry, TextLogPayloadRetainsPlatformInvalidUnicodeConversion){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
#if defined(NWB_UNICODE)
#if WCHAR_MAX <= NWB_STRING_UTF16_CODE_UNIT_MAX
    constexpr Array<tchar, 3> s_Message = { static_cast<tchar>(0xd800u), static_cast<tchar>('X'), static_cast<tchar>(0xdc00u) };
#else
    constexpr Array<tchar, 3> s_Message = { static_cast<tchar>(0x110000u), static_cast<tchar>('X'), static_cast<tchar>(0xffffffffu) };
#endif
    constexpr Array<u8, 3> s_Expected = { static_cast<u8>('?'), static_cast<u8>('X'), static_cast<u8>('?') };
#else
    // Narrow byte strings pass through without repairing invalid UTF-8.
    constexpr Array<tchar, 3> s_Message = { static_cast<tchar>(0xc0u), static_cast<tchar>(0xafu), static_cast<tchar>('X') };
    constexpr Array<u8, 3> s_Expected = { 0xc0u, 0xafu, static_cast<u8>('X') };
#endif
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Info, TStringView(s_Message.data(), s_Message.size()), testArena.scratchArena, payload));
    CheckPayload(payload, s_Expected);
}

TEST(Telemetry, TextLogPayloadPreservesAliasedInputBeforeHeaderWrite){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    constexpr TStringView s_Message = NWB_TEXT("captured \ud55c\U0001f642");
    constexpr Array<u8, 16> s_Expected = { 'c', 'a', 'p', 't', 'u', 'r', 'e', 'd', ' ', 0xedu, 0x95u, 0x9cu, 0xf0u, 0x9fu, 0x99u, 0x82u };
    constexpr usize s_Offset = sizeof(tchar);
    constexpr usize s_MessageBytes = s_Message.size() * sizeof(tchar);
    payload.resize(s_Offset + s_MessageBytes);
    NWB_MEMCPY(payload.data() + s_Offset, s_MessageBytes, s_Message.data(), s_MessageBytes);
    const TStringView aliased(reinterpret_cast<const tchar*>(payload.data() + s_Offset), s_Message.size());
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Error, aliased, testArena.scratchArena, payload));
    CheckPayload(payload, s_Expected);
}

TEST(Telemetry, TextLogPayloadRejectsNonCurrentVersionsAndRecovers){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Warning, NWB_TEXT("retained log"), testArena.scratchArena, payload));
    const Telemetry::TelemetryBytes current = payload;
    const Array<u16, 3> unsupportedVersions = { 0u, static_cast<u16>(Telemetry::s_TextLogPayloadVersion + 1u), Limit<u16>::s_Max, };
    Expected<Telemetry::TextLogPayload> parsed = MakeUnexpected(Failure{});
    for(const u16 version : unsupportedVersions){
        SCOPED_TRACE(version);
        ASSERT_TRUE((parsed = Telemetry::ParseTextLogPayload(current.data(), current.size())));
        usize cursor = 0u;
        const auto readHeader = ReadPOD<Telemetry::EncodedTextLogPayloadHeader>(current, cursor);
        ASSERT_TRUE(readHeader);
        Telemetry::EncodedTextLogPayloadHeader header = *readHeader;
        header.version = version;
        NWB_MEMCPY(payload.data(), payload.size(), &header, sizeof(header));
        EXPECT_FALSE((parsed = Telemetry::ParseTextLogPayload(payload.data(), payload.size())));
        ASSERT_TRUE((parsed = Telemetry::ParseTextLogPayload(current.data(), current.size())));
        EXPECT_EQ(AStringView(parsed->messageUtf8), AStringView("retained log"));
    }
}

TEST(Telemetry, TextLogPayloadEmptyAndInvalidTypeReplacePreviousBytes){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::Info, NWB_TEXT("old payload"), testArena.scratchArena, payload));
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(LogType::EssentialInfo, {}, testArena.scratchArena, payload));
    CheckPayload(payload);
    EXPECT_FALSE(Telemetry::BuildTextLogPayload(static_cast<LogType::Enum>(0xffu), {}, testArena.scratchArena, payload));
    EXPECT_TRUE(payload.empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

