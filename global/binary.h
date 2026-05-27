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


template<typename>
inline constexpr bool s_DependentFalse = false;

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
inline void AppendBytesNoReserveUnchecked(Container& outBinary, const void* bytes, const usize byteCount){
    RequireByteContainer<Container>();
    if(byteCount == 0u)
        return;

    NWB_ASSERT(bytes);

    using ByteType = typename Container::value_type;
    const ByteType* typedBytes = static_cast<const ByteType*>(bytes);
    if constexpr(requires(Container& c, const ByteType* first, const ByteType* last){ c.insert(c.end(), first, last); }){
        outBinary.insert(outBinary.end(), typedBytes, typedBytes + byteCount);
    }
    else if constexpr(requires(Container& c, ByteType value){ c.push_back(value); }){
        for(usize i = 0u; i < byteCount; ++i)
            outBinary.push_back(typedBytes[i]);
    }
    else{
        static_assert(s_DependentFalse<Container>, "binary helpers require insert or push_back support");
    }
}

template<typename Container>
inline void ReserveAppendBytesIfSupported(Container& outBinary, const usize byteCount){
    if constexpr(requires(Container& c, usize n){ c.reserve(n); })
        outBinary.reserve(outBinary.size() + byteCount);
}

template<typename Container>
inline void AppendBytesUnchecked(Container& outBinary, const void* bytes, const usize byteCount){
    RequireByteContainer<Container>();
    if(byteCount == 0u)
        return;

    ReserveAppendBytesIfSupported(outBinary, byteCount);
    AppendBytesNoReserveUnchecked(outBinary, bytes, byteCount);
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

template<typename ValueContainer>
[[nodiscard]] inline bool CanStoreValueCount(const ValueContainer& values, const usize count){
    if constexpr(requires(const ValueContainer& c){ c.max_size(); })
        return count <= values.max_size();
    else
        return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container, typename PodType>
inline void AppendPOD(Container& outBinary, const PodType& value){
    if(!BinaryDetail::CanAppendBytes(outBinary, sizeof(PodType)))
        throw RuntimeException("AppendPOD size overflow");

    BinaryDetail::AppendBytesNoReserveUnchecked(outBinary, &value, sizeof(PodType));
}

template<typename Container, typename PodType>
[[nodiscard]] inline bool ReadPOD(const Container& binary, usize& inOutOffset, PodType& outValue){
    return BinaryDetail::ReadBytes(binary, inOutOffset, &outValue, sizeof(PodType));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


};


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
    BinaryDetail::ReserveAppendBytesIfSupported(outBinary, byteCount);
    BinaryDetail::AppendBytesNoReserveUnchecked(outBinary, &textLength, sizeof(textLength));
    BinaryDetail::AppendBytesNoReserveUnchecked(outBinary, text.data(), textLength);
    return true;
}

template<typename Container, typename StringT>
[[nodiscard]] inline bool ReadString(const Container& binary, usize& inOutOffset, StringT& outText)
    requires requires(StringT& text, const char* data, usize size){ text.assign(data, size); }
{
    usize cursor = inOutOffset;
    AStringView parsedText;
    if(!BinaryDetail::ReadLengthPrefixedString(binary, cursor, parsedText))
        return false;

    outText.assign(parsedText.data(), parsedText.size());
    inOutOffset = cursor;
    return true;
}

template<typename Container>
[[nodiscard]] inline bool ReadString(const Container& binary, usize& inOutOffset, ACompactString& outText){
    usize cursor = inOutOffset;
    AStringView parsedTextView;
    if(!BinaryDetail::ReadLengthPrefixedString(binary, cursor, parsedTextView))
        return false;

    ACompactString parsedText;
    if(!parsedText.assign(parsedTextView))
        return false;

    outText = parsedText;
    inOutOffset = cursor;
    return true;
}

struct BinaryVectorPayloadFailure{
    enum Enum{
        None,
        CountOverflow,
        SourceTruncated,
        OutputOverflow
    };
};

template<typename ValueType>
[[nodiscard]] inline bool ComputeBinaryVectorPayloadBytes(const u64 count, usize& outBytes){
    static_assert(IsTriviallyCopyable_V<ValueType>, "binary vector payloads require trivially-copyable elements");

    outBytes = 0u;
    if(count > static_cast<u64>(Limit<usize>::s_Max / sizeof(ValueType)))
        return false;

    outBytes = static_cast<usize>(count) * sizeof(ValueType);
    return true;
}

template<typename Container, typename ValueContainer>
[[nodiscard]] inline BinaryVectorPayloadFailure::Enum ReadBinaryVectorPayload(
    const Container& binary,
    usize& inOutOffset,
    const u64 count,
    ValueContainer& outValues
){
    using ValueType = typename ValueContainer::value_type;
    static_assert(IsTriviallyCopyable_V<ValueType>, "binary vector payloads require trivially-copyable elements");
    BinaryDetail::RequireByteContainer<Container>();

    outValues.clear();

    usize byteCount = 0u;
    if(!ComputeBinaryVectorPayloadBytes<ValueType>(count, byteCount))
        return BinaryVectorPayloadFailure::CountOverflow;

    if(!BinaryDetail::CanReadBytes(binary, inOutOffset, byteCount))
        return BinaryVectorPayloadFailure::SourceTruncated;

    const usize valueCount = static_cast<usize>(count);
    if(!BinaryDetail::CanStoreValueCount(outValues, valueCount))
        return BinaryVectorPayloadFailure::OutputOverflow;

    if constexpr(requires(ValueContainer& c, usize n){ c.reserve(n); })
        outValues.reserve(valueCount);

    usize cursor = inOutOffset;
    for(usize i = 0u; i < valueCount; ++i){
        ValueType value = {};
        NWB_MEMCPY(&value, sizeof(ValueType), binary.data() + cursor, sizeof(ValueType));
        cursor += sizeof(ValueType);
        outValues.push_back(value);
    }

    inOutOffset = cursor;
    return BinaryVectorPayloadFailure::None;
}

template<typename Container, typename ValueContainer>
[[nodiscard]] inline BinaryVectorPayloadFailure::Enum AppendBinaryVectorPayload(
    Container& outBinary,
    const ValueContainer& values
){
    BinaryDetail::RequireByteContainer<Container>();

    using ValueType = typename ValueContainer::value_type;
    usize byteCount = 0u;
    if(!ComputeBinaryVectorPayloadBytes<ValueType>(static_cast<u64>(values.size()), byteCount))
        return BinaryVectorPayloadFailure::CountOverflow;

    if(!BinaryDetail::CanAppendBytes(outBinary, byteCount))
        return BinaryVectorPayloadFailure::OutputOverflow;

    BinaryDetail::AppendBytesUnchecked(outBinary, values.data(), byteCount);
    return BinaryVectorPayloadFailure::None;
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

    return AddBinaryReserveBytes(inOutBytes, sizeof(u32)) && AddBinaryReserveBytes(inOutBytes, text.size());
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

template<typename Container>
[[nodiscard]] inline bool AppendStringTableText(Container& outStringTable, const AStringView text, u32& outOffset){
    outOffset = Limit<u32>::s_Max;
    usize reserveBytes = outStringTable.size();
    if(!AddStringTableTextReserveBytes(reserveBytes, text))
        return false;

    const usize beginOffset = outStringTable.size();
    outOffset = static_cast<u32>(beginOffset);
    BinaryDetail::RequireByteContainer<Container>();
    BinaryDetail::ReserveAppendBytesIfSupported(outStringTable, reserveBytes - beginOffset);
    BinaryDetail::AppendBytesNoReserveUnchecked(outStringTable, text.data(), text.size());
    outStringTable.push_back(typename Container::value_type{});
    return true;
}

template<typename Container>
[[nodiscard]] inline bool ReadStringTableText(
    const Container& binary,
    const usize stringTableOffset,
    const usize stringTableByteCount,
    const u32 textOffset,
    ACompactString& outText
){
    outText.clear();
    AStringView parsedTextView;
    if(!BinaryDetail::ReadStringTableTextView(binary, stringTableOffset, stringTableByteCount, textOffset, parsedTextView))
        return false;

    ACompactString parsedText;
    if(!parsedText.assign(parsedTextView))
        return false;

    outText = parsedText;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

