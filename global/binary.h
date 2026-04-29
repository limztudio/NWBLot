// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "compact_string.h"
#include "limit.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container, typename PodType>
inline void AppendPOD(Container& outBinary, const PodType& value){
    const usize beginOffset = outBinary.size();
    if(beginOffset > Limit<usize>::s_Max - sizeof(PodType))
        throw RuntimeException("AppendPOD size overflow");

    using ByteType = typename Container::value_type;
    static_assert(sizeof(ByteType) == 1u, "AppendPOD requires a byte-sized output container");
    const ByteType* bytes = reinterpret_cast<const ByteType*>(&value);
    outBinary.insert(outBinary.end(), bytes, bytes + sizeof(PodType));
}

template<typename Container, typename PodType>
[[nodiscard]] inline bool ReadPOD(const Container& binary, usize& inOutOffset, PodType& outValue){
    if(inOutOffset > binary.size())
        return false;
    if(binary.size() - inOutOffset < sizeof(PodType))
        return false;

    NWB_MEMCPY(&outValue, sizeof(PodType), binary.data() + inOutOffset, sizeof(PodType));
    inOutOffset += sizeof(PodType);
    return true;
}

template<typename Container>
[[nodiscard]] inline bool AppendString(Container& outBinary, const AStringView text){
    if(text.size() > Limit<u32>::s_Max)
        return false;

    const usize lengthOffset = outBinary.size();
    if(lengthOffset > Limit<usize>::s_Max - sizeof(u32))
        return false;

    const u32 textLength = static_cast<u32>(text.size());
    const usize textOffset = lengthOffset + sizeof(u32);
    if(textOffset > Limit<usize>::s_Max - textLength)
        return false;

    using ByteType = typename Container::value_type;
    static_assert(sizeof(ByteType) == 1u, "AppendString requires a byte-sized output container");
    outBinary.reserve(textOffset + textLength);
    const ByteType* lengthBytes = reinterpret_cast<const ByteType*>(&textLength);
    outBinary.insert(outBinary.end(), lengthBytes, lengthBytes + sizeof(textLength));
    if(textLength > 0){
        const ByteType* textBytes = reinterpret_cast<const ByteType*>(text.data());
        outBinary.insert(outBinary.end(), textBytes, textBytes + textLength);
    }
    return true;
}

template<typename Container>
[[nodiscard]] inline bool AppendString(Container& outBinary, const CompactString& text){
    return AppendString(outBinary, text.view());
}

template<typename Container>
[[nodiscard]] inline bool ReadString(const Container& binary, usize& inOutOffset, AString& outText){
    usize cursor = inOutOffset;
    u32 textLength = 0;
    if(!ReadPOD(binary, cursor, textLength))
        return false;

    if(cursor > binary.size())
        return false;
    if(binary.size() - cursor < textLength)
        return false;

    outText.assign(reinterpret_cast<const char*>(binary.data() + cursor), textLength);
    cursor += textLength;

    inOutOffset = cursor;
    return true;
}

template<typename Container>
[[nodiscard]] inline bool ReadString(const Container& binary, usize& inOutOffset, CompactString& outText){
    usize cursor = inOutOffset;
    u32 textLength = 0;
    if(!ReadPOD(binary, cursor, textLength))
        return false;

    if(cursor > binary.size())
        return false;
    if(binary.size() - cursor < textLength)
        return false;

    CompactString parsedText;
    if(!parsedText.assign(AStringView(reinterpret_cast<const char*>(binary.data() + cursor), textLength)))
        return false;
    cursor += textLength;

    outText = parsedText;
    inOutOffset = cursor;
    return true;
}

[[nodiscard]] inline bool AddBinaryReserveBytes(usize& inOutBytes, const usize additionalBytes){
    if(additionalBytes > Limit<usize>::s_Max - inOutBytes)
        return false;

    inOutBytes += additionalBytes;
    return true;
}

[[nodiscard]] inline bool AddBinaryRepeatedReserveBytes(usize& inOutBytes, const usize count, const usize bytesPerItem){
    if(bytesPerItem != 0u && count > Limit<usize>::s_Max / bytesPerItem)
        return false;

    return AddBinaryReserveBytes(inOutBytes, count * bytesPerItem);
}

[[nodiscard]] inline bool AddBinaryStringReserveBytes(usize& inOutBytes, const AStringView text){
    if(text.size() > Limit<u32>::s_Max)
        return false;

    return AddBinaryReserveBytes(inOutBytes, sizeof(u32))
        && AddBinaryReserveBytes(inOutBytes, text.size())
    ;
}

[[nodiscard]] inline bool AddBinaryStringReserveBytes(usize& inOutBytes, const CompactString& text){
    return AddBinaryStringReserveBytes(inOutBytes, text.view());
}

template<typename Container>
[[nodiscard]] inline bool AddBinaryVectorReserveBytes(usize& inOutBytes, const Container& values){
    using ValueType = typename Container::value_type;
    return AddBinaryRepeatedReserveBytes(inOutBytes, values.size(), sizeof(ValueType));
}

[[nodiscard]] inline bool AddStringTableTextReserveBytes(usize& inOutBytes, const AStringView text){
    if(text.empty())
        return false;
    if(text.size() > Limit<usize>::s_Max - 1u)
        return false;

    const usize byteCount = text.size() + 1u;
    constexpr usize s_U32Max = static_cast<usize>(Limit<u32>::s_Max);
    if(byteCount > s_U32Max)
        return false;
    if(inOutBytes > s_U32Max - byteCount)
        return false;

    return AddBinaryReserveBytes(inOutBytes, byteCount);
}

[[nodiscard]] inline bool AddStringTableTextReserveBytes(usize& inOutBytes, const CompactString& text){
    return AddStringTableTextReserveBytes(inOutBytes, text.view());
}

template<typename Container>
[[nodiscard]] inline bool AppendStringTableText(Container& outStringTable, const AStringView text, u32& outOffset){
    outOffset = Limit<u32>::s_Max;
    usize reserveBytes = outStringTable.size();
    if(!AddStringTableTextReserveBytes(reserveBytes, text))
        return false;

    const usize beginOffset = outStringTable.size();
    outOffset = static_cast<u32>(beginOffset);
    using ByteType = typename Container::value_type;
    static_assert(sizeof(ByteType) == 1u, "AppendStringTableText requires a byte-sized output container");
    outStringTable.reserve(reserveBytes);
    const ByteType* textBytes = reinterpret_cast<const ByteType*>(text.data());
    outStringTable.insert(outStringTable.end(), textBytes, textBytes + text.size());
    outStringTable.push_back(ByteType{});
    return true;
}

template<typename Container>
[[nodiscard]] inline bool AppendStringTableText(Container& outStringTable, const CompactString& text, u32& outOffset){
    return AppendStringTableText(outStringTable, text.view(), outOffset);
}

template<typename Container>
[[nodiscard]] inline bool ReadStringTableText(
    const Container& binary,
    const usize stringTableOffset,
    const usize stringTableByteCount,
    const u32 textOffset,
    CompactString& outText
){
    outText.clear();
    if(textOffset == Limit<u32>::s_Max || static_cast<usize>(textOffset) >= stringTableByteCount)
        return false;
    if(stringTableOffset > binary.size())
        return false;
    if(stringTableByteCount > binary.size() - stringTableOffset)
        return false;

    const usize relativeOffset = static_cast<usize>(textOffset);
    const usize absoluteOffset = stringTableOffset + relativeOffset;
    const usize remainingBytes = stringTableByteCount - relativeOffset;
    usize textLength = 0u;
    while(textLength < remainingBytes && binary[absoluteOffset + textLength] != 0u)
        ++textLength;

    if(textLength == 0u || textLength >= remainingBytes)
        return false;

    CompactString parsedText;
    if(!parsedText.assign(AStringView(reinterpret_cast<const char*>(binary.data() + absoluteOffset), textLength)))
        return false;

    outText = parsedText;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

