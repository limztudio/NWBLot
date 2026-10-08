// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/common/log.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer>
[[nodiscard]] bool ReadVectorPayload(
    const AssetBytes& binary,
    usize& inOutCursor,
    const u64 count,
    ValueContainer& outValues,
    const TStringView failureContext,
    const TStringView label
){
    const auto result = ::ReadBinaryVectorPayload(binary, inOutCursor, count, outValues);
    if(result)
        return true;

    if(result.error() == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: '{}' payload byte size overflows"), failureContext, label);
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: malformed '{}' payload"), failureContext, label);
    }

    return false;
}

template<typename HeaderT>
[[nodiscard]] Expected<HeaderT> ReadMagicHeaderPayload(
    const AssetBytes& binary,
    usize& inOutCursor,
    const u32 expectedMagic,
    const TStringView failureContext,
    const TStringView assetType
){
    const auto header = ReadPOD<HeaderT>(binary, inOutCursor);
    if(!header){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: malformed header"), failureContext);
        return MakeUnexpected(Failure{});
    }

    if(header->magic != expectedMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: invalid {} asset format; recook required"), failureContext, assetType);
        return MakeUnexpected(Failure{});
    }

    return header;
}

[[nodiscard]] inline bool ReadCompletePayload(
    const AssetBytes& binary,
    const usize cursor,
    const TStringView failureContext
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
[[nodiscard]] bool AppendVectorPayload(
    AssetBytes& outBinary,
    const ValueContainer& values,
    const TStringView failureContext,
    const TStringView label
){
    const auto result = ::AppendBinaryVectorPayload(outBinary, values);
    if(result)
        return true;

    if(result.error() == BinaryVectorPayloadFailure::CountOverflow){
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


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

