// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "containers.h"
#include "limit.h"
#include "utf8_text.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Latin1TextError{
    enum Enum : u8{ InvalidText, TooLarge, Unsupported };
};


template<typename ArenaT>
[[nodiscard]] Expected<void, Latin1TextError::Enum> DecodeLatin1Text(const AStringView text, const usize maxBytes, AString<ArenaT>& output){
    const auto alias = ContainerDetail::SourceAliasesDestination(output, Span<const char>(text.data(), text.size()));
    if(alias){
        if(text.size() > output.size() - *alias){
            output.clear();
            return MakeUnexpected(Latin1TextError::InvalidText);
        }
        AString<ArenaT> staged(output.get_allocator().arena());
        const auto result = DecodeLatin1Text(text, maxBytes, staged);
        output = Move(staged);
        return result;
    }
    output.clear();
    if(text.size() > maxBytes)
        return MakeUnexpected(Latin1TextError::TooLarge);
    usize outputSize = text.size();
    b8 overflow = false;
    for(const char character : text){
        if(character == '\0')
            return MakeUnexpected(Latin1TextError::InvalidText);
        if(static_cast<u8>(character) >= s_Utf8OneByteMaxExclusive){
            if(outputSize == Limit<usize>::s_Max)
                overflow = true;
            else
                ++outputSize;
        }
    }
    if(overflow || outputSize > maxBytes)
        return MakeUnexpected(Latin1TextError::TooLarge);
    output.reserve(outputSize);
    Array<char, s_Utf8MaxSequenceBytes> encoded{};
    for(const char character : text){
        const auto encodedLength = EncodeUtf8CodePoint(static_cast<u8>(character), encoded);
        FOUNDATION_ASSERT(encodedLength);
        output.append(encoded.data(), *encodedLength);
    }
    return {};
}

template<typename ArenaT>
[[nodiscard]] Expected<void, Latin1TextError::Enum> EncodeLatin1Text(const AStringView text, const usize maxBytes, AString<ArenaT>& output){
    const auto alias = ContainerDetail::SourceAliasesDestination(output, Span<const char>(text.data(), text.size()));
    if(alias){
        if(text.size() > output.size() - *alias){
            output.clear();
            return MakeUnexpected(Latin1TextError::InvalidText);
        }
        AString<ArenaT> staged(output.get_allocator().arena());
        const auto result = EncodeLatin1Text(text, maxBytes, staged);
        output = Move(staged);
        return result;
    }
    output.clear();
    if(text.size() > maxBytes)
        return MakeUnexpected(Latin1TextError::TooLarge);
    if(!IsValidUtf8TextWithoutNul(text))
        return MakeUnexpected(Latin1TextError::InvalidText);
    usize offset = 0u;
    while(offset < text.size()){
        const auto decoded = DecodeUtf8CodePoint(text.substr(offset));
        if(!decoded)
            return MakeUnexpected(Latin1TextError::InvalidText);
        if(decoded->codePoint > 0xffu){
            output.clear();
            return MakeUnexpected(Latin1TextError::Unsupported);
        }
        output.push_back(static_cast<char>(decoded->codePoint));
        offset += static_cast<usize>(decoded->byteCount);
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

