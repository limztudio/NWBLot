// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "compact_string.h"
#include "expected.h"
#include "limit.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BinaryDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename>
inline constexpr b8 s_DependentFalse = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
inline void RequireByteContainer()noexcept{
    using ByteType = typename Container::value_type;
    static constexpr usize s_ByteTypeByteSize = 1u;
    static_assert(sizeof(ByteType) == s_ByteTypeByteSize, "binary helpers require a byte-sized container");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
[[nodiscard]] inline b8 CanAppendBytes(const Container& outBinary, const usize byteCount)noexcept(IsSame_V<decltype(outBinary.size()), usize> && noexcept(outBinary.size())){
    return outBinary.size() <= Limit<usize>::s_Max - byteCount;
}

template<typename Container>
inline void AppendBytesNoReserveUnchecked(Container& outBinary, const void* bytes, const usize byteCount){
    BinaryDetail::RequireByteContainer<Container>();
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
        static_assert(BinaryDetail::s_DependentFalse<Container>, "binary helpers require insert or push_back support");
    }
}

template<typename Container>
inline void ReserveAppendBytesIfSupported(Container& outBinary, const usize byteCount)noexcept([]()consteval noexcept{
    if constexpr(requires(Container& c, usize n){ c.reserve(n); })
        return noexcept(DeclVal<Container&>().reserve(DeclVal<Container&>().size() + DeclVal<const usize&>()));
    else
        return true;
}()){
    if constexpr(requires(Container& c, usize n){ c.reserve(n); })
        outBinary.reserve(outBinary.size() + byteCount);
}

template<typename Container>
inline void AppendBytesUnchecked(Container& outBinary, const void* bytes, const usize byteCount){
    BinaryDetail::RequireByteContainer<Container>();
    if(byteCount == 0u)
        return;

    ReserveAppendBytesIfSupported(outBinary, byteCount);
    AppendBytesNoReserveUnchecked(outBinary, bytes, byteCount);
}

template<typename Container>
[[nodiscard]] inline b8 CanReadBytes(const Container& binary, const usize offset, const usize byteCount)noexcept(IsSame_V<decltype(binary.size()), usize> && noexcept(binary.size())){
    if(offset > binary.size())
        return false;
    return binary.size() - offset >= byteCount;
}

template<typename Container>
[[nodiscard]] inline b8 ReadBytes(const Container& binary, const usize byteCount, usize& inOutOffset, void* outBytes)noexcept(noexcept(CanReadBytes(binary, inOutOffset, byteCount)) && IsPointer_V<decltype(binary.data())> && noexcept(binary.data())){
    BinaryDetail::RequireByteContainer<Container>();
    if(!CanReadBytes(binary, inOutOffset, byteCount))
        return false;

    if(byteCount > 0u)
        NWB_MEMCPY(outBytes, byteCount, binary.data() + inOutOffset, byteCount);
    inOutOffset += byteCount;
    return true;
}

template<typename ValueContainer>
[[nodiscard]] inline b8 CanStoreValueCount(const ValueContainer& values, const usize count)noexcept([]()consteval noexcept{
    if constexpr(requires(const ValueContainer& c){ c.max_size(); })
        return noexcept(DeclVal<void(*)(b8)noexcept>()(DeclVal<const usize&>() <= DeclVal<const ValueContainer&>().max_size()));
    else
        return true;
}()){
    if constexpr(requires(const ValueContainer& c){ c.max_size(); })
        return count <= values.max_size();
    else
        return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BinaryByteView{
    using value_type = u8;


public:
    const u8* bytes = nullptr;
    usize byteCount = 0u;


public:
    [[nodiscard]] u8 operator[](const usize index)const noexcept{ return bytes[index]; }


public:
    [[nodiscard]] b8 empty()const noexcept{ return byteCount == 0u; }

    [[nodiscard]] usize size()const noexcept{ return byteCount; }

    [[nodiscard]] const u8* data()const noexcept{ return bytes; }

    [[nodiscard]] const u8* begin()const noexcept{ return bytes; }

    [[nodiscard]] const u8* end()const noexcept{ return bytes + byteCount; }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container, typename PodType>
inline void AppendPOD(Container& outBinary, const PodType& value){
    if(!CanAppendBytes(outBinary, sizeof(PodType)))
        throw RuntimeException("AppendPOD size overflow");

    AppendBytesNoReserveUnchecked(outBinary, &value, sizeof(PodType));
}

template<typename PodType, typename Container>
[[nodiscard]] inline Expected<PodType> ReadPOD(const Container& binary, usize& inOutOffset)noexcept(noexcept(PodType{}) && IsNothrowMoveConstructible_V<PodType> && noexcept(ReadBytes(binary, sizeof(PodType), inOutOffset, &DeclVal<PodType&>()))){
    static_assert(IsTriviallyCopyable_V<PodType>, "binary POD reads require trivially-copyable values");
    PodType value = {};
    if(!ReadBytes(binary, sizeof(PodType), inOutOffset, &value))
        return MakeUnexpected(Failure{});
    return value;
}

template<typename Container, typename CharT>
inline void AppendTextBytesNoReserveUnchecked(Container& outBinary, const BasicStringView<CharT> text){
    if(!text.empty())
        AppendBytesNoReserveUnchecked(outBinary, text.data(), text.size() * sizeof(CharT));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
[[nodiscard]] inline b8 SkipBytes(const Container& binary, const usize byteCount, usize& inOutOffset)noexcept(noexcept(CanReadBytes(binary, inOutOffset, byteCount))){
    BinaryDetail::RequireByteContainer<Container>();
    if(!CanReadBytes(binary, inOutOffset, byteCount))
        return false;

    inOutOffset += byteCount;
    return true;
}

template<typename Container>
[[nodiscard]] inline Expected<AStringView> ReadStringTableTextView(const Container& binary, const usize stringTableOffset, const usize stringTableByteCount, const u32 textOffset)noexcept(
    noexcept(CanReadBytes(binary, stringTableOffset, stringTableByteCount))
    && noexcept(static_cast<b8>(DeclVal<usize&>() < DeclVal<const usize&>() && binary[DeclVal<usize>()] != 0u))
    && noexcept(Expected<AStringView>(AStringView(reinterpret_cast<const char*>(binary.data() + DeclVal<const usize&>()), DeclVal<usize&>())))
){
    if(textOffset == Limit<u32>::s_Max || static_cast<usize>(textOffset) >= stringTableByteCount)
        return MakeUnexpected(Failure{});
    if(!CanReadBytes(binary, stringTableOffset, stringTableByteCount))
        return MakeUnexpected(Failure{});

    const usize relativeOffset = static_cast<usize>(textOffset);
    const usize absoluteOffset = stringTableOffset + relativeOffset;
    const usize remainingBytes = stringTableByteCount - relativeOffset;

    usize textLength = 0u;
    while(textLength < remainingBytes && binary[absoluteOffset + textLength] != 0u)
        ++textLength;

    if(textLength == 0u || textLength >= remainingBytes)
        return MakeUnexpected(Failure{});

    return AStringView(reinterpret_cast<const char*>(binary.data() + absoluteOffset), textLength);
}

template<typename Container>
[[nodiscard]] inline b8 AppendString(Container& outBinary, const AStringView text){
    if(text.size() > Limit<u32>::s_Max)
        return false;

    const u32 textLength = static_cast<u32>(text.size());
    const usize byteCount = sizeof(u32) + textLength;
    if(!CanAppendBytes(outBinary, byteCount))
        return false;

    BinaryDetail::RequireByteContainer<Container>();
    ReserveAppendBytesIfSupported(outBinary, byteCount);
    AppendBytesNoReserveUnchecked(outBinary, &textLength, sizeof(textLength));
    AppendBytesNoReserveUnchecked(outBinary, text.data(), textLength);
    return true;
}

template<typename Container>
[[nodiscard]] inline Expected<AStringView> ReadString(const Container& binary, usize& inOutOffset)noexcept(noexcept(ReadPOD<u32>(binary, inOutOffset)) && IsPointer_V<decltype(binary.data())> && noexcept(binary.data())){
    usize cursor = inOutOffset;
    const auto textLength = ReadPOD<u32>(binary, cursor);
    if(!textLength)
        return MakeUnexpected(textLength.error());

    if(!CanReadBytes(binary, cursor, *textLength))
        return MakeUnexpected(Failure{});

    const AStringView text(reinterpret_cast<const char*>(binary.data() + cursor), *textLength);
    cursor += *textLength;
    inOutOffset = cursor;
    return text;
}

namespace BinaryVectorPayloadFailure{
    enum Enum : u8{
        CountOverflow,
        SourceTruncated,
        OutputOverflow
    };
};

template<typename ValueType>
[[nodiscard]] inline Expected<usize, BinaryVectorPayloadFailure::Enum> ComputeBinaryVectorPayloadBytes(const u64 count)noexcept{
    static_assert(IsTriviallyCopyable_V<ValueType>, "binary vector payloads require trivially-copyable elements");

    if(count > static_cast<u64>(Limit<usize>::s_Max / sizeof(ValueType)))
        return MakeUnexpected(BinaryVectorPayloadFailure::CountOverflow);

    return static_cast<usize>(count) * sizeof(ValueType);
}

template<typename Container, typename ValueContainer>
[[nodiscard]] inline Expected<void, BinaryVectorPayloadFailure::Enum> ReadBinaryVectorPayload(const Container& binary, const u64 count, usize& inOutOffset, ValueContainer& outValues)noexcept([]()consteval noexcept{
    using ValueType = typename ValueContainer::value_type;
    constexpr b8 s_CommonIsNothrow =
        noexcept(DeclVal<ValueContainer&>().clear())
        && noexcept(CanReadBytes(DeclVal<const Container&>(), DeclVal<usize&>(), DeclVal<const usize&>()))
        && noexcept(CanStoreValueCount(DeclVal<ValueContainer&>(), DeclVal<const usize&>()))
    ;
    constexpr b8 s_ReserveIsNothrow = []()consteval noexcept{
        if constexpr(requires(ValueContainer& c, usize n){ c.reserve(n); })
            return noexcept(DeclVal<ValueContainer&>().reserve(DeclVal<const usize&>()));
        else
            return true;
    }();
    if constexpr(IsDefaultConstructible_V<ValueType> && requires(ValueContainer& c, usize n){ c.resize(n); c.data(); }){
        return s_CommonIsNothrow && s_ReserveIsNothrow
            && noexcept(DeclVal<ValueContainer&>().resize(DeclVal<const usize&>()))
            && noexcept(NWB_MEMCPY(DeclVal<ValueContainer&>().data(), DeclVal<const usize&>(), DeclVal<const Container&>().data() + DeclVal<usize&>(), DeclVal<const usize&>()))
        ;
    }
    else{
        return s_CommonIsNothrow && s_ReserveIsNothrow && noexcept(ValueType{}) && IsNothrowDestructible_V<ValueType>
            && noexcept(NWB_MEMCPY(&DeclVal<ValueType&>(), sizeof(ValueType), DeclVal<const Container&>().data() + DeclVal<usize&>(), sizeof(ValueType)))
            && noexcept(DeclVal<ValueContainer&>().push_back(DeclVal<ValueType&>()))
        ;
    }
}()){
    using ValueType = typename ValueContainer::value_type;
    static_assert(IsTriviallyCopyable_V<ValueType>, "binary vector payloads require trivially-copyable elements");
    BinaryDetail::RequireByteContainer<Container>();

    outValues.clear();

    const auto byteCount = ComputeBinaryVectorPayloadBytes<ValueType>(count);
    if(!byteCount)
        return MakeUnexpected(byteCount.error());

    if(!CanReadBytes(binary, inOutOffset, *byteCount))
        return MakeUnexpected(BinaryVectorPayloadFailure::SourceTruncated);

    const usize valueCount = static_cast<usize>(count);
    if(!CanStoreValueCount(outValues, valueCount))
        return MakeUnexpected(BinaryVectorPayloadFailure::OutputOverflow);

    if constexpr(requires(ValueContainer& c, usize n){ c.reserve(n); })
        outValues.reserve(valueCount);

    usize cursor = inOutOffset;
    if constexpr(IsDefaultConstructible_V<ValueType> && requires(ValueContainer& c, usize n){ c.resize(n); c.data(); }){
        outValues.resize(valueCount);
        if(*byteCount > 0u)
            NWB_MEMCPY(outValues.data(), *byteCount, binary.data() + cursor, *byteCount);
        cursor += *byteCount;
    }
    else{
        for(usize i = 0u; i < valueCount; ++i){
            ValueType value = {};
            NWB_MEMCPY(&value, sizeof(ValueType), binary.data() + cursor, sizeof(ValueType));
            cursor += sizeof(ValueType);
            outValues.push_back(value);
        }
    }

    inOutOffset = cursor;
    return {};
}

template<typename Container, typename ValueContainer>
[[nodiscard]] inline Expected<void, BinaryVectorPayloadFailure::Enum> AppendBinaryVectorPayload(Container& outBinary, const ValueContainer& values){
    BinaryDetail::RequireByteContainer<Container>();

    using ValueType = typename ValueContainer::value_type;
    const auto byteCount = ComputeBinaryVectorPayloadBytes<ValueType>(static_cast<u64>(values.size()));
    if(!byteCount)
        return MakeUnexpected(byteCount.error());

    if(!CanAppendBytes(outBinary, *byteCount))
        return MakeUnexpected(BinaryVectorPayloadFailure::OutputOverflow);

    AppendBytesUnchecked(outBinary, values.data(), *byteCount);
    return {};
}

[[nodiscard]] inline b8 AddBinaryReserveBytes(usize& inOutBytes, const usize additionalBytes)noexcept{
    if(additionalBytes > Limit<usize>::s_Max - inOutBytes)
        return false;

    inOutBytes += additionalBytes;
    return true;
}

[[nodiscard]] inline b8 AddBinaryRepeatedReserveBytes(usize& inOutBytes, const usize count, const usize bytesPerItem)noexcept{
    if(bytesPerItem != 0u && count > Limit<usize>::s_Max / bytesPerItem)
        return false;

    return AddBinaryReserveBytes(inOutBytes, count * bytesPerItem);
}

[[nodiscard]] inline b8 AddBinaryStringReserveBytes(usize& inOutBytes, const AStringView text)noexcept{
    if(text.size() > Limit<u32>::s_Max)
        return false;

    return AddBinaryReserveBytes(inOutBytes, sizeof(u32)) && AddBinaryReserveBytes(inOutBytes, text.size());
}

template<typename Container>
[[nodiscard]] inline b8 AddBinaryVectorReserveBytes(usize& inOutBytes, const Container& values)noexcept(IsSame_V<decltype(values.size()), usize> && noexcept(values.size())){
    using ValueType = typename Container::value_type;
    return AddBinaryRepeatedReserveBytes(inOutBytes, values.size(), sizeof(ValueType));
}

[[nodiscard]] inline b8 AddStringTableTextReserveBytes(usize& inOutBytes, const AStringView text)noexcept{
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
[[nodiscard]] inline Expected<u32> AppendStringTableText(Container& outStringTable, const AStringView text){
    usize reserveBytes = outStringTable.size();
    if(!AddStringTableTextReserveBytes(reserveBytes, text))
        return MakeUnexpected(Failure{});

    const usize beginOffset = outStringTable.size();
    BinaryDetail::RequireByteContainer<Container>();
    ReserveAppendBytesIfSupported(outStringTable, reserveBytes - beginOffset);
    AppendBytesNoReserveUnchecked(outStringTable, text.data(), text.size());
    outStringTable.push_back(typename Container::value_type{});
    return static_cast<u32>(beginOffset);
}

template<typename Container>
[[nodiscard]] inline Expected<ACompactString> ReadStringTableText(const Container& binary, const usize stringTableOffset, const usize stringTableByteCount, const u32 textOffset)noexcept(
    noexcept(ReadStringTableTextView(binary, stringTableOffset, stringTableByteCount, textOffset))
    && noexcept(ACompactString())
    && noexcept(DeclVal<ACompactString&>().assign(DeclVal<const AStringView&>()))
    && noexcept(Expected<ACompactString>(DeclVal<ACompactString&&>()))
){
    const auto parsedTextView = ReadStringTableTextView(binary, stringTableOffset, stringTableByteCount, textOffset);
    if(!parsedTextView)
        return MakeUnexpected(parsedTextView.error());

    ACompactString parsedText;
    if(!parsedText.assign(*parsedTextView))
        return MakeUnexpected(Failure{});

    return parsedText;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

