// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "joint_types.h"

#include <global/core/common/log.h>
#include <global/core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsSkeletonCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ParseSkeletonJointMatrixValue(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView metaKind,
    const AStringView label,
    SkeletonJointMatrix& outMatrix
){
    outMatrix = MakeIdentitySkeletonJointMatrix();

    if(!value.isList() || value.asList().size() != 3u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a 3x4 affine matrix")
            , StringConvert(metaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    const auto& rows = value.asList();
    for(usize rowIndex = 0u; rowIndex < 3u; ++rowIndex){
        const Core::Metascript::Value& row = rows[rowIndex];
        if(!row.isList() || row.asList().size() != 4u){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' row {} must have 4 numeric values")
                , StringConvert(metaKind)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(label)
                , rowIndex
            );
            return false;
        }

        f32 rowValues[4u] = {};
        const auto& columns = row.asList();
        for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex){
            const Core::Metascript::Value& column = columns[columnIndex];
            if(!column.isNumeric()){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' row {} column {} must be numeric")
                    , StringConvert(metaKind)
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(label)
                    , rowIndex
                    , columnIndex
                );
                return false;
            }

            const f64 numericValue = column.toDouble();
            if(
                !IsFinite(numericValue)
                || numericValue < static_cast<f64>(Limit<f32>::s_Min)
                || numericValue > static_cast<f64>(Limit<f32>::s_Max)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' row {} column {} is non-finite or outside f32 range")
                    , StringConvert(metaKind)
                    , PathToString<tchar>(nwbFilePath)
                    , StringConvert(label)
                    , rowIndex
                    , columnIndex
                );
                return false;
            }

            rowValues[columnIndex] = static_cast<f32>(numericValue);
        }

        outMatrix.rows[rowIndex] = Float4(rowValues[0u], rowValues[1u], rowValues[2u], rowValues[3u]);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

