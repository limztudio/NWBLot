// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_shader/types.adhd.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline BytecodeValidationFailure::Enum ValidateBytecode(const Span<const u8> binary)noexcept{
    if(!IsValidBytecodeSize(binary.size()))
        return BytecodeValidationFailure::InvalidSize;

    usize cursor = 0u;
    const auto magic = ReadPOD<u32>(binary, cursor);
    if(!magic || *magic != s_SpvMagic)
        return BytecodeValidationFailure::InvalidMagic;

    return BytecodeValidationFailure::None;
}

template<typename PayloadContainer>
[[nodiscard]] inline AssetPayloadEncodeFailure::Enum EncodeAssetPayload(const AStringView entryPoint, const Span<const u8> bytecode, PayloadContainer& outPayload){
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
    ReserveAppendBytesIfSupported(outPayload, payloadBytes);
    AppendPOD(outPayload, s_AssetPayloadMagic);
    AppendPOD(outPayload, s_AssetPayloadVersion);
    if(!AppendString(outPayload, entryPoint))
        return AssetPayloadEncodeFailure::OutputSizeOverflow;
    AppendBytesNoReserveUnchecked(outPayload, bytecode.data(), bytecode.size());
    return AssetPayloadEncodeFailure::None;
}

struct AssetPayloadView{
    AStringView entryPoint;
    BinaryByteView bytecode;
};

// The result borrows the serialized payload, which must outlive both views.
[[nodiscard]] inline Expected<AssetPayloadView, AssetPayloadDecodeFailure::Enum> DecodeAssetPayload(const Span<const u8> payload)noexcept{
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

