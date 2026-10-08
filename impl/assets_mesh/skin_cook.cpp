// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_cook.h"
#include "skin_binary_payload.h"
#include "skin_validation.h"

#include <impl/assets_skeleton/cook_matrix.h>

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("SkinAssetCodec::serialize")))
        return false;

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
static constexpr AStringView s_SkinMetaDiagnosticPrefix = "Skin meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateSkinAssetFields(const Path& nwbFilePath, const Value& asset){
    return Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        s_SkinMetaDiagnosticPrefix,
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
[[nodiscard]] Expected<Array<f64, ComponentCount>> ReadNumericTuple(
    const Path& nwbFilePath,
    const Value& value,
    const AStringView label,
    const AStringView listValueKind
){
    Array<f64, ComponentCount> values{};
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}' must be a {}-component {} list")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
            , ComponentCount
            , StringConvert(listValueKind)
        );
        return MakeUnexpected(Failure{});
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
            return MakeUnexpected(Failure{});
        }

        values[componentIndex] = component.toDouble();
    }
    return values;
}

template<usize ComponentCount>
[[nodiscard]] Expected<Array<u16, ComponentCount>> ParseU16Tuple(
    const Path& nwbFilePath,
    const Value& value,
    const AStringView label
){
    Array<u16, ComponentCount> values{};
    const auto numericValues = ReadNumericTuple<ComponentCount>(nwbFilePath, value, label, "integer");
    if(!numericValues)
        return MakeUnexpected(Failure{});

    for(usize componentIndex = 0u; componentIndex < ComponentCount; ++componentIndex){
        const f64 numericValue = (*numericValues)[componentIndex];
        if(!IsFinite(numericValue) || numericValue < 0.0 || numericValue != Floor(numericValue) || numericValue > static_cast<f64>(Limit<u16>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}[{}]' must be a u16 integer")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(label)
                , componentIndex
            );
            return MakeUnexpected(Failure{});
        }

        values[componentIndex] = static_cast<u16>(numericValue);
    }
    return values;
}

template<usize ComponentCount>
[[nodiscard]] Expected<Array<f32, ComponentCount>> ParseF32Tuple(
    const Path& nwbFilePath,
    const Value& value,
    const AStringView label
){
    Array<f32, ComponentCount> values{};
    const auto numericValues = ReadNumericTuple<ComponentCount>(nwbFilePath, value, label, "numeric");
    if(!numericValues)
        return MakeUnexpected(Failure{});

    for(usize componentIndex = 0u; componentIndex < ComponentCount; ++componentIndex){
        const f64 numericValue = (*numericValues)[componentIndex];
        if(!IsFinite(numericValue) || numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}[{}]' is non-finite or outside f32 range")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(label)
                , componentIndex
            );
            return MakeUnexpected(Failure{});
        }

        values[componentIndex] = static_cast<f32>(numericValue);
    }
    return values;
}

[[nodiscard]] Expected<SIMDVector> NormalizeSkinInfluenceWeights(
    const Path& nwbFilePath,
    const usize influenceIndex,
    const SIMDVector weights
){
    if(!VectorIsFinite(weights, VectorComponentMask::s_XYZW) || !Vector4GreaterOrEqual(weights, VectorZero())){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}].weights must be finite and non-negative")
            , PathToString<tchar>(nwbFilePath)
            , influenceIndex
        );
        return MakeUnexpected(Failure{});
    }

    const SIMDVector weightSum = Vector4Dot(weights, s_SIMDOne);
    if(!VectorIsFinite(weightSum, VectorComponentMask::s_XYZW) || !Vector4Greater(weightSum, VectorReplicate(SkinValidation::s_Epsilon))){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}].weights must contain a positive total")
            , PathToString<tchar>(nwbFilePath)
            , influenceIndex
        );
        return MakeUnexpected(Failure{});
    }

    const SIMDVector normalizedWeights = VectorDivide(weights, weightSum);
    if(!SkinValidation::ValidSkinInfluenceWeights(normalizedWeights))
        return MakeUnexpected(Failure{});
    return normalizedWeights;
}

[[nodiscard]] Expected<SkinInfluence4> ParseSkinInfluence(
    const Path& nwbFilePath,
    const Value& influenceValue,
    const usize influenceIndex
){
    SkinInfluence4 influence{};

    if(!ValidateSkinInfluenceFields(nwbFilePath, influenceValue))
        return MakeUnexpected(Failure{});

    const Value* joints = FindField(influenceValue, s_JointsField);
    const Value* weights = FindField(influenceValue, s_WeightsField);
    if(!joints || !weights){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}] requires 'joints' and 'weights'")
            , PathToString<tchar>(nwbFilePath)
            , influenceIndex
        );
        return MakeUnexpected(Failure{});
    }

    const auto parsedJoints = ParseU16Tuple<s_SkinInfluenceJointCount>(nwbFilePath, *joints, s_JointsField);
    if(!parsedJoints)
        return MakeUnexpected(Failure{});
    const auto weightValues = ParseF32Tuple<s_SkinInfluenceJointCount>(nwbFilePath, *weights, s_WeightsField);
    if(!weightValues)
        return MakeUnexpected(Failure{});
    Float4U parsedWeights;
    for(usize component = 0u; component < s_SkinInfluenceJointCount; ++component){
        influence.joint[component] = (*parsedJoints)[component];
        parsedWeights.raw[component] = (*weightValues)[component];
    }
    const auto normalizedWeights = NormalizeSkinInfluenceWeights(nwbFilePath, influenceIndex, LoadFloat(parsedWeights));
    if(!normalizedWeights)
        return MakeUnexpected(Failure{});

    StoreFloat(*normalizedWeights, influence.weight);
    return influence;
}

[[nodiscard]] Expected<Core::Assets::AssetVector<SkinInfluence4>> ParseSkinInfluences(
    const Path& nwbFilePath,
    const Value& asset,
    Core::Assets::AssetArena& arena
){
    Core::Assets::AssetVector<SkinInfluence4> values(arena);

    const Value* influences = Core::Assets::FindMetadataListField(nwbFilePath, asset, "Skin meta", s_InfluencesField);
    if(!influences)
        return MakeUnexpected(Failure{});

    const auto& influenceList = influences->asList();
    values.reserve(influenceList.size());
    for(usize influenceIndex = 0u; influenceIndex < influenceList.size(); ++influenceIndex){
        const Value& influenceValue = influenceList[influenceIndex];
        if(!influenceValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}] must be a map")
                , PathToString<tchar>(nwbFilePath)
                , influenceIndex
            );
            return MakeUnexpected(Failure{});
        }

        const auto influence = ParseSkinInfluence(nwbFilePath, influenceValue, influenceIndex);
        if(!influence)
            return MakeUnexpected(Failure{});
        values.push_back(*influence);
    }

    if(values.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_InfluencesField)
        );
        return MakeUnexpected(Failure{});
    }
    return values;
}

[[nodiscard]] Expected<Core::Assets::AssetVector<SkeletonJointMatrix>> ParseInverseBindMatrices(
    const Path& nwbFilePath,
    const Value& asset,
    Core::Assets::AssetArena& arena
){
    Core::Assets::AssetVector<SkeletonJointMatrix> values(arena);

    const Value* matrices = Core::Assets::FindMetadataListField(nwbFilePath, asset, "Skin meta", s_InverseBindMatricesField);
    if(!matrices)
        return MakeUnexpected(Failure{});

    const auto& matrixList = matrices->asList();
    values.reserve(matrixList.size());
    for(usize matrixIndex = 0u; matrixIndex < matrixList.size(); ++matrixIndex){
        const auto matrix = AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(
            nwbFilePath,
            matrixList[matrixIndex],
            s_SkinMetaKind,
            s_InverseBindMatricesField
        );
        if(!matrix)
            return MakeUnexpected(Failure{});

        if(!MatrixIsInvertibleAffine(LoadFloat(*matrix), SkinValidation::s_Epsilon, SkinValidation::s_Epsilon)){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': inverse_bind_matrices[{}] is not a finite invertible affine matrix")
                , PathToString<tchar>(nwbFilePath)
                , matrixIndex
            );
            return MakeUnexpected(Failure{});
        }
        values.push_back(*matrix);
    }

    if(values.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}' must not be empty")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_InverseBindMatricesField)
        );
        return MakeUnexpected(Failure{});
    }
    return values;
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
        for(usize componentIndex = 0u; componentIndex < s_SkinInfluenceJointCount; ++componentIndex){
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


Expected<SkinCookEntry> ParseSkinCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena
){
    using namespace __hidden_skin_cook;

    SkinCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, "Skin meta"))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, "Skin meta"))
        return MakeUnexpected(Failure{});
    if(!ValidateSkinAssetFields(nwbFilePath, asset))
        return MakeUnexpected(Failure{});
    auto meshResult = Core::Assets::ReadMetadataAssetRefField<Mesh>(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_MeshField, true);
    if(!meshResult)
        return MakeUnexpected(Failure{});
    entry.mesh = *meshResult;
    auto skeletonResult = Core::Assets::ReadMetadataAssetRefField<Skeleton>(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkeletonField, true);
    if(!skeletonResult)
        return MakeUnexpected(Failure{});
    entry.skeleton = *skeletonResult;
    auto influencesResult = ParseSkinInfluences(nwbFilePath, asset, arena);
    if(!influencesResult)
        return MakeUnexpected(Failure{});
    entry.influences = Move(*influencesResult);
    auto inverseBindMatricesResult = ParseInverseBindMatrices(nwbFilePath, asset, arena);
    if(!inverseBindMatricesResult)
        return MakeUnexpected(Failure{});
    entry.inverseBindMatrices = Move(*inverseBindMatricesResult);
    if(!ValidateSkinInfluenceJointIndices(nwbFilePath, entry))
        return MakeUnexpected(Failure{});

    Skin testSkin(entry.influences.get_allocator().arena(), entry.virtualPath);
    testSkin.setMesh(entry.mesh);
    testSkin.setSkeleton(entry.skeleton);
    testSkin.setPayload(Skin::InfluenceVector(entry.influences), Skin::InverseBindMatrixVector(entry.inverseBindMatrices));
    if(!testSkin.validatePayload())
        return MakeUnexpected(Failure{});
    return entry;
}

Expected<SkinCookEntry> ParseSkinCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    Name virtualPath = s_NameNone;
    auto virtualPathResult = Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    virtualPath = *virtualPathResult;
    return ParseSkinCookMetadata(virtualPath, nwbFilePath, doc.asset(), arena);
}

Expected<Skin> BuildSkinAsset(SkinCookEntry& skinEntry, Core::Assets::AssetArena& arena){
    Skin asset(arena, skinEntry.virtualPath);
    asset.setMesh(skinEntry.mesh);
    asset.setSkeleton(skinEntry.skeleton);
    asset.setPayload(Move(skinEntry.influences), Move(skinEntry.inverseBindMatrices));
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

