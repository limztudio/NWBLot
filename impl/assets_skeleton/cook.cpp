// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"
#include "binary_payload.h"
#include "cook_matrix.h"
#include "payload_validation.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>

#include <global/binary.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkeletonAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, NWB_TEXT("SkeletonAssetCodec::serialize")))
        return false;

    const Skeleton& skeleton = *checked_cast<const Skeleton*>(&asset);
    Core::Alloc::ScratchArena scratchArena(AssetsSkeletonArenaScope::s_SerializeArena);
    if(!skeleton.validatePayload(scratchArena))
        return false;

    usize reserveBytes = sizeof(SkeletonBinaryPayload::HeaderBinary);
    if(!AddBinaryRepeatedReserveBytes(reserveBytes, skeleton.joints().size(), sizeof(SkeletonBinaryPayload::JointBinary)) || !BinaryDetail::CanStoreValueCount(outBinary, reserveBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkeletonAssetCodec::serialize failed: cooked payload size overflows"));
        return false;
    }

    Vector<NameHash, Core::Alloc::ScratchArena> jointNameHashes(scratchArena);
    jointNameHashes.resize(skeleton.joints().size());
    for(const auto& jointLookup : skeleton.jointIndices())
        jointNameHashes[jointLookup.second] = jointLookup.first.hash();

    outBinary.clear();
    outBinary.reserve(reserveBytes);

    SkeletonBinaryPayload::HeaderBinary header;
    header.jointCount = static_cast<u64>(skeleton.joints().size());
    AppendPOD(outBinary, header);
    for(usize jointIndex = 0u; jointIndex < skeleton.joints().size(); ++jointIndex){
        const SkeletonJoint& joint = skeleton.joints()[jointIndex];
        SkeletonBinaryPayload::JointBinary jointBinary = {};
        jointBinary.nameHash = jointNameHashes[jointIndex];
        jointBinary.parentIndex = joint.parentIndex;
        jointBinary.localBindPose = joint.localBindPose;
        AppendPOD(outBinary, jointBinary);
    }
    return true;
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
static constexpr AStringView s_SkeletonJointMetaKind = "Skeleton joint meta";
static constexpr AStringView s_SkeletonMetaDiagnosticPrefix = "Skeleton meta";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateSkeletonAssetFields(const Path& nwbFilePath, const Value& asset){
    return Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SkeletonMetaDiagnosticPrefix, { s_JointsField });
}

[[nodiscard]] bool ValidateSkeletonJointFields(const Path& nwbFilePath, const Value& joint){
    return Core::Assets::ValidateMetadataAssetFields(nwbFilePath, joint, "Skeleton joint", { s_NameField, s_ParentField, s_LocalBindPoseField });
}

[[nodiscard]] Expected<SkeletonCookJoint> ParseSkeletonJoint(const Path& nwbFilePath, const Value& jointValue){
    SkeletonCookJoint joint{};


    if(!ValidateSkeletonJointFields(nwbFilePath, jointValue))
        return MakeUnexpected(Failure{});
    auto nameResult = Core::Assets::ReadMetadataNameField(nwbFilePath, jointValue, s_SkeletonJointMetaKind, s_NameField, true);
    if(!nameResult)
        return MakeUnexpected(Failure{});
    joint.name = *nameResult;
    auto parentResult = Core::Assets::ReadMetadataNameField(nwbFilePath, jointValue, s_SkeletonJointMetaKind, s_ParentField, false);
    if(!parentResult)
        return MakeUnexpected(Failure{});
    joint.parent = *parentResult;

    const Value* localBindPose = FindField(jointValue, s_LocalBindPoseField);
    if(!localBindPose)
        return joint;

    auto matrixResult = AssetsSkeletonCookDetail::ParseSkeletonJointMatrixValue(nwbFilePath, *localBindPose, s_SkeletonMetaKind, s_LocalBindPoseField);
    if(!matrixResult)
        return MakeUnexpected(Failure{});
    joint.localBindPose = *matrixResult;
    return joint;
}

template<typename ArenaT>
[[nodiscard]] Expected<u32> ResolveParentIndex(const SkeletonCookEntry& skeletonEntry, const HashMap<Name, u32, ArenaT, Hasher<Name>, EqualTo<Name>>& earlierJointIndices, const usize jointIndex, const Name& parent){
    if(!parent)
        return s_SkeletonInvalidJointIndex;

    const auto foundParent = earlierJointIndices.find(parent);
    if(foundParent != earlierJointIndices.end()){
        return foundParent.value();
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': joint '{}' references missing or later parent '{}'")
        , StringConvert(skeletonEntry.virtualPath.resolvedText())
        , StringConvert(skeletonEntry.joints[jointIndex].name.resolvedText())
        , StringConvert(parent.resolvedText())
    );
    return MakeUnexpected(Failure{});
}

template<typename ArenaT>
struct SkeletonJointPayload final{
    Vector<SkeletonJoint, ArenaT> joints;
    HashMap<Name, u32, ArenaT, Hasher<Name>, EqualTo<Name>> jointIndices;


public:
    explicit SkeletonJointPayload(ArenaT& arena)
        : joints(arena)
        , jointIndices(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
};

template<typename ArenaT>
[[nodiscard]] Expected<SkeletonJointPayload<ArenaT>> BuildSkeletonJointPayload(const SkeletonCookEntry& skeletonEntry, ArenaT& arena){
    SkeletonJointPayload<ArenaT> payload(arena);
    payload.joints.reserve(skeletonEntry.joints.size());
    payload.jointIndices.reserve(skeletonEntry.joints.size());

    for(usize jointIndex = 0u; jointIndex < skeletonEntry.joints.size(); ++jointIndex){
        const SkeletonCookJoint& cookJoint = skeletonEntry.joints[jointIndex];

        SkeletonJoint joint;
        joint.localBindPose = cookJoint.localBindPose;
        auto parentIndexResult = ResolveParentIndex<ArenaT>(skeletonEntry, payload.jointIndices, jointIndex, cookJoint.parent);
        if(!parentIndexResult)
            return MakeUnexpected(Failure{});
        joint.parentIndex = *parentIndexResult;
        if(!payload.jointIndices.emplace(cookJoint.name, static_cast<u32>(payload.joints.size())).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton meta '{}': duplicate joint name '{}'"), StringConvert(skeletonEntry.virtualPath.resolvedText()), StringConvert(cookJoint.name.resolvedText()));
            return MakeUnexpected(Failure{});
        }

        payload.joints.push_back(joint);
    }

    return payload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SkeletonCookEntry> ParseSkeletonCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_skeleton_cook;

    SkeletonCookEntry entry(arena);

    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SkeletonMetaDiagnosticPrefix))
        return MakeUnexpected(Failure{});

    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_SkeletonMetaDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!ValidateSkeletonAssetFields(nwbFilePath, asset))
        return MakeUnexpected(Failure{});

    const auto joints = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_SkeletonMetaDiagnosticPrefix, s_JointsField);
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

    auto payloadResult = BuildSkeletonJointPayload(entry, scratchArena);
    if(!payloadResult)
        return MakeUnexpected(Failure{});
    if(!AssetsSkeletonPayloadDetail::ValidateJoints<Core::Alloc::ScratchArena>(Span<const SkeletonJoint>(payloadResult->joints.data(), payloadResult->joints.size()), payloadResult->jointIndices, scratchArena))
        return MakeUnexpected(Failure{});
    return entry;
}

Expected<SkeletonCookEntry> ParseSkeletonCookMetadata(const Path& assetRoot, const AStringView virtualRoot, const Path& nwbFilePath, const Core::Metascript::Document& doc, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    Name virtualPath = s_NameNone;
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    virtualPath = *virtualPathResult;
    return ParseSkeletonCookMetadata(virtualPath, nwbFilePath, doc.asset(), arena, scratchArena);
}

Expected<Skeleton> BuildSkeletonAsset(const SkeletonCookEntry& skeletonEntry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    Skeleton asset(arena, skeletonEntry.virtualPath);

    auto payloadResult = __hidden_skeleton_cook::BuildSkeletonJointPayload(skeletonEntry, arena);
    if(!payloadResult)
        return MakeUnexpected(Failure{});

    asset.setJoints(Move(payloadResult->joints), Move(payloadResult->jointIndices));
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

