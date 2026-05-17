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


inline void LogMorphPayloadFailure(
    const tchar* contextText,
    const tchar* subjectText,
    const TString& sourceText,
    const Vector<SkinnedGeometryMorph>& morphs,
    const MorphPayloadFailureInfo& failure){
    const TString morphNameText = MorphPayloadFailureMorphNameText(morphs, failure);

    switch(failure.reason){
    case MorphPayloadFailure::MorphCountLimit:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' morph count exceeds u32 limits")
            , contextText
            , subjectText
            , sourceText
        );
        break;
    case MorphPayloadFailure::EmptyMorph:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' morph {} is unnamed or empty")
            , contextText
            , subjectText
            , sourceText
            , failure.morphIndex
        );
        break;
    case MorphPayloadFailure::DuplicateMorphName:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' contains duplicate morph '{}'")
            , contextText
            , subjectText
            , sourceText
            , morphNameText
        );
        break;
    case MorphPayloadFailure::MorphDeltaCountLimit:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' morph '{}' delta count exceeds u32 limits")
            , contextText
            , subjectText
            , sourceText
            , morphNameText
        );
        break;
    case MorphPayloadFailure::InvalidMorphDelta:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' morph '{}' delta {} is invalid")
            , contextText
            , subjectText
            , sourceText
            , morphNameText
            , failure.deltaIndex
        );
        break;
    case MorphPayloadFailure::DuplicateMorphDeltaVertex:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' morph '{}' has duplicate vertex {}")
            , contextText
            , subjectText
            , sourceText
            , morphNameText
            , failure.vertexId
        );
        break;
    case MorphPayloadFailure::None:
        break;
    }
}

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
    const Vector<SkinnedGeometryMorph>& morphs,
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
    case RuntimePayloadFailure::SourceSampleCountMismatch:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' source sample count {} does not match vertex count {}")
            , contextText
            , subjectText
            , sourceText
            , failure.count
            , failure.expectedCount
        );
        break;
    case RuntimePayloadFailure::InvalidSourceSample:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' source sample {} is invalid")
            , contextText
            , subjectText
            , sourceText
            , failure.vertexIndex
        );
        break;
    case RuntimePayloadFailure::EditMaskCountMismatch:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' edit mask count {} does not match triangle count {}")
            , contextText
            , subjectText
            , sourceText
            , failure.count
            , failure.expectedCount
        );
        break;
    case RuntimePayloadFailure::InvalidEditMask:
        NWB_LOGGER_ERROR(NWB_TEXT("{}: {} '{}' edit mask {} is invalid")
            , contextText
            , subjectText
            , sourceText
            , failure.indexBase / 3u
        );
        break;
    case RuntimePayloadFailure::MorphPayload:
        LogMorphPayloadFailure(contextText, subjectText, sourceText, morphs, failure.morphFailure);
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

