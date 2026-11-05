// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "cook_matrix.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/cook_metadata.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<SkeletonCookJoint> ParseSkeletonJoint(const Path& nwbFilePath, const Value& jointValue){
    SkeletonCookJoint joint{};

    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, jointValue, s_SkeletonJointDiagnosticPrefix, &IsAllowedSkeletonJointMetadataField))
        return MakeUnexpected(Failure{});
    auto nameResult = Core::Assets::ReadMetadataNameField(nwbFilePath, jointValue, s_SkeletonJointMetaKind, s_SkeletonNameField, true);
    if(!nameResult)
        return MakeUnexpected(Failure{});
    joint.name = *nameResult;
    auto parentResult = Core::Assets::ReadMetadataNameField(nwbFilePath, jointValue, s_SkeletonJointMetaKind, s_SkeletonParentField, false);
    if(!parentResult)
        return MakeUnexpected(Failure{});
    joint.parent = *parentResult;

    const Value* localBindPose = jointValue.findField(s_SkeletonLocalBindPoseField);
    if(!localBindPose)
        return joint;

    auto matrixResult = AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(nwbFilePath, *localBindPose, s_SkeletonMetaKind, s_SkeletonLocalBindPoseField);
    if(!matrixResult)
        return MakeUnexpected(Failure{});
    joint.localBindPose = *matrixResult;
    return joint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SkeletonJointMatrix> AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(const Path& nwbFilePath, const Core::Metascript::Value& value, const AStringView metaKind, const AStringView label){
    SkeletonJointMatrix matrix = ::MatrixIdentity<Float34>();

    if(!value.isList() || value.asList().size() != s_SkeletonJointMatrixRowCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' must be a 3x4 affine matrix"), ConvertString(metaKind), PathToString<tchar>(nwbFilePath), ConvertString(label));
        return MakeUnexpected(Failure{});
    }

    const auto& rows = value.asList();
    for(usize rowIndex = 0u; rowIndex < s_SkeletonJointMatrixRowCount; ++rowIndex){
        const Core::Metascript::Value& row = rows[rowIndex];
        if(!row.isList() || row.asList().size() != s_SkeletonJointMatrixColumnCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' row {} must have 4 numeric values"), ConvertString(metaKind), PathToString<tchar>(nwbFilePath), ConvertString(label), rowIndex);
            return MakeUnexpected(Failure{});
        }

        Array<f32, s_SkeletonJointMatrixColumnCount> rowValues{};
        const auto& columns = row.asList();
        for(usize columnIndex = 0u; columnIndex < s_SkeletonJointMatrixColumnCount; ++columnIndex){
            const Core::Metascript::Value& column = columns[columnIndex];
            auto columnIndexResult = Core::Assets::TryDecodeMetadataFiniteF32(column);
            if(!columnIndexResult){
                if(!column.isNumeric()){
                    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' row {} column {} must be numeric"), ConvertString(metaKind), PathToString<tchar>(nwbFilePath), ConvertString(label), rowIndex, columnIndex);
                    return MakeUnexpected(Failure{});
                }

                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': '{}' row {} column {} is non-finite or outside f32 range"), ConvertString(metaKind), PathToString<tchar>(nwbFilePath), ConvertString(label), rowIndex, columnIndex);
                return MakeUnexpected(Failure{});
            }
            rowValues[columnIndex] = *columnIndexResult;
        }

        matrix.rows[rowIndex] = Float4(rowValues[0u], rowValues[1u], rowValues[2u], rowValues[3u]);
    }

    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SkeletonAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("SkeletonAssetCodec::serialize")))
        return false;

    const Skeleton& skeleton = *MakeNotNull(checked_cast<const Skeleton*>(&asset));
    Core::Alloc::ScratchArena scratchArena(s_AssetsSkeletonSerializeArena);
    if(!skeleton.validatePayload(scratchArena))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> jointNameHashes(scratchArena);
    return SerializeSkeletonBinaryPayload(skeleton.joints(), skeleton.jointIndices(), jointNameHashes, outBinary);
}

Expected<SkeletonCookEntry> ParseSkeletonCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_skeleton_cook;

    SkeletonCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SkeletonMetaDiagnosticPrefix))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_SkeletonMetaDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SkeletonMetaDiagnosticPrefix, &IsAllowedSkeletonMetadataField))
        return MakeUnexpected(Failure{});

    const auto joints = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_SkeletonMetaDiagnosticPrefix, s_SkeletonJointsField);
    if(!joints)
        return MakeUnexpected(joints.error());

    const auto& jointList = (*joints)->asList();
    entry.joints.reserve(jointList.size());
    for(usize jointIndex = 0u; jointIndex < jointList.size(); ++jointIndex){
        const Value& jointValue = jointList[jointIndex];
        if(!jointValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': joints[{}] must be a map"), PathToString<tchar>(nwbFilePath), jointIndex);
            return MakeUnexpected(Failure{});
        }

        auto jointResult = ParseSkeletonJoint(nwbFilePath, jointValue);
        if(!jointResult)
            return MakeUnexpected(Failure{});
        entry.joints.push_back(*jointResult);
    }

    if(!BuildSkeletonAsset(entry, arena, scratchArena))
        return MakeUnexpected(Failure{});
    return entry;
}

Expected<SkeletonCookEntry> ParseSkeletonCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    return Core::Assets::ParseCookMetadataDocument<SkeletonCookEntry, &ParseSkeletonCookMetadata>(assetRoot, virtualRoot, nwbFilePath, doc, s_SkeletonMetaDiagnosticPrefix, arena, scratchArena);
}

Expected<Skeleton> BuildSkeletonAsset(const SkeletonCookEntry& skeletonEntry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    Skeleton::JointVector joints(arena);
    Skeleton::JointIndexMap jointIndices(0, Hasher<Name>(), EqualTo<Name>(), arena);
    if(!BuildSkeletonCookJoints(skeletonEntry.virtualPath, skeletonEntry.joints, joints, jointIndices))
        return MakeUnexpected(Failure{});

    Skeleton asset(arena, skeletonEntry.virtualPath);
    asset.setJoints(Move(joints), Move(jointIndices));
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

