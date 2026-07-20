#include "asset.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skeleton_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Assets::AssetCodecAutoRegistrar s_SkeletonAssetCodecAutoRegistrar(&Core::Assets::CreateAssetCodec<SkeletonAssetCodec>);


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Skeleton::setJoints(JointVector&& joints, JointIndexMap&& jointIndices){
    m_joints = Move(joints);
    m_jointIndices = Move(jointIndices);
    rebuildHierarchy();
}

void Skeleton::rebuildHierarchy(){
    m_childRanges.clear();
    m_childIndices.clear();

    m_childRanges.resize(m_joints.size());
    if(m_joints.empty())
        return;

    u32 childCount = 0u;
    for(const SkeletonJoint& joint : m_joints){
        if(joint.parentIndex == s_SkeletonInvalidJointIndex || joint.parentIndex >= m_childRanges.size())
            continue;

        ++m_childRanges[joint.parentIndex].childCount;
        ++childCount;
    }

    m_childIndices.resize(childCount);

    u32 firstChild = 0u;
    for(SkeletonJointChildRange& range : m_childRanges){
        const u32 rangeChildCount = range.childCount;
        range.firstChild = firstChild;
        range.childCount = 0u;
        firstChild += rangeChildCount;
    }

    for(u32 jointIndex = 0u; jointIndex < m_joints.size(); ++jointIndex){
        const u32 parentIndex = m_joints[jointIndex].parentIndex;
        if(parentIndex == s_SkeletonInvalidJointIndex || parentIndex >= m_childRanges.size())
            continue;

        SkeletonJointChildRange& parentRange = m_childRanges[parentIndex];
        m_childIndices[parentRange.firstChild + parentRange.childCount] = jointIndex;
        ++parentRange.childCount;
    }
}

u32 Skeleton::rootJointCount()const{
    u32 rootCount = 0u;
    for(const SkeletonJoint& joint : m_joints){
        if(joint.parentIndex == s_SkeletonInvalidJointIndex)
            ++rootCount;
    }
    return rootCount;
}

u32 Skeleton::findJointIndex(const Name jointName)const{
    const auto foundJoint = m_jointIndices.find(jointName);
    if(foundJoint == m_jointIndices.end())
        return s_SkeletonInvalidJointIndex;
    return foundJoint.value();
}

bool Skeleton::validatePayload()const{
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: virtual path is empty"));
        return false;
    }
    if(m_joints.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: skeleton has no joints"));
        return false;
    }

    if(m_jointIndices.size() != m_joints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup count does not match joint count"));
        return false;
    }

    Core::Assets::AssetVector<u8> namedJoints(m_joints.get_allocator().arena());
    namedJoints.resize(m_joints.size(), 0u);
    for(const auto& jointLookup : m_jointIndices){
        const Name jointName = jointLookup.first;
        const u32 jointIndex = jointLookup.second;
        if(!jointName){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup contains an empty name"));
            return false;
        }
        if(jointIndex >= m_joints.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint lookup '{}' has invalid index {}")
                , StringConvert(jointName.c_str())
                , jointIndex
            );
            return false;
        }
        if(namedJoints[jointIndex] != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has multiple names"), jointIndex);
            return false;
        }
        namedJoints[jointIndex] = 1u;
    }
    for(usize jointIndex = 0u; jointIndex < namedJoints.size(); ++jointIndex){
        if(namedJoints[jointIndex] != 0u)
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has no name"), jointIndex);
        return false;
    }

    for(usize i = 0u; i < m_joints.size(); ++i){
        const SkeletonJoint& joint = m_joints[i];
        if(joint.parentIndex != s_SkeletonInvalidJointIndex && joint.parentIndex >= i){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has invalid parent {}")
                , i
                , joint.parentIndex
            );
            return false;
        }
    }

    if(m_childRanges.size() != m_joints.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: hierarchy range count does not match joint count"));
        return false;
    }

    u32 expectedChildCount = 0u;
    for(const SkeletonJoint& joint : m_joints){
        if(joint.parentIndex != s_SkeletonInvalidJointIndex)
            ++expectedChildCount;
    }
    if(m_childIndices.size() != expectedChildCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: hierarchy child index count does not match parent links"));
        return false;
    }

    u32 referencedChildCount = 0u;
    for(u32 jointIndex = 0u; jointIndex < m_childRanges.size(); ++jointIndex){
        const SkeletonJointChildRange& range = m_childRanges[jointIndex];
        const u64 rangeEnd = static_cast<u64>(range.firstChild) + static_cast<u64>(range.childCount);
        if(rangeEnd > m_childIndices.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has invalid child range"), jointIndex);
            return false;
        }

        referencedChildCount += range.childCount;
        for(u32 childOffset = 0u; childOffset < range.childCount; ++childOffset){
            const u32 childIndex = m_childIndices[range.firstChild + childOffset];
            if(childIndex >= m_joints.size() || m_joints[childIndex].parentIndex != jointIndex){
                NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: joint {} has invalid child {}")
                    , jointIndex
                    , childIndex
                );
                return false;
            }
        }
    }
    if(referencedChildCount != expectedChildCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::validatePayload failed: hierarchy child ranges do not cover parent links"));
        return false;
    }

    return true;
}

bool Skeleton::loadBinary(const Core::Assets::AssetBytes& binary){
    m_joints.clear();
    m_childRanges.clear();
    m_childIndices.clear();
    m_jointIndices.clear();

    usize cursor = 0u;
    SkeletonBinaryPayload::HeaderBinary header;
    if(!Core::Assets::ReadMagicHeaderPayload(
        binary,
        cursor,
        header,
        SkeletonBinaryPayload::s_SkeletonMagic,
        NWB_TEXT("Skeleton::loadBinary"),
        NWB_TEXT("skeleton")
    ))
        return false;

    Core::Assets::AssetVector<SkeletonBinaryPayload::JointBinary> jointBinaries(m_joints.get_allocator().arena());
    if(!Core::Assets::ReadVectorPayload(
        binary,
        cursor,
        header.jointCount,
        jointBinaries,
        NWB_TEXT("Skeleton::loadBinary"),
        NWB_TEXT("joints")
    ))
        return false;

    m_joints.reserve(jointBinaries.size());
    for(const SkeletonBinaryPayload::JointBinary& jointBinary : jointBinaries){
        const Name jointName(jointBinary.nameHash);
        if(!m_jointIndices.emplace(jointName, static_cast<u32>(m_joints.size())).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Skeleton::loadBinary failed: duplicate joint '{}'"), StringConvert(jointName.c_str()));
            return false;
        }

        SkeletonJoint joint;
        joint.parentIndex = jointBinary.parentIndex;
        joint.localBindPose = jointBinary.localBindPose;
        m_joints.push_back(joint);
    }
    rebuildHierarchy();

    return Core::Assets::ReadCompletePayload(binary, cursor, NWB_TEXT("Skeleton::loadBinary"))
        && validatePayload()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

