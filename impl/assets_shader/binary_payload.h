// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SpvMagic = 0x07230203u;
inline constexpr usize s_SpvWordBytes = sizeof(u32);
inline constexpr u32 s_AssetPayloadMagic = 0x53484431u; // SHD1
inline constexpr u32 s_AssetPayloadVersion = 1u;
inline constexpr usize s_AssetPayloadHeaderBytes = sizeof(u32) * 2u;

namespace BytecodeValidationFailure{
enum Enum : u8{
    None,
    InvalidSize,
    InvalidMagic,
};
};

[[nodiscard]] inline bool IsValidBytecodeSize(const usize byteSize){
    return byteSize >= s_SpvWordBytes && (byteSize % s_SpvWordBytes) == 0u;
}

template<typename BinaryContainer>
[[nodiscard]] inline BytecodeValidationFailure::Enum ValidateBytecode(const BinaryContainer& binary){
    if(!IsValidBytecodeSize(binary.size()))
        return BytecodeValidationFailure::InvalidSize;

    usize cursor = 0u;
    u32 magic = 0u;
    if(!ReadPOD(binary, cursor, magic) || magic != s_SpvMagic)
        return BytecodeValidationFailure::InvalidMagic;

    return BytecodeValidationFailure::None;
}

namespace AssetPayloadFailure{
enum Enum : u8{
    None,
    InvalidHeader,
    UnsupportedVersion,
    InvalidEntryPoint,
    InvalidBytecode,
    OutputSizeOverflow,
};
};

template<typename BytecodeContainer, typename PayloadContainer>
[[nodiscard]] inline AssetPayloadFailure::Enum EncodeAssetPayload(
    const AStringView entryPoint,
    const BytecodeContainer& bytecode,
    PayloadContainer& outPayload
){
    if(entryPoint.empty() || entryPoint.size() > Limit<u32>::s_Max)
        return AssetPayloadFailure::InvalidEntryPoint;
    if(ValidateBytecode(bytecode) != BytecodeValidationFailure::None)
        return AssetPayloadFailure::InvalidBytecode;

    const usize entryPointBytes = sizeof(u32) + entryPoint.size();
    if(entryPointBytes < entryPoint.size() || entryPointBytes > Limit<usize>::s_Max - s_AssetPayloadHeaderBytes)
        return AssetPayloadFailure::OutputSizeOverflow;
    const usize headerAndEntryPointBytes = s_AssetPayloadHeaderBytes + entryPointBytes;
    if(bytecode.size() > Limit<usize>::s_Max - headerAndEntryPointBytes)
        return AssetPayloadFailure::OutputSizeOverflow;

    const usize payloadBytes = headerAndEntryPointBytes + bytecode.size();
    outPayload.clear();
    BinaryDetail::ReserveAppendBytesIfSupported(outPayload, payloadBytes);
    AppendPOD(outPayload, s_AssetPayloadMagic);
    AppendPOD(outPayload, s_AssetPayloadVersion);
    if(!AppendString(outPayload, entryPoint))
        return AssetPayloadFailure::OutputSizeOverflow;
    BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, bytecode.data(), bytecode.size());
    return AssetPayloadFailure::None;
}

template<typename PayloadContainer, typename StringT, typename BytecodeContainer>
[[nodiscard]] inline AssetPayloadFailure::Enum DecodeAssetPayload(
    const PayloadContainer& payload,
    StringT& outEntryPoint,
    BytecodeContainer& outBytecode
){
    outEntryPoint.clear();
    outBytecode.clear();

    usize cursor = 0u;
    u32 magic = 0u;
    u32 version = 0u;
    if(!ReadPOD(payload, cursor, magic) || !ReadPOD(payload, cursor, version) || magic != s_AssetPayloadMagic)
        return AssetPayloadFailure::InvalidHeader;
    if(version != s_AssetPayloadVersion)
        return AssetPayloadFailure::UnsupportedVersion;
    if(!ReadString(payload, cursor, outEntryPoint) || outEntryPoint.empty())
        return AssetPayloadFailure::InvalidEntryPoint;

    const BinaryByteView bytecode{
        payload.data() + cursor,
        payload.size() - cursor
    };
    if(ValidateBytecode(bytecode) != BytecodeValidationFailure::None)
        return AssetPayloadFailure::InvalidBytecode;
    if(!BinaryDetail::CanAppendBytes(outBytecode, bytecode.size()))
        return AssetPayloadFailure::OutputSizeOverflow;

    BinaryDetail::ReserveAppendBytesIfSupported(outBytecode, bytecode.size());
    BinaryDetail::AppendBytesNoReserveUnchecked(outBytecode, bytecode.data(), bytecode.size());
    return AssetPayloadFailure::None;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

