// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_skeleton/joint_types.h>

#include <core/assets/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SkeletonInvalidJointIndex = Limit<u32>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkeletonJoint{
    u32 parentIndex = s_SkeletonInvalidJointIndex;
    SkeletonJointMatrix localBindPose = ::Float34Identity();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Skeleton final : public Core::Assets::TypedAsset<Skeleton>{
public:
    NWB_DEFINE_ASSET_TYPE("skeleton")


public:
    using JointVector = Core::Assets::AssetVector<SkeletonJoint>;
    using JointIndexMap = HashMap<Name, u32, Core::Assets::AssetArena, Hasher<Name>, EqualTo<Name>>;


public:
    explicit Skeleton(Core::Assets::AssetArena& arena)
        : m_joints(arena)
        , m_jointIndices(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
    Skeleton(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Skeleton>(virtualPath)
        , m_joints(arena)
        , m_jointIndices(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setJoints(JointVector&& joints, JointIndexMap&& jointIndices);

public:
    [[nodiscard]] const JointVector& joints()const noexcept{ return m_joints; }
    [[nodiscard]] const JointIndexMap& jointIndices()const noexcept{ return m_jointIndices; }
    [[nodiscard]] u32 jointCount()const noexcept{ return static_cast<u32>(m_joints.size()); }
    [[nodiscard]] u32 findJointIndex(Name jointName)const;


private:
    JointVector m_joints;
    JointIndexMap m_jointIndices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(SkeletonAssetCodec, Skeleton);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

