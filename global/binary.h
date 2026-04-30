// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "compact_string.h"
#include "limit.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BinaryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
inline void RequireByteContainer(){
    using ByteType = typename Container::value_type;
    static_assert(sizeof(ByteType) == 1u, "binary helpers require a byte-sized container");
}

template<typename Container>
[[nodiscard]] inline bool CanAppendBytes(const Container& outBinary, const usize byteCount){
    return outBinary.size() <= Limit<usize>::s_Max - byteCount;
}

template<typename Container>
inline void AppendBytesUnchecked(Container& outBinary, const void* bytes, const usize byteCount){
    RequireByteContainer<Container>();
    if(byteCount == 0u)
        return;

    NWB_ASSERT(bytes);

    using ByteType = typename Container::value_type;
    const ByteType* typedBytes = static_cast<const ByteType*>(bytes);

    if constexpr(requires(Container& c, usize n){ c.reserve(n); })
        outBinary.reserve(outBinary.size() + byteCount);

    if constexpr(requires(Container& c, const ByteType* p){ c.insert(c.end(), p, p); })
        outBinary.insert(outBinary.end(), typedBytes, typedBytes + byteCount);
    else{
        const usize offset = outBinary.size();
        outBinary.resize(offset + byteCount);
        NWB_MEMCPY(outBinary.data() + offset, byteCount, bytes, byteCount);
    }
}

template<typename Container>
[[nodiscard]] inline bool AppendBytes(Container& outBinary, const void* bytes, const usize byteCount){
    RequireByteContainer<Container>();
    if(!CanAppendBytes(outBinary, byteCount))
        return false;

    AppendBytesUnchecked(outBinary, bytes, byteCount);
    return true;
}

template<typename Container>
[[nodiscard]] inline bool CanReadBytes(const Container& binary, const usize offset, const usize byteCount){
    if(offset > binary.size())
        return false;
    return binary.size() - offset >= byteCount;
}

template<typename Container>
[[nodiscard]] inline bool ReadBytes(const Container& binary, usize& inOutOffset, void* outBytes, const usize byteCount){
    RequireByteContainer<Container>();
    if(!CanReadBytes(binary, inOutOffset, byteCount))
        return false;

    if(byteCount > 0u)
        NWB_MEMCPY(outBytes, byteCount, binary.data() + inOutOffset, byteCount);
    inOutOffset += byteCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container, typename PodType>
inline void AppendPOD(Container& outBinary, const PodType& value){
    if(!BinaryDetail::AppendBytes(outBinary, &value, sizeof(PodType)))
        throw RuntimeException("AppendPOD size overflow");
}

template<typename Container, typename PodType>
[[nodiscard]] inline bool ReadPOD(const Container& binary, usize& inOutOffset, PodType& outValue){
    return BinaryDetail::ReadBytes(binary, inOutOffset, &outValue, sizeof(PodType));
}


namespace BinaryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
[[nodiscard]] inline bool ReadLengthPrefixedString(const Container& binary, usize& inOutOffset, AStringView& outText){
    outText = {};

    usize cursor = inOutOffset;
    u32 textLength = 0;
    if(!ReadPOD(binary, cursor, textLength))
        return false;

    if(!CanReadBytes(binary, cursor, textLength))
        return false;

    outText = AStringView(reinterpret_cast<const char*>(binary.data() + cursor), textLength);
    cursor += textLength;
    inOutOffset = cursor;
    return true;
}

template<typename Container>
[[nodiscard]] inline bool ReadStringTableTextView(
    const Container& binary,
    const usize stringTableOffset,
    const usize stringTableByteCount,
    const u32 textOffset,
    AStringView& outText
){
    outText = {};
    if(textOffset == Limit<u32>::s_Max || static_cast<usize>(textOffset) >= stringTableByteCount)
        return false;
    if(!CanReadBytes(binary, stringTableOffset, stringTableByteCount))
        return false;

    const usize relativeOffset = static_cast<usize>(textOffset);
    const usize absoluteOffset = stringTableOffset + relativeOffset;
    const usize remainingBytes = stringTableByteCount - relativeOffset;

    usize textLength = 0u;
    while(textLength < remainingBytes && binary[absoluteOffset + textLength] != 0u)
        ++textLength;

    if(textLength == 0u || textLength >= remainingBytes)
        return false;

    outText = AStringView(reinterpret_cast<const char*>(binary.data() + absoluteOffset), textLength);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
[[nodiscard]] inline bool AppendString(Container& outBinary, const AStringView text){
    if(text.size() > Limit<u32>::s_Max)
        return false;

    const u32 textLength = static_cast<u32>(text.size());
    const usize byteCount = sizeof(u32) + textLength;
    if(!BinaryDetail::CanAppendBytes(outBinary, byteCount))
        return false;

    BinaryDetail::RequireByteContainer<Container>();
    outBinary.reserve(outBinary.size() + byteCount);
    BinaryDetail::AppendBytesUnchecked(outBinary, &textLength, sizeof(textLength));
    BinaryDetail::AppendBytesUnchecked(outBinary, text.data(), textLength);
    return true;
}

template<typename Container>
[[nodiscard]] inline bool AppendString(Container& outBinary, const CompactString& text){
    return AppendString(outBinary, text.view());
}

template<typename Container>
[[nodiscard]] inline bool ReadString(const Container& binary, usize& inOutOffset, AString& outText){
    usize cursor = inOutOffset;
    AStringView parsedText;
    if(!BinaryDetail::ReadLengthPrefixedString(binary, cursor, parsedText))
        return false;

    outText.assign(parsedText.data(), parsedText.size());
    inOutOffset = cursor;
    return true;
}

template<typename Container>
[[nodiscard]] inline bool ReadString(const Container& binary, usize& inOutOffset, CompactString& outText){
    usize cursor = inOutOffset;
    AStringView parsedTextView;
    if(!BinaryDetail::ReadLengthPrefixedString(binary, cursor, parsedTextView))
        return false;

    CompactString parsedText;
    if(!parsedText.assign(parsedTextView))
        return false;

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

    return
        AddBinaryReserveBytes(inOutBytes, sizeof(u32))
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
    BinaryDetail::RequireByteContainer<Container>();
    outStringTable.reserve(reserveBytes);
    BinaryDetail::AppendBytesUnchecked(outStringTable, text.data(), text.size());
    outStringTable.push_back(typename Container::value_type{});
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
    AStringView parsedTextView;
    if(!BinaryDetail::ReadStringTableTextView(binary, stringTableOffset, stringTableByteCount, textOffset, parsedTextView))
        return false;

    CompactString parsedText;
    if(!parsedText.assign(parsedTextView))
        return false;

    outText = parsedText;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

