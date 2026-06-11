// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"
#include "cook_matrix.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkeletonAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkeletonAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Skeleton::s_AssetTypeText)
        );
        return false;
    }

    const Skeleton& skeleton = static_cast<const Skeleton&>(asset);
    if(!skeleton.validatePayload())
        return false;

    Core::Assets::AssetVector<NameHash> jointNameHashes(outBinary.get_allocator().arena());
    jointNameHashes.resize(skeleton.joints().size());
    for(const auto& jointLookup : skeleton.jointIndices())
        jointNameHashes[jointLookup.second] = jointLookup.first.hash();

    Core::Assets::AssetVector<SkeletonBinaryPayload::JointBinary> jointBinaries(outBinary.get_allocator().arena());
    jointBinaries.reserve(skeleton.joints().size());
    for(usize jointIndex = 0u; jointIndex < skeleton.joints().size(); ++jointIndex){
        const SkeletonJoint& joint = skeleton.joints()[jointIndex];
        SkeletonBinaryPayload::JointBinary jointBinary;
        jointBinary.nameHash = jointNameHashes[jointIndex];
        jointBinary.parentIndex = joint.parentIndex;
        jointBinary.localBindPose = joint.localBindPose;
        jointBinaries.push_back(jointBinary);
    }

    outBinary.clear();
    outBinary.reserve(sizeof(SkeletonBinaryPayload::HeaderBinary) + jointBinaries.size() * sizeof(SkeletonBinaryPayload::JointBinary));

    SkeletonBinaryPayload::HeaderBinary header;
    header.jointCount = static_cast<u64>(jointBinaries.size());
    AppendPOD(outBinary, header);
    return Core::Assets::AppendVectorPayload(
        outBinary,
        jointBinaries,
        NWB_TEXT("SkeletonAssetCodec::serialize"),
        NWB_TEXT("joints")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_JointsField = "joints";
static constexpr AStringView s_NameField = "name";
static constexpr AStringView s_ParentField = "parent";
static constexpr AStringView s_LocalBindPoseField = "local_bind_pose";
static constexpr AStringView s_SkeletonMetaKind = "Skeleton";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const Value* FindField(const Value& map, const AStringView fieldName){
    return map.findField(MStringView(fieldName.data(), fieldName.size()));
}

[[nodiscard]] bool ReadNameField(
    const Path& nwbFilePath,
    const Value& object,
    const AStringView objectKind,
    const AStringView fieldName,
    const bool required,
    Name& outName
){
    outName = NAME_NONE;

    const Value* fieldValue = FindField(object, fieldName);
    if(!fieldValue){
        if(!required)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': field '{}' is required")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': field '{}' must be a string")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const MStringView text = fieldValue->asString();
    outName = Name(AStringView(text.data(), text.size()));
    if(required && !outName){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': field '{}' must not be empty")
            , StringConvert(objectKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateSkeletonAssetFields(const Path& nwbFilePath, const Value& asset){
    return Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "Skeleton meta",
        { s_JointsField }
    );
}

[[nodiscard]] bool ValidateSkeletonJointFields(const Path& nwbFilePath, const Value& joint){
    return Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        joint,
        "Skeleton joint",
        { s_NameField, s_ParentField, s_LocalBindPoseField }
    );
}

[[nodiscard]] bool ParseSkeletonJoint(const Path& nwbFilePath, const Value& jointValue, SkeletonCookJoint& outJoint){
    outJoint = {};

    if(!ValidateSkeletonJointFields(nwbFilePath, jointValue))
        return false;
    if(
        !ReadNameField(nwbFilePath, jointValue, "Skeleton joint", s_NameField, true, outJoint.name)
        || !ReadNameField(nwbFilePath, jointValue, "Skeleton joint", s_ParentField, false, outJoint.parent)
    )
        return false;

    const Value* localBindPose = FindField(jointValue, s_LocalBindPoseField);
    if(!localBindPose)
        return true;

    return AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(
        nwbFilePath,
        *localBindPose,
        s_SkeletonMetaKind,
        s_LocalBindPoseField,
        outJoint.localBindPose
    );
}

[[nodiscard]] bool ResolveParentIndex(
    const SkeletonCookEntry& skeletonEntry,
    const usize jointIndex,
    const Name& parent,
    u32& outParentIndex
){
    outParentIndex = s_SkeletonInvalidJointIndex;
    if(!parent)
        return true;

    for(usize candidateIndex = 0u; candidateIndex < jointIndex; ++candidateIndex){
        if(skeletonEntry.joints[candidateIndex].name != parent)
            continue;

        outParentIndex = static_cast<u32>(candidateIndex);
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': joint '{}' references missing or later parent '{}'")
        , StringConvert(skeletonEntry.virtualPath.c_str())
        , StringConvert(skeletonEntry.joints[jointIndex].name.c_str())
        , StringConvert(parent.c_str())
    );
    return false;
}

[[nodiscard]] bool BuildSkeletonJointPayload(
    const SkeletonCookEntry& skeletonEntry,
    Skeleton::JointVector& outJoints,
    Skeleton::JointIndexMap& outJointIndices
){
    outJoints.clear();
    outJointIndices.clear();
    outJoints.reserve(skeletonEntry.joints.size());

    for(usize jointIndex = 0u; jointIndex < skeletonEntry.joints.size(); ++jointIndex){
        const SkeletonCookJoint& cookJoint = skeletonEntry.joints[jointIndex];

        SkeletonJoint joint;
        joint.localBindPose = cookJoint.localBindPose;
        if(!ResolveParentIndex(skeletonEntry, jointIndex, cookJoint.parent, joint.parentIndex)){
            outJoints.clear();
            outJointIndices.clear();
            return false;
        }
        if(!outJointIndices.emplace(cookJoint.name, static_cast<u32>(outJoints.size())).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': duplicate joint name '{}'")
                , StringConvert(skeletonEntry.virtualPath.c_str())
                , StringConvert(cookJoint.name.c_str())
            );
            outJoints.clear();
            outJointIndices.clear();
            return false;
        }

        outJoints.push_back(joint);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseSkeletonCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    SkeletonCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_skeleton_cook;
    (void)scratchArena;

    outEntry = SkeletonCookEntry(outEntry.joints.get_allocator().arena());

    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': asset is not a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    outEntry.virtualPath = virtualPath;
    if(!outEntry.virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': virtual path must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!ValidateSkeletonAssetFields(nwbFilePath, asset))
        return false;

    const Value* joints = FindField(asset, s_JointsField);
    if(!joints || !joints->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': field '{}' must be a list")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_JointsField)
        );
        return false;
    }

    const auto& jointList = joints->asList();
    outEntry.joints.reserve(jointList.size());
    for(usize jointIndex = 0u; jointIndex < jointList.size(); ++jointIndex){
        const Value& jointValue = jointList[jointIndex];
        if(!jointValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': joints[{}] must be a map")
                , PathToString<tchar>(nwbFilePath)
                , jointIndex
            );
            return false;
        }

        SkeletonCookJoint joint;
        if(!ParseSkeletonJoint(nwbFilePath, jointValue, joint))
            return false;
        outEntry.joints.push_back(joint);
    }

    Skeleton testSkeleton(outEntry.joints.get_allocator().arena(), outEntry.virtualPath);
    Skeleton::JointVector testJoints(outEntry.joints.get_allocator().arena());
    Skeleton::JointIndexMap testJointIndices(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        outEntry.joints.get_allocator().arena()
    );
    if(!BuildSkeletonJointPayload(outEntry, testJoints, testJointIndices))
        return false;
    testSkeleton.setJoints(Move(testJoints), Move(testJointIndices));
    return testSkeleton.validatePayload();
}

bool ParseSkeletonCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SkeletonCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    Name virtualPath = NAME_NONE;
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, virtualPath, scratchArena))
        return false;
    return ParseSkeletonCookMetadata(virtualPath, nwbFilePath, doc.asset(), outEntry, scratchArena);
}

bool BuildSkeletonAsset(const SkeletonCookEntry& skeletonEntry, Skeleton& outSkeleton){
    outSkeleton = Skeleton(skeletonEntry.joints.get_allocator().arena(), skeletonEntry.virtualPath);

    Skeleton::JointVector joints(skeletonEntry.joints.get_allocator().arena());
    Skeleton::JointIndexMap jointIndices(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        skeletonEntry.joints.get_allocator().arena()
    );
    if(!__hidden_skeleton_cook::BuildSkeletonJointPayload(skeletonEntry, joints, jointIndices))
        return false;

    outSkeleton.setJoints(Move(joints), Move(jointIndices));
    return outSkeleton.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

