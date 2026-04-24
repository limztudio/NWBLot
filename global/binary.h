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

    outBinary.resize(beginOffset + sizeof(PodType));
    NWB_MEMCPY(outBinary.data() + beginOffset, sizeof(PodType), &value, sizeof(PodType));
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

    outBinary.resize(textOffset + textLength);
    NWB_MEMCPY(outBinary.data() + lengthOffset, sizeof(textLength), &textLength, sizeof(textLength));
    if(textLength > 0)
        NWB_MEMCPY(outBinary.data() + textOffset, textLength, text.data(), textLength);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

