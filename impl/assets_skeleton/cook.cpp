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

template<typename JointIndexMapT>
[[nodiscard]] Expected<u32> ResolveParentIndex(const SkeletonCookEntry& skeletonEntry, const JointIndexMapT& earlierJointIndices, const usize jointIndex, const Name& parent){
    if(!parent)
        return s_SkeletonInvalidJointIndex;

    const auto foundParent = earlierJointIndices.find(parent);
    if(foundParent != earlierJointIndices.end())
        return foundParent.value();

    NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': joint '{}' references missing or later parent '{}'")
        , ConvertString(skeletonEntry.virtualPath.resolvedText())
        , ConvertString(skeletonEntry.joints[jointIndex].name.resolvedText())
        , ConvertString(parent.resolvedText())
    );
    return MakeUnexpected(Failure{});
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

    usize reserveBytes = sizeof(SkeletonHeaderBinary);
    if(!AddBinaryRepeatedReserveBytes(reserveBytes, skeleton.joints().size(), sizeof(SkeletonJointBinary)) || !CanStoreValueCount(outBinary, reserveBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkeletonAssetCodec::serialize failed: cooked payload size overflows"));
        return false;
    }

    Vector<NameHash, Core::Alloc::ScratchArena> jointNameHashes(scratchArena);
    jointNameHashes.resize(skeleton.joints().size());
    for(const auto& jointLookup : skeleton.jointIndices())
        jointNameHashes[jointLookup.second] = jointLookup.first.hash();

    outBinary.clear();
    outBinary.reserve(reserveBytes);

    const SkeletonHeaderBinary header = MakeSkeletonHeaderBinary(static_cast<u64>(skeleton.joints().size()));
    AppendPOD(outBinary, header);
    for(usize jointIndex = 0u; jointIndex < skeleton.joints().size(); ++jointIndex){
        const SkeletonJointBinary jointBinary = PackSkeletonJointBinary(jointNameHashes[jointIndex], skeleton.joints()[jointIndex]);
        AppendPOD(outBinary, jointBinary);
    }
    return true;
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
    joints.reserve(skeletonEntry.joints.size());
    jointIndices.reserve(skeletonEntry.joints.size());

    for(usize jointIndex = 0u; jointIndex < skeletonEntry.joints.size(); ++jointIndex){
        const SkeletonCookJoint& cookJoint = skeletonEntry.joints[jointIndex];

        SkeletonJoint joint;
        joint.localBindPose = cookJoint.localBindPose;
        auto parentIndexResult = __hidden_skeleton_cook::ResolveParentIndex(skeletonEntry, jointIndices, jointIndex, cookJoint.parent);
        if(!parentIndexResult)
            return MakeUnexpected(Failure{});
        joint.parentIndex = *parentIndexResult;
        if(!jointIndices.emplace(cookJoint.name, static_cast<u32>(joints.size())).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': duplicate joint name '{}'"), ConvertString(skeletonEntry.virtualPath.resolvedText()), ConvertString(cookJoint.name.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        joints.push_back(joint);
    }

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

