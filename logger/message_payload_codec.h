// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


namespace MessagePayloadError{
    enum Enum : u8{
        InvalidType,
        SizeOverflow,
    };
};


class MessagePayloadCodec{
private:
    inline static constexpr TStringView s_TruncatedMessageError = NWB_TEXT("Received a truncated message");
    inline static constexpr TStringView s_MalformedMessagePayloadError = NWB_TEXT("Received a malformed message payload");

    [[nodiscard]] static constexpr b8 IsValidType(Core::Common::LogType::Enum type)noexcept{
        switch(type){
        case Core::Common::LogType::Info:
        case Core::Common::LogType::EssentialInfo:
        case Core::Common::LogType::Warning:
        case Core::Common::LogType::CriticalWarning:
        case Core::Common::LogType::Assert:
        case Core::Common::LogType::Error:
        case Core::Common::LogType::Fatal:
            return true;
        }
        return false;
    }


public:
    template<typename PayloadContainer>
    [[nodiscard]] static Expected<void, MessagePayloadError::Enum> Build(const MessageType& msg, PayloadContainer& outPayload){
        const auto& [time, type, str] = msg;
        outPayload.clear();

        if(!IsValidType(type))
            return MakeUnexpected(MessagePayloadError::InvalidType);

        usize payloadBytes = 0u;
        if(!AddBinaryReserveBytes(payloadBytes, sizeof(time)) || !AddBinaryReserveBytes(payloadBytes, sizeof(type)) || !AddBinaryRepeatedReserveBytes(payloadBytes, str.size(), sizeof(tchar)) || !AddBinaryReserveBytes(payloadBytes, sizeof(tchar)))
            return MakeUnexpected(MessagePayloadError::SizeOverflow);

        if constexpr(requires(PayloadContainer& p, usize bytes){ p.reserve(bytes); })
            outPayload.reserve(payloadBytes);

        AppendPOD(outPayload, time);
        AppendPOD(outPayload, type);
        AppendBytesNoReserveUnchecked(outPayload, str.data(), str.size() * sizeof(tchar));

        constexpr tchar s_NullTerminator = 0;
        AppendPOD(outPayload, s_NullTerminator);

        NWB_ASSERT(outPayload.size() == payloadBytes);
        return {};
    }

    [[nodiscard]] static Expected<MessageType, TStringView> Parse(LogArena& arena, const void* contents, const usize totalSize){
        if(totalSize < sizeof(Timer) + sizeof(Core::Common::LogType::Enum) + sizeof(tchar)){
            return MakeUnexpected(s_TruncatedMessageError);
        }
        if(!contents){
            return MakeUnexpected(s_MalformedMessagePayloadError);
        }

        const BinaryByteView payload{ static_cast<const u8*>(contents), totalSize };
        usize cursor = 0u;

        const auto time = ReadPOD<Timer>(payload, cursor);
        const auto type = ReadPOD<Core::Common::LogType::Enum>(payload, cursor);
        if(!time || !type){
            return MakeUnexpected(s_TruncatedMessageError);
        }

        if(!IsValidType(*type)){
            return MakeUnexpected(TStringView(NWB_TEXT("Received a message with an invalid type")));
        }

        const usize textBytes = totalSize - cursor;
        if((textBytes % sizeof(tchar)) != 0u){
            return MakeUnexpected(s_MalformedMessagePayloadError);
        }

        usize terminatorCursor = totalSize - sizeof(tchar);
        const auto terminator = ReadPOD<tchar>(payload, terminatorCursor);
        if(!terminator || *terminator != 0){
            return MakeUnexpected(TStringView(NWB_TEXT("Received a non-null-terminated message")));
        }

        // Serialized text can start at an unaligned byte offset; copy into aligned character storage before reading it.
        LogString message(arena);
        const usize messageBytes = textBytes - sizeof(tchar);
        message.resize(messageBytes / sizeof(tchar));
        if(messageBytes != 0u)
            NWB_MEMCPY(message.data(), messageBytes, payload.data() + cursor, messageBytes);
        return MakeTuple(*time, *type, Move(message));
    }
};


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

