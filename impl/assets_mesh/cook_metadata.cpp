// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MeshCookMetadataDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AccumulateFlattenedValueLeafCount(const Core::Metascript::Value& value, usize& inOutCount)noexcept{
    if(value.isList()){
        for(const Core::Metascript::Value& child : value.asList()){
            if(!AccumulateFlattenedValueLeafCount(child, inOutCount))
                return false;
        }
        return true;
    }

    if(inOutCount == Limit<usize>::s_Max)
        return false;

    ++inOutCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshCookMetadata::IsAllowedMeshAssetField(const AStringView fieldName)noexcept{
    return
        fieldName == s_MeshPositionsFieldNameView
        || fieldName == s_MeshNormalsFieldNameView
        || fieldName == s_MeshTangentsFieldNameView
        || fieldName == s_MeshUv0FieldNameView
        || fieldName == s_MeshColorsFieldNameView
        || fieldName == s_MeshVertexRefsFieldNameView
        || fieldName == s_MeshIndicesFieldNameView
    ;
}

Expected<usize> MeshCookMetadata::CountFlattenedValueLeaves(const Core::Metascript::Value& value)noexcept{
    usize count = 0u;
    if(!MeshCookMetadataDetail::AccumulateFlattenedValueLeafCount(value, count))
        return MakeUnexpected(Failure{});
    return count;
}

ScratchString MeshCookMetadata::MakeIndexedLabel(const AStringView baseLabel, const usize index, Core::Alloc::ScratchArena& arena){
    Array<char, s_DecimalTextBufferBytes> indexBuffer{};
    const AStringView indexText = FormatDecimal(index, indexBuffer);
    NWB_ASSERT(!indexText.empty());

    ScratchString label{arena};
    label.reserve(baseLabel.size() + indexText.size() + s_IndexedLabelBracketReserve);
    label.append(baseLabel.data(), baseLabel.size());
    label += '[';
    label.append(indexText.data(), indexText.size());
    label += ']';
    return label;
}

Expected<f32, MetadataF32ValueFailure::Enum> MeshCookMetadata::ValidateMetadataFiniteF32Value(const Core::Metascript::Value& value)noexcept{
    if(!value.isNumeric())
        return Unexpected<MetadataF32ValueFailure::Enum>(MetadataF32ValueFailure::NotNumeric);

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue))
        return Unexpected<MetadataF32ValueFailure::Enum>(MetadataF32ValueFailure::NonFinite);
    if(numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max))
        return Unexpected<MetadataF32ValueFailure::Enum>(MetadataF32ValueFailure::OutOfRange);

    return static_cast<f32>(numericValue);
}

Expected<u32, MetadataU32ValueFailure::Enum> MeshCookMetadata::ValidateMetadataU32Value(const Core::Metascript::Value& value)noexcept{
    if(!value.isNumeric())
        return Unexpected<MetadataU32ValueFailure::Enum>(MetadataU32ValueFailure::NotNumeric);

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue))
        return Unexpected<MetadataU32ValueFailure::Enum>(MetadataU32ValueFailure::NonIntegerOrNegative);
    if(numericValue > static_cast<f64>(Limit<u32>::s_Max))
        return Unexpected<MetadataU32ValueFailure::Enum>(MetadataU32ValueFailure::OutOfRange);

    return static_cast<u32>(numericValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

