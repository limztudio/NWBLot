// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer, typename FailureContextT, typename LabelT>
[[nodiscard]] b8 ReadVectorPayload(const Span<const u8> binary, const u64 count, const FailureContextT& failureContext, const LabelT& label, usize& inOutCursor, ValueContainer& outValues){
    const auto result = ::ReadBinaryVectorPayload(binary, count, inOutCursor, outValues);
    if(result)
        return true;

    if(result.error() == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: '{}' payload byte size overflows"), ConvertString(failureContext), ConvertString(label));
    }
    else{
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: malformed '{}' payload"), ConvertString(failureContext), ConvertString(label));
    }

    return false;
}

template<typename HeaderT, typename FailureContextT, typename AssetTypeT>
[[nodiscard]] Expected<HeaderT> ReadMagicHeaderPayload(const Span<const u8> binary, const u32 expectedMagic, const FailureContextT& failureContext, const AssetTypeT& assetType, usize& inOutCursor){
    const auto header = ReadPOD<HeaderT>(binary, inOutCursor);
    if(!header){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: malformed header"), ConvertString(failureContext));
        return MakeUnexpected(Failure{});
    }

    if(header->magic != expectedMagic){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: invalid {} asset format; recook required"), ConvertString(failureContext), ConvertString(assetType));
        return MakeUnexpected(Failure{});
    }

    return header;
}

[[nodiscard]] b8 ReadCompletePayload(Span<const u8> binary, usize cursor, TStringView failureContext);

template<typename FailureContextT>
requires(!IsConvertible_V<const FailureContextT&, TStringView> && IsConvertible_V<const FailureContextT&, AStringView>)
[[nodiscard]] inline b8 ReadCompletePayload(const Span<const u8> binary, const usize cursor, const FailureContextT& failureContext){
    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: trailing bytes detected"), ConvertString(AStringView(failureContext)));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueContainer, typename FailureContextT, typename LabelT>
[[nodiscard]] b8 AppendVectorPayload(const ValueContainer& values, const FailureContextT& failureContext, const LabelT& label, AssetBytes& outBinary){
    const auto result = ::AppendBinaryVectorPayload(outBinary, values);
    if(result)
        return true;

    if(result.error() == BinaryVectorPayloadFailure::CountOverflow){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: '{}' payload byte size overflows"), ConvertString(failureContext), ConvertString(label));
    }
    else{
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: '{}' payload overflows output binary"), ConvertString(failureContext), ConvertString(label));
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

