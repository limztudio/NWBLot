// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/asset.h>
#include <global/binary.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GeometryBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_GeometryMagic = 0x47454F31u; // GEO1
inline constexpr u32 s_GeometryVersion = 1u;

#pragma pack(push, 1)
struct GeometryHeaderBinary{
    u32 magic = s_GeometryMagic;
    u32 version = s_GeometryVersion;
    u64 vertexCount = 0;
    u64 indexCount = 0;
};
#pragma pack(pop)
static_assert(sizeof(GeometryHeaderBinary) == sizeof(u32) + sizeof(u32) + sizeof(u64) + sizeof(u64), "GeometryHeaderBinary layout drifted");
static_assert(alignof(GeometryHeaderBinary) == 1u, "GeometryHeaderBinary must stay packed");
static_assert(IsStandardLayout_V<GeometryHeaderBinary>, "GeometryHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<GeometryHeaderBinary>, "GeometryHeaderBinary must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer>
[[nodiscard]] bool ReadVector(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    const u64 count,
    ValueContainer& outValues,
    const tchar* failureContext,
    const tchar* label
){
    const BinaryVectorPayloadFailure::Enum failure = ::ReadBinaryVectorPayload(binary, inOutCursor, count, outValues);
    if(failure == BinaryVectorPayloadFailure::None)
        return true;

    if(failure == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload byte size overflows"), failureContext, label);
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: malformed '{}' payload"), failureContext, label);
    }

    return false;
}


template<typename Header>
[[nodiscard]] bool ReadHeader(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    Header& outHeader,
    const u32 expectedMagic,
    const u32 expectedVersion,
    const tchar* failureContext
){
    if(!ReadPOD(binary, inOutCursor, outHeader)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: malformed header"), failureContext);
        return false;
    }

    if(outHeader.magic != expectedMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: invalid magic"), failureContext);
        return false;
    }
    if(outHeader.version != expectedVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: unsupported version {}"), failureContext, outHeader.version);
        return false;
    }

    return true;
}

[[nodiscard]] inline bool ReadComplete(
    const Core::Assets::AssetBytes& binary,
    const usize cursor,
    const tchar* failureContext
){
    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: trailing bytes detected"), failureContext);
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer>
[[nodiscard]] bool AppendVector(
    Core::Assets::AssetBytes& outBinary,
    const ValueContainer& values,
    const tchar* failureContext,
    const tchar* label
){
    const BinaryVectorPayloadFailure::Enum failure = ::AppendBinaryVectorPayload(outBinary, values);
    if(failure == BinaryVectorPayloadFailure::None)
        return true;

    if(failure == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload byte size overflows"), failureContext, label);
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload overflows output binary"), failureContext, label);
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

