// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook_metadata.h"

#include <core/assets/cook_metadata.h>
#include <core/assets/metadata_fields.h>
#include <core/assets/paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinInfluenceMetadataParser final{
public:
    template<typename SkinInfluenceVectorT, typename PathArenaT>
    [[nodiscard]] static b8 parse(
        const ::Path<PathArenaT>& nwbFilePath,
        const Core::Metascript::Value& skinAsset,
        const TStringView metaKind,
        const AStringView ownerPrefix,
        const b8 normalizeWeights,
        SkinInfluenceVectorT& outInfluences
    ){
        outInfluences.clear();
        const auto field = MeshCookMetadata::FindRequiredMetadataListField(nwbFilePath, skinAsset, metaKind, s_SkinInfluencesField);
        if(!field)
            return false;

        const auto& list = (*field)->asList();
        outInfluences.reserve(list.size());
        for(usize influenceIndex = 0u; influenceIndex < list.size(); ++influenceIndex){
            const Core::Metascript::Value& influenceValue = list[influenceIndex];
            if(!influenceValue.isMap()){
                if(ownerPrefix.empty())
                    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}] must be a map"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
                else
                    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}.influences[{}]' must be a map"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(ownerPrefix), influenceIndex);
                return false;
            }

            if(normalizeWeights){
                if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, influenceValue, s_SkinInfluenceDiagnosticPrefix, { s_SkinJointsField, s_SkinWeightsField }))
                    return false;
            }

            const Core::Metascript::Value* joints = influenceValue.findField(s_SkinJointsField);
            const Core::Metascript::Value* weights = influenceValue.findField(s_SkinWeightsField);
            if(!joints || !weights){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}] requires 'joints' and 'weights'"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
                return false;
            }
            if(!joints->isList() || joints->asList().size() != s_SkinInfluenceJointCount){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component {} list"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinJointsField), s_SkinInfluenceJointCount, ConvertString(s_SkinIntegerListValueKind));
                return false;
            }
            if(!weights->isList() || weights->asList().size() != s_SkinInfluenceJointCount){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a {}-component {} list"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinWeightsField), s_SkinInfluenceJointCount, ConvertString(s_SkinNumericListValueKind));
                return false;
            }

            SkinInfluence4 influence{};
            Float4U parsedWeights{};
            for(usize componentIndex = 0u; componentIndex < s_SkinInfluenceJointCount; ++componentIndex){
                const auto parsedJoint = MeshCookMetadata::ValidateMetadataU32Value(joints->asList()[componentIndex]);
                if(!parsedJoint || *parsedJoint > static_cast<u32>(Limit<u16>::s_Max)){
                    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}[{}]' must be a u16 integer"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinJointsField), componentIndex);
                    return false;
                }
                const auto parsedWeight = MeshCookMetadata::ValidateMetadataFiniteF32Value(weights->asList()[componentIndex]);
                if(!parsedWeight){
                    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}[{}]' is non-finite or outside f32 range"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinWeightsField), componentIndex);
                    return false;
                }
                influence.joint[componentIndex] = static_cast<u16>(*parsedJoint);
                parsedWeights.raw[componentIndex] = *parsedWeight;
            }

            if(normalizeWeights){
                const auto normalizedWeights = TryNormalizeSkinInfluenceWeights(parsedWeights);
                if(!normalizedWeights){
                    if(normalizedWeights.error() == SkinInfluenceWeightFailure::NegativeWeight)
                        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}].weights must be finite and non-negative"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
                    else if(normalizedWeights.error() == SkinInfluenceWeightFailure::ZeroTotalWeight)
                        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': influences[{}].weights must contain a positive total"), metaKind, PathToString<tchar>(nwbFilePath), influenceIndex);
                    return false;
                }
                influence.weight = *normalizedWeights;
            }
            else
                influence.weight = parsedWeights;

            outInfluences.push_back(influence);
        }

        if(outInfluences.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must not be empty"), metaKind, PathToString<tchar>(nwbFilePath), ConvertString(s_SkinInfluencesField));
            return false;
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
