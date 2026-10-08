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

[[nodiscard]] inline bool IsValidBytecodeSize(const usize byteSize)noexcept{
    return byteSize >= s_SpvWordBytes && (byteSize % s_SpvWordBytes) == 0u;
}

template<typename BinaryContainer>
[[nodiscard]] inline BytecodeValidationFailure::Enum ValidateBytecode(const BinaryContainer& binary){
    if(!IsValidBytecodeSize(binary.size()))
        return BytecodeValidationFailure::InvalidSize;

    usize cursor = 0u;
    const auto magic = ReadPOD<u32>(binary, cursor);
    if(!magic || *magic != s_SpvMagic)
        return BytecodeValidationFailure::InvalidMagic;

    return BytecodeValidationFailure::None;
}

namespace AssetPayloadEncodeFailure{
    enum Enum : u8{
        None,
        InvalidEntryPoint,
        InvalidBytecode,
        OutputSizeOverflow,
    };
};

template<typename BytecodeContainer, typename PayloadContainer>
[[nodiscard]] inline AssetPayloadEncodeFailure::Enum EncodeAssetPayload(
    const AStringView entryPoint,
    const BytecodeContainer& bytecode,
    PayloadContainer& outPayload
){
    if(entryPoint.empty() || entryPoint.size() > Limit<u32>::s_Max)
        return AssetPayloadEncodeFailure::InvalidEntryPoint;
    if(ValidateBytecode(bytecode) != BytecodeValidationFailure::None)
        return AssetPayloadEncodeFailure::InvalidBytecode;

    const usize entryPointBytes = sizeof(u32) + entryPoint.size();
    if(entryPointBytes < entryPoint.size() || entryPointBytes > Limit<usize>::s_Max - s_AssetPayloadHeaderBytes)
        return AssetPayloadEncodeFailure::OutputSizeOverflow;
    const usize headerAndEntryPointBytes = s_AssetPayloadHeaderBytes + entryPointBytes;
    if(bytecode.size() > Limit<usize>::s_Max - headerAndEntryPointBytes)
        return AssetPayloadEncodeFailure::OutputSizeOverflow;

    const usize payloadBytes = headerAndEntryPointBytes + bytecode.size();
    outPayload.clear();
    BinaryDetail::ReserveAppendBytesIfSupported(outPayload, payloadBytes);
    AppendPOD(outPayload, s_AssetPayloadMagic);
    AppendPOD(outPayload, s_AssetPayloadVersion);
    if(!AppendString(outPayload, entryPoint))
        return AssetPayloadEncodeFailure::OutputSizeOverflow;
    BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, bytecode.data(), bytecode.size());
    return AssetPayloadEncodeFailure::None;
}

namespace AssetPayloadDecodeFailure{
    enum Enum : u8{
        InvalidHeader,
        UnsupportedVersion,
        InvalidEntryPoint,
        InvalidBytecode,
    };
};

struct AssetPayloadView{
    AStringView entryPoint;
    BinaryByteView bytecode;
};

// The result borrows the serialized payload, which must outlive both views.
template<typename PayloadContainer>
[[nodiscard]] inline Expected<AssetPayloadView, AssetPayloadDecodeFailure::Enum> DecodeAssetPayload(const PayloadContainer& payload){
    usize cursor = 0u;
    const auto magic = ReadPOD<u32>(payload, cursor);
    const auto version = ReadPOD<u32>(payload, cursor);
    if(!magic || !version || *magic != s_AssetPayloadMagic)
        return MakeUnexpected(AssetPayloadDecodeFailure::InvalidHeader);
    if(*version != s_AssetPayloadVersion)
        return MakeUnexpected(AssetPayloadDecodeFailure::UnsupportedVersion);
    const auto entryPoint = ReadString(payload, cursor);
    if(!entryPoint || entryPoint->empty())
        return MakeUnexpected(AssetPayloadDecodeFailure::InvalidEntryPoint);

    const BinaryByteView bytecode{ payload.data() + cursor, payload.size() - cursor };
    if(ValidateBytecode(bytecode) != BytecodeValidationFailure::None)
        return MakeUnexpected(AssetPayloadDecodeFailure::InvalidBytecode);
    return AssetPayloadView{ *entryPoint, bytecode };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

