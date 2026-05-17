// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "skinned_geometry_validation.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinnedGeometryValidation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void LogRestVertexPayloadFailure(
    const tchar* contextText,
    const tchar* subjectText,
    const TString& sourceText,
    const RuntimePayloadFailureInfo& failure){
    switch(failure.restVertexFailure){
    case RestVertexPayloadFailure::NonFiniteData:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' rest vertex {} contains non-finite data")
            , contextText
            , subjectText
            , sourceText
            , failure.vertexIndex
        );
        break;
    case RestVertexPayloadFailure::DegenerateFrame:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' rest vertex {} has a degenerate normal/tangent frame")
            , contextText
            , subjectText
            , sourceText
            , failure.vertexIndex
        );
        break;
    case RestVertexPayloadFailure::InvalidFrame:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' rest vertex {} has an invalid normal/tangent frame")
            , contextText
            , subjectText
            , sourceText
            , failure.vertexIndex
        );
        break;
    case RestVertexPayloadFailure::None:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' rest vertex {} is invalid")
            , contextText
            , subjectText
            , sourceText
            , failure.vertexIndex
        );
        break;
    }
}

inline void LogRuntimePayloadFailure(
    const tchar* contextText,
    const tchar* subjectText,
    const TString& sourceText,
    const RuntimePayloadFailureInfo& failure){
    switch(failure.reason){
    case RuntimePayloadFailure::IncompleteRestIndexPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' has incomplete rest/index payload")
            , contextText
            , subjectText
            , sourceText
        );
        break;
    case RuntimePayloadFailure::VertexIndexCountLimit:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' exceeds u32 vertex/index count limits")
            , contextText
            , subjectText
            , sourceText
        );
        break;
    case RuntimePayloadFailure::IndexCountNotTriangleList:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' index count {} is not a multiple of 3")
            , contextText
            , subjectText
            , sourceText
            , failure.count
        );
        break;
    case RuntimePayloadFailure::InvalidRestVertex:
        LogRestVertexPayloadFailure(contextText, subjectText, sourceText, failure);
        break;
    case RuntimePayloadFailure::IndexOutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' index {} exceeds {} vertices")
            , contextText
            , subjectText
            , sourceText
            , failure.vertexId
            , failure.count
        );
        break;
    case RuntimePayloadFailure::DegenerateTriangle:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' triangle {} is degenerate")
            , contextText
            , subjectText
            , sourceText
            , failure.indexBase / 3u
        );
        break;
    case RuntimePayloadFailure::ZeroAreaTriangle:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' triangle {} has zero area")
            , contextText
            , subjectText
            , sourceText
            , failure.indexBase / 3u
        );
        break;
    case RuntimePayloadFailure::SkinCountMismatch:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' skin count {} does not match vertex count {}")
            , contextText
            , subjectText
            , sourceText
            , failure.count
            , failure.expectedCount
        );
        break;
    case RuntimePayloadFailure::SkinMissingSkeleton:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' has skin but no skeleton joint count")
            , contextText
            , subjectText
            , sourceText
        );
        break;
    case RuntimePayloadFailure::SkeletonJointCountLimit:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' skeleton joint count {} exceeds skin stream limits")
            , contextText
            , subjectText
            , sourceText
            , failure.count
        );
        break;
    case RuntimePayloadFailure::InvalidInverseBindMatrices:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' inverse bind matrices are invalid")
            , contextText
            , subjectText
            , sourceText
        );
        break;
    case RuntimePayloadFailure::InvalidSkinInfluence:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' skin influence {} is invalid")
            , contextText
            , subjectText
            , sourceText
            , failure.vertexIndex
        );
        break;
    case RuntimePayloadFailure::SkinJointOutOfRange:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' skin joint {} for vertex {} exceeds skeleton joint count {}")
            , contextText
            , subjectText
            , sourceText
            , failure.failedJoint
            , failure.vertexIndex
            , failure.count
        );
        break;
    case RuntimePayloadFailure::None:
        break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

