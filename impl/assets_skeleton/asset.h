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
    Name name = NAME_NONE;
    u32 parentIndex = s_SkeletonInvalidJointIndex;
    SkeletonJointMatrix localBindPose = MakeIdentitySkeletonJointMatrix();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Skeleton final : public Core::Assets::TypedAsset<Skeleton>{
public:
    NWB_DEFINE_ASSET_TYPE("skeleton")


public:
    using JointVector = Core::Assets::AssetVector<SkeletonJoint>;


public:
    explicit Skeleton(Core::Assets::AssetArena& arena)
        : m_joints(arena)
    {}
    Skeleton(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Skeleton>(virtualPath)
        , m_joints(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setJoints(JointVector&& joints){ m_joints = Move(joints); }

public:
    [[nodiscard]] const JointVector& joints()const{ return m_joints; }
    [[nodiscard]] u32 jointCount()const{ return static_cast<u32>(m_joints.size()); }


private:
    JointVector m_joints;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkeletonAssetCodec final : public Core::Assets::AssetCodec<Skeleton>{
public:
    SkeletonAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
