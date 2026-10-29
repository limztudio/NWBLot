// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_skeleton/joint_types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/assets/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(IsStandardLayout_V<SkeletonJointMatrix>, "SkeletonJointMatrix must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<SkeletonJointMatrix>, "SkeletonJointMatrix must stay GPU-uploadable");
static_assert(sizeof(SkeletonJointMatrix) == sizeof(f32) * s_SkeletonJointMatrixFloatCount, "SkeletonJointMatrix GPU layout drifted");
static_assert(alignof(SkeletonJointMatrix) >= alignof(Float4), "SkeletonJointMatrix must stay SIMD-aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Skeleton final : public Core::Assets::TypedAsset<Skeleton>{
public:
    using JointVector = Core::Assets::AssetVector<SkeletonJoint>;
    using JointIndexMap = HashMap<Name, u32, Core::Assets::AssetArena, Hasher<Name>, EqualTo<Name>>;


public:
    NWB_DEFINE_ASSET_TYPE("skeleton")


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
    void setJoints(JointVector&& joints, JointIndexMap&& jointIndices)noexcept;

public:
    [[nodiscard]] const JointVector& joints()const noexcept{ return m_joints; }

    [[nodiscard]] const JointIndexMap& jointIndices()const noexcept{ return m_jointIndices; }

    [[nodiscard]] u32 jointCount()const noexcept{ return static_cast<u32>(m_joints.size()); }
    [[nodiscard]] u32 findJointIndex(Name jointName)const noexcept;

public:
    [[nodiscard]] b8 validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    b8 loadBinary(Span<const u8> binary);


private:
    JointVector m_joints;
    JointIndexMap m_jointIndices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(SkeletonAssetCodec, Skeleton);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

