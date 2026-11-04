// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_cook.h"
#include "cook_metadata.h"
#include "cook_source_streams.h"

#include <impl/assets_skeleton/cook_matrix.h>

#include <core/assets/binary_payload_io.h>
#include <core/assets/cook_metadata.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SkinAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, s_SkinSerializeContext))
        return false;

    const Skin& skin = static_cast<const Skin&>(asset);
    if(!skin.validatePayload())
        return false;

    usize reserveBytes = sizeof(SkinHeaderBinary);
    const b8 canReserve = AddBinaryVectorReserveBytes(reserveBytes, skin.influences()) && AddBinaryVectorReserveBytes(reserveBytes, skin.inverseBindMatrices());

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    const SkinHeaderBinary header = MakeSkinHeaderBinary(
        skin.mesh().name().hash(),
        skin.skeleton().name().hash(),
        skin.influences().size(),
        skin.inverseBindMatrices().size()
    );
    AppendPOD(outBinary, header);

    return
        Core::Assets::AppendVectorPayload(skin.influences(), s_SkinSerializeContext, s_SkinInfluencesPayloadLabel, outBinary)
        && Core::Assets::AppendVectorPayload(skin.inverseBindMatrices(), s_SkinSerializeContext, s_SkinInverseBindMatricesPayloadLabel, outBinary)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skin_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::Assets::AssetVector<SkeletonJointMatrix>> ParseInverseBindMatrices(const Path& nwbFilePath, const Value& asset, Core::Assets::AssetArena& arena){
    Core::Assets::AssetVector<SkeletonJointMatrix> values(arena);

    const auto matrices = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkinInverseBindMatricesField);
    if(!matrices)
        return MakeUnexpected(matrices.error());

    const auto& matrixList = (*matrices)->asList();
    values.reserve(matrixList.size());
    for(usize matrixIndex = 0u; matrixIndex < matrixList.size(); ++matrixIndex){
        const auto matrix = AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(nwbFilePath, matrixList[matrixIndex], s_SkinMetaKind, s_SkinInverseBindMatricesField);
        if(!matrix)
            return MakeUnexpected(Failure{});

        if(!IsValidSkinInverseBindMatrix(LoadFloat(*matrix))){
            NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': inverse_bind_matrices[{}] is not a finite invertible affine matrix"), PathToString<tchar>(nwbFilePath), matrixIndex);
            return MakeUnexpected(Failure{});
        }
        values.push_back(*matrix);
    }

    if(values.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': '{}' must not be empty"), PathToString<tchar>(nwbFilePath), ConvertString(s_SkinInverseBindMatricesField));
        return MakeUnexpected(Failure{});
    }
    return values;
}

[[nodiscard]] b8 ValidateSkinInfluenceJointIndices(const Path& nwbFilePath, const SkinCookEntry& entry){
    if(entry.inverseBindMatrices.size() > static_cast<usize>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': inverse_bind_matrices count exceeds u16 joint index range"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    const u32 jointCount = static_cast<u32>(entry.inverseBindMatrices.size());
    for(usize influenceIndex = 0u; influenceIndex < entry.influences.size(); ++influenceIndex){
        const Expected<usize> componentIndex = TryFindOutOfRangeSkinInfluenceJoint(entry.influences[influenceIndex], jointCount);
        if(!componentIndex)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("Skin meta '{}': influences[{}].joints[{}] is out of inverse_bind_matrices range"), PathToString<tchar>(nwbFilePath), influenceIndex, *componentIndex);
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SkinCookEntry> ParseSkinCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    return Core::Assets::ParseCookMetadataDocument<SkinCookEntry>(
        assetRoot,
        virtualRoot,
        nwbFilePath,
        doc,
        s_SkinMetaDiagnosticPrefix,
        [&nwbFilePath, &arena](const Name virtualPath, const Core::Metascript::Value& asset){
            return ParseSkinCookMetadata(virtualPath, nwbFilePath, asset, arena);
        },
        scratchArena
    );
}

Expected<SkinCookEntry> ParseSkinCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena){
    using namespace __hidden_skin_cook;

    SkinCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_SkinMetaDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, &IsAllowedSkinMetadataField))
        return MakeUnexpected(Failure{});
    auto meshResult = Core::Assets::ReadMetadataAssetRefField<Mesh>(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkinMetaMeshField, true);
    if(!meshResult)
        return MakeUnexpected(Failure{});
    entry.mesh = *meshResult;
    auto skeletonResult = Core::Assets::ReadMetadataAssetRefField<Skeleton>(nwbFilePath, asset, s_SkinMetaDiagnosticPrefix, s_SkinMetaSkeletonField, true);
    if(!skeletonResult)
        return MakeUnexpected(Failure{});
    entry.skeleton = *skeletonResult;
    if(!MeshCookSourceStreams::ParseSourceSkinInfluencesInto(nwbFilePath, asset, NWB_TEXT("Skin"), {}, true, entry.influences))
        return MakeUnexpected(Failure{});
    auto inverseBindMatricesResult = ParseInverseBindMatrices(nwbFilePath, asset, arena);
    if(!inverseBindMatricesResult)
        return MakeUnexpected(Failure{});
    entry.inverseBindMatrices = Move(*inverseBindMatricesResult);
    if(!ValidateSkinInfluenceJointIndices(nwbFilePath, entry))
        return MakeUnexpected(Failure{});

    return entry;
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

