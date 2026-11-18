// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <logger/client/module.h>
#include <logger/message_payload_codec.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::LoggerProtocolSdk{

#if defined(FOUNDATION_PLATFORM_WINDOWS)
inline constexpr b8 s_Windows = true;
#else
inline constexpr b8 s_Windows = false;
#endif
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
inline constexpr b8 s_DesktopLinux = true;
#else
inline constexpr b8 s_DesktopLinux = false;
#endif

inline constexpr usize s_NativeCharacterBytes = sizeof(tchar);

template<usize N>
[[nodiscard]] constexpr TStringView Literal(const tchar (&text)[N])noexcept{ return { text, N - 1u }; }

[[nodiscard]] inline b8 EnqueueTelemetry(Log::Client& client, const BinaryByteView bytes){
    return client.enqueueTelemetry(bytes.data(), bytes.size());
}

[[nodiscard]] inline Log::LogString NativeText(HeapStorage& storage, const TStringView text){ return Log::LogString(text, storage.arena); }

[[nodiscard]] inline Log::MessageType Message(HeapStorage& storage, const Timer time, const Core::Common::LogType::Enum type, const TStringView text){
    return MakeTuple(time, type, NativeText(storage, text));
}

[[nodiscard]] inline Expected<Log::MessageType, TStringView> Parse(HeapStorage& storage, const Span<const u8> bytes){
    return Log::MessagePayloadCodec::Parse(storage.arena, bytes.data(), bytes.size());
}

[[nodiscard]] inline TStringView MessageText(const Log::MessageType& message)noexcept{ return Get<2u>(message); }

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

