// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_cook.h"
#include "skin_binary_payload.h"
#include "skin_validation.h"

#include <impl/assets_skeleton/cook_matrix.h>

#include <global/core/assets/binary_payload_io.h>
#include <global/core/assets/paths.h>
#include <global/core/common/log.h>
#include <global/core/metascript/parser.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Skin::s_AssetTypeText)
        );
        return false;
    }

    const Skin& skin = static_cast<const Skin&>(asset);
    if(!skin.validatePayload())
        return false;

    usize reserveBytes = sizeof(SkinBinaryPayload::HeaderBinary);
    const bool canReserve =
        AddBinaryVectorReserveBytes(reserveBytes, skin.influences())
        && AddBinaryVectorReserveBytes(reserveBytes, skin.inverseBindMatrices())
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SkinBinaryPayload::HeaderBinary header;
    header.meshNameHash = skin.mesh().name().hash();
    header.skeletonNameHash = skin.skeleton().name().hash();
    header.influenceCount = static_cast<u64>(skin.influences().size());
    header.inverseBindMatrixCount = static_cast<u64>(skin.inverseBindMatrices().size());
    AppendPOD(outBinary, header);

    return Core::Assets::AppendVectorPayload(
        outBinary,
        skin.influences(),
        NWB_TEXT("SkinAssetCodec::serialize"),
        NWB_TEXT("influences")
    )
        && Core::Assets::AppendVectorPayload(
            outBinary,
            skin.inverseBindMatrices(),
            NWB_TEXT("SkinAssetCodec::serialize"),
            NWB_TEXT("inverse bind matrices")
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skin_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_MeshField = "mesh";
static constexpr AStringView s_SkeletonField = "skeleton";
static constexpr AStringView s_InfluencesField = "influences";
static constexpr AStringView s_InverseBindMatricesField = "inverse_bind_matrices";
static constexpr AStringView s_JointsField = "joints";
static constexpr AStringView s_WeightsField = "weights";
static constexpr AStringView s_SkinMetaKind = "Skin";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ReadNameField(
    const Path& nwbFilePath,
    const Value& object,
    const AStringView fieldName,
    const bool required,
    Name& outName
){
    outName = NAME_NONE;

    const Value* fieldValue = FindField(object, fieldName);
    if(!fieldValue){
        if(!required)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': field '{}' is required")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': field '{}' must be a string")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const MStringView text = fieldValue->asString();
    outName = Name(AStringView(text.data(), text.size()));
    if(required && !outName){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': field '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

template<typename AssetT>
[[nodiscard]] bool ReadAssetRefField(
    const Path& nwbFilePath,
    const Value& object,
    const AStringView fieldName,
    Core::Assets::AssetRef<AssetT>& outRef
){
    Name assetName = NAME_NONE;
    if(!ReadNameField(nwbFilePath, object, fieldName, true, assetName))
        return false;

    outRef = {};
    outRef.virtualPath = assetName;
    return outRef.valid();
}

[[nodiscard]] bool ValidateSkinAssetFields(const Path& nwbFilePath, const Value& asset){
    return Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "Skin meta",
        { s_MeshField, s_SkeletonField, s_InfluencesField, s_InverseBindMatricesField }
    );
}

[[nodiscard]] bool ValidateSkinInfluenceFields(const Path& nwbFilePath, const Value& influence){
    return Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        influence,
        "Skin influence",
        { s_JointsField, s_WeightsField }
    );
}

template<usize ComponentCount>
[[nodiscard]] bool ReadNumericTuple(
    const Path& nwbFilePath,
    const Value& value,
    const AStringView label,
    const AStringView listValueKind,
    f64 (&outValues)[ComponentCount]
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}' must be a {}-component {} list")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
            , ComponentCount
            , StringConvert(listValueKind)
        );
        return false;
    }

    const auto& list = value.asList();
    for(usize componentIndex = 0u; componentIndex < ComponentCount; ++componentIndex){
        const Value& component = list[componentIndex];
        if(!component.isNumeric()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}[{}]' must be numeric")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(label)
                , componentIndex
            );
            return false;
        }

        outValues[componentIndex] = component.toDouble();
    }
    return true;
}

template<usize ComponentCount>
[[nodiscard]] bool ParseU16Tuple(
    const Path& nwbFilePath,
    const Value& value,
    const AStringView label,
    u16 (&outValues)[ComponentCount]
){
    f64 numericValues[ComponentCount] = {};
    if(!ReadNumericTuple(nwbFilePath, value, label, "integer", numericValues))
        return false;

    for(usize componentIndex = 0u; componentIndex < ComponentCount; ++componentIndex){
        const f64 numericValue = numericValues[componentIndex];
        if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue) || numericValue > static_cast<f64>(Limit<u16>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}[{}]' must be a u16 integer")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(label)
                , componentIndex
            );
            return false;
        }

        outValues[componentIndex] = static_cast<u16>(numericValue);
    }
    return true;
}

template<usize ComponentCount>
[[nodiscard]] bool ParseF32Tuple(
    const Path& nwbFilePath,
    const Value& value,
    const AStringView label,
    f32 (&outValues)[ComponentCount]
){
    f64 numericValues[ComponentCount] = {};
    if(!ReadNumericTuple(nwbFilePath, value, label, "numeric", numericValues))
        return false;

    for(usize componentIndex = 0u; componentIndex < ComponentCount; ++componentIndex){
        const f64 numericValue = numericValues[componentIndex];
        if(!IsFinite(numericValue) || numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}[{}]' is non-finite or outside f32 range")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(label)
                , componentIndex
            );
            return false;
        }

        outValues[componentIndex] = static_cast<f32>(numericValue);
    }
    return true;
}

[[nodiscard]] bool NormalizeSkinInfluenceWeights(
    const Path& nwbFilePath,
    const usize influenceIndex,
    SkinInfluence4& influence
){
    const SIMDVector weights = VectorSet(
        influence.weight[0u],
        influence.weight[1u],
        influence.weight[2u],
        influence.weight[3u]
    );
    if(!VectorIsFinite(weights, 0xFu) || !Vector4GreaterOrEqual(weights, VectorZero())){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}].weights must be finite and non-negative")
            , PathToString<tchar>(nwbFilePath)
            , influenceIndex
        );
        return false;
    }

    const SIMDVector weightSum = Vector4Dot(weights, s_SIMDOne);
    if(!VectorIsFinite(weightSum, 0xFu) || !Vector4Greater(weightSum, VectorReplicate(SkinValidation::s_Epsilon))){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}].weights must contain a positive total")
            , PathToString<tchar>(nwbFilePath)
            , influenceIndex
        );
        return false;
    }

    const SIMDVector normalizedWeights = VectorDivide(weights, weightSum);
    influence.weight[0u] = VectorGetX(normalizedWeights);
    influence.weight[1u] = VectorGetY(normalizedWeights);
    influence.weight[2u] = VectorGetZ(normalizedWeights);
    influence.weight[3u] = VectorGetW(normalizedWeights);

    return SkinValidation::ValidSkinInfluenceWeights(normalizedWeights);
}

[[nodiscard]] bool ParseSkinInfluence(
    const Path& nwbFilePath,
    const Value& influenceValue,
    const usize influenceIndex,
    SkinInfluence4& outInfluence
){
    outInfluence = {};

    if(!ValidateSkinInfluenceFields(nwbFilePath, influenceValue))
        return false;

    const Value* joints = FindField(influenceValue, s_JointsField);
    const Value* weights = FindField(influenceValue, s_WeightsField);
    if(!joints || !weights){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}] requires 'joints' and 'weights'")
            , PathToString<tchar>(nwbFilePath)
            , influenceIndex
        );
        return false;
    }

    return ParseU16Tuple(nwbFilePath, *joints, s_JointsField, outInfluence.joint)
        && ParseF32Tuple(nwbFilePath, *weights, s_WeightsField, outInfluence.weight)
        && NormalizeSkinInfluenceWeights(nwbFilePath, influenceIndex, outInfluence)
    ;
}

[[nodiscard]] bool ParseSkinInfluences(
    const Path& nwbFilePath,
    const Value& asset,
    Core::Assets::AssetVector<SkinInfluence4>& outInfluences
){
    outInfluences.clear();

    const Value* influences = FindField(asset, s_InfluencesField);
    if(!influences || !influences->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': field '{}' must be a list")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_InfluencesField)
        );
        return false;
    }

    const auto& influenceList = influences->asList();
    outInfluences.reserve(influenceList.size());
    for(usize influenceIndex = 0u; influenceIndex < influenceList.size(); ++influenceIndex){
        const Value& influenceValue = influenceList[influenceIndex];
        if(!influenceValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}] must be a map")
                , PathToString<tchar>(nwbFilePath)
                , influenceIndex
            );
            return false;
        }

        SkinInfluence4 influence;
        if(!ParseSkinInfluence(nwbFilePath, influenceValue, influenceIndex, influence))
            return false;
        outInfluences.push_back(influence);
    }

    if(outInfluences.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_InfluencesField)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseInverseBindMatrices(
    const Path& nwbFilePath,
    const Value& asset,
    Core::Assets::AssetVector<SkeletonJointMatrix>& outMatrices
){
    outMatrices.clear();

    const Value* matrices = FindField(asset, s_InverseBindMatricesField);
    if(!matrices || !matrices->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': field '{}' must be a list")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_InverseBindMatricesField)
        );
        return false;
    }

    const auto& matrixList = matrices->asList();
    outMatrices.reserve(matrixList.size());
    for(usize matrixIndex = 0u; matrixIndex < matrixList.size(); ++matrixIndex){
        SkeletonJointMatrix matrix{};
        if(!AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(
            nwbFilePath,
            matrixList[matrixIndex],
            s_SkinMetaKind,
            s_InverseBindMatricesField,
            matrix
        ))
            return false;

        if(!SkinValidation::ValidAffineJointMatrix(LoadFloat(matrix))){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': inverse_bind_matrices[{}] is not a finite invertible affine matrix")
                , PathToString<tchar>(nwbFilePath)
                , matrixIndex
            );
            return false;
        }
        outMatrices.push_back(matrix);
    }

    if(outMatrices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_InverseBindMatricesField)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateSkinInfluenceJointIndices(const Path& nwbFilePath, const SkinCookEntry& entry){
    if(entry.inverseBindMatrices.size() > static_cast<usize>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': inverse_bind_matrices count exceeds u16 joint index range")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const u32 jointCount = static_cast<u32>(entry.inverseBindMatrices.size());
    for(usize influenceIndex = 0u; influenceIndex < entry.influences.size(); ++influenceIndex){
        const SkinInfluence4& influence = entry.influences[influenceIndex];
        for(usize componentIndex = 0u; componentIndex < 4u; ++componentIndex){
            if(static_cast<u32>(influence.joint[componentIndex]) < jointCount)
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}].joints[{}] is out of inverse_bind_matrices range")
                , PathToString<tchar>(nwbFilePath)
                , influenceIndex
                , componentIndex
            );
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseSkinCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    SkinCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_skin_cook;
    static_cast<void>(scratchArena);

    outEntry = SkinCookEntry(outEntry.influences.get_allocator().arena());

    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': asset is not a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    outEntry.virtualPath = virtualPath;
    if(!outEntry.virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': virtual path must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!ValidateSkinAssetFields(nwbFilePath, asset))
        return false;
    if(
        !ReadAssetRefField(nwbFilePath, asset, s_MeshField, outEntry.mesh)
        || !ReadAssetRefField(nwbFilePath, asset, s_SkeletonField, outEntry.skeleton)
        || !ParseSkinInfluences(nwbFilePath, asset, outEntry.influences)
        || !ParseInverseBindMatrices(nwbFilePath, asset, outEntry.inverseBindMatrices)
        || !ValidateSkinInfluenceJointIndices(nwbFilePath, outEntry)
    )
        return false;

    Skin testSkin(outEntry.influences.get_allocator().arena(), outEntry.virtualPath);
    testSkin.setMesh(outEntry.mesh);
    testSkin.setSkeleton(outEntry.skeleton);
    testSkin.setPayload(Skin::InfluenceVector(outEntry.influences), Skin::InverseBindMatrixVector(outEntry.inverseBindMatrices));
    return testSkin.validatePayload();
}

bool ParseSkinCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SkinCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    Name virtualPath = NAME_NONE;
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, virtualPath, scratchArena))
        return false;
    return ParseSkinCookMetadata(virtualPath, nwbFilePath, doc.asset(), outEntry, scratchArena);
}

bool BuildSkinAsset(SkinCookEntry& skinEntry, Skin& outSkin){
    outSkin = Skin(skinEntry.influences.get_allocator().arena(), skinEntry.virtualPath);
    outSkin.setMesh(skinEntry.mesh);
    outSkin.setSkeleton(skinEntry.skeleton);
    outSkin.setPayload(Move(skinEntry.influences), Move(skinEntry.inverseBindMatrices));
    return outSkin.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

