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

void CheckPayload(const Telemetry::TelemetryBytes& payload, const LogType::Enum type, const u8* expected, const usize count){
    ASSERT_EQ(payload.size(), sizeof(Telemetry::EncodedTextLogPayloadHeader) + count);
    Telemetry::EncodedTextLogPayloadHeader header;
    usize cursor = 0u;
    ASSERT_TRUE(ReadPOD(payload, cursor, header));
    EXPECT_EQ(header.magic, Telemetry::s_TextLogPayloadMagic);
    EXPECT_EQ(header.version, Telemetry::s_TextLogPayloadVersion);
    EXPECT_EQ(header.type, static_cast<u8>(type));
    EXPECT_EQ(header.reserved, 0u);
    EXPECT_EQ(header.messageBytes, count);
    for(usize index = 0u; index < count; ++index)
        EXPECT_EQ(payload[cursor + index], expected[index]) << index;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Telemetry, TextLogPayloadPreservesUnicodeAndEmbeddedNullBytes){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    constexpr tchar s_Message[] = NWB_TEXT("A\u00e9\ud55c\U0001f642\0Z");
    constexpr u8 s_Expected[] = { 0x41u, 0xc3u, 0xa9u, 0xedu, 0x95u, 0x9cu, 0xf0u, 0x9fu, 0x99u, 0x82u, 0u, 0x5au };
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(
        testArena.arena, LogType::Warning, TStringView(s_Message, LengthOf(s_Message) - 1u), payload
    ));
    CheckPayload(payload, LogType::Warning, s_Expected, LengthOf(s_Expected));
}

TEST(Telemetry, TextLogPayloadRetainsPlatformInvalidUnicodeConversion){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
#if defined(NWB_UNICODE)
#if WCHAR_MAX <= 0xffff
    constexpr tchar s_Message[] = { static_cast<tchar>(0xd800u), static_cast<tchar>('X'), static_cast<tchar>(0xdc00u) };
#else
    constexpr tchar s_Message[] = { static_cast<tchar>(0x110000u), static_cast<tchar>('X'), static_cast<tchar>(0xffffffffu) };
#endif
    constexpr u8 s_Expected[] = { static_cast<u8>('?'), static_cast<u8>('X'), static_cast<u8>('?') };
#else
    // Narrow inputs are already byte strings; the existing conversion deliberately does not repair malformed UTF-8.
    constexpr tchar s_Message[] = { static_cast<tchar>(0xc0u), static_cast<tchar>(0xafu), static_cast<tchar>('X') };
    constexpr u8 s_Expected[] = { 0xc0u, 0xafu, static_cast<u8>('X') };
#endif
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(
        testArena.arena, LogType::Info, TStringView(s_Message, LengthOf(s_Message)), payload
    ));
    CheckPayload(payload, LogType::Info, s_Expected, LengthOf(s_Expected));
}

TEST(Telemetry, TextLogPayloadReusesWarmDestinationWithoutTransientAllocations){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    tchar message[192u];
    for(tchar& character : message)
        character = static_cast<tchar>('x');
    const TStringView text(message, LengthOf(message));
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(testArena.arena, LogType::Info, text, payload));
    const u8* const initialData = payload.data();
    const ArenaMemoryStats warmed = testArena.arena.memoryStats();
    for(usize iteration = 0u; iteration < 64u; ++iteration){
        ASSERT_TRUE(Telemetry::BuildTextLogPayload(testArena.arena, LogType::Info, text, payload));
        EXPECT_EQ(payload.data(), initialData);
        ASSERT_EQ(payload.size(), sizeof(Telemetry::EncodedTextLogPayloadHeader) + LengthOf(message));
        for(usize index = sizeof(Telemetry::EncodedTextLogPayloadHeader); index < payload.size(); ++index)
            EXPECT_EQ(payload[index], static_cast<u8>('x'));
    }
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    EXPECT_EQ(after.allocationCount, warmed.allocationCount);
    EXPECT_EQ(after.reallocationCount, warmed.reallocationCount);
    EXPECT_EQ(after.deallocationCount, warmed.deallocationCount);
}

TEST(Telemetry, TextLogPayloadPreservesAliasedInputBeforeHeaderWrite){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    constexpr tchar s_Message[] = NWB_TEXT("captured \ud55c\U0001f642");
    constexpr u8 s_Expected[] = {
        'c', 'a', 'p', 't', 'u', 'r', 'e', 'd', ' ', 0xedu, 0x95u, 0x9cu, 0xf0u, 0x9fu, 0x99u, 0x82u
    };
    constexpr usize s_Offset = sizeof(tchar);
    constexpr usize s_MessageBytes = sizeof(s_Message) - sizeof(tchar);
    payload.resize(s_Offset + s_MessageBytes);
    NWB_MEMCPY(payload.data() + s_Offset, s_MessageBytes, s_Message, s_MessageBytes);
    const TStringView aliased(reinterpret_cast<const tchar*>(payload.data() + s_Offset), LengthOf(s_Message) - 1u);
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(testArena.arena, LogType::Error, aliased, payload));
    CheckPayload(payload, LogType::Error, s_Expected, LengthOf(s_Expected));
}

TEST(Telemetry, TextLogPayloadEmptyAndInvalidTypeReplacePreviousBytes){
    TestArena testArena;
    Telemetry::TelemetryBytes payload(testArena.arena);
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(testArena.arena, LogType::Info, NWB_TEXT("old payload"), payload));
    ASSERT_TRUE(Telemetry::BuildTextLogPayload(testArena.arena, LogType::EssentialInfo, {}, payload));
    CheckPayload(payload, LogType::EssentialInfo, nullptr, 0u);
    EXPECT_FALSE(Telemetry::BuildTextLogPayload(testArena.arena, static_cast<LogType::Enum>(0xffu), {}, payload));
    EXPECT_TRUE(payload.empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

