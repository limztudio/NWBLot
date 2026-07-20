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


struct SkeletonJointChildRange{
    u32 firstChild = 0u;
    u32 childCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Skeleton final : public Core::Assets::TypedAsset<Skeleton>{
public:
    NWB_DEFINE_ASSET_TYPE("skeleton")


public:
    using JointVector = Core::Assets::AssetVector<SkeletonJoint>;
    using JointChildRangeVector = Core::Assets::AssetVector<SkeletonJointChildRange>;
    using JointChildIndexVector = Core::Assets::AssetVector<u32>;
    using JointIndexMap = HashMap<Name, u32, Hasher<Name>, EqualTo<Name>, Core::Assets::AssetArena>;


public:
    explicit Skeleton(Core::Assets::AssetArena& arena)
        : m_joints(arena)
        , m_childRanges(arena)
        , m_childIndices(arena)
        , m_jointIndices(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
    Skeleton(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Skeleton>(virtualPath)
        , m_joints(arena)
        , m_childRanges(arena)
        , m_childIndices(arena)
        , m_jointIndices(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setJoints(JointVector&& joints, JointIndexMap&& jointIndices);

public:
    [[nodiscard]] const JointVector& joints()const{ return m_joints; }
    [[nodiscard]] const JointChildRangeVector& jointChildRanges()const{ return m_childRanges; }
    [[nodiscard]] const JointChildIndexVector& jointChildIndices()const{ return m_childIndices; }
    [[nodiscard]] const JointIndexMap& jointIndices()const{ return m_jointIndices; }
    [[nodiscard]] u32 jointCount()const{ return static_cast<u32>(m_joints.size()); }
    [[nodiscard]] u32 rootJointCount()const;
    [[nodiscard]] u32 findJointIndex(Name jointName)const;

private:
    void rebuildHierarchy();


private:
    JointVector m_joints;
    JointChildRangeVector m_childRanges;
    JointChildIndexVector m_childIndices;
    JointIndexMap m_jointIndices;
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

