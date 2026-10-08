// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <impl/assets/csg/shape_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_CsgShapeInlineParameterFloat4Count = 2u;
inline constexpr usize s_CsgShapeInlineParameterMaxBytes = sizeof(Float4) * s_CsgShapeInlineParameterFloat4Count;
inline constexpr f32 s_CsgShapeBoundsW = 0.0f;
inline constexpr u32 s_CsgCapsuleAxisSelectX = 0u;
inline constexpr u32 s_CsgCapsuleAxisSelectY = 1u;
inline constexpr u32 s_CsgCapsuleAxisSelectZ = 0u;
inline constexpr u32 s_CsgCapsuleAxisSelectW = 0u;
inline constexpr Float4 s_CsgPlaneDefaultNormalDistance = Float4(0.0f, 1.0f, 0.0f, 0.0f);
inline constexpr Float4 s_CsgBoxDefaultHalfExtents = Float4(1.0f, 1.0f, 1.0f, 0.0f);
inline constexpr Float4 s_CsgSphereDefaultRadius = Float4(1.0f, 0.0f, 0.0f, 0.0f);
inline constexpr Float4 s_CsgCapsuleDefaultRadiusHalfHeight = Float4(1.0f, 1.0f, 0.0f, 0.0f);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgShapeBounds{
    SIMDVector minBounds = VectorZero();
    SIMDVector maxBounds = VectorZero();
    bool finiteBounds = false;
};

using CsgShapeBoundsCallback = Expected<CsgShapeBounds>(*)(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    usize parameterByteSize
);

struct CsgShapeTypeDesc{
    using DefaultParameterByteVector = FixedVector<u8, s_CsgShapeInlineParameterMaxBytes>;

    Name name = s_NameNone;
    Name shaderModule = s_NameNone;
    ACompactString shaderModuleInclude;

    u32 parameterByteSize = 0u;
    DefaultParameterByteVector defaultParameterBytes;
    CsgShapeBoundsCallback boundsCallback = nullptr;
};

struct CsgShapeTypeInfo{
    CsgShapeTypeId id = s_InvalidCsgShapeTypeId;
    CsgShapeTypeDesc desc;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgPlaneShapeParameters{
    Float4 normalDistance = s_CsgPlaneDefaultNormalDistance;
};

struct CsgBoxShapeParameters{
    Float4 halfExtents = s_CsgBoxDefaultHalfExtents;
};

struct CsgSphereShapeParameters{
    Float4 radius = s_CsgSphereDefaultRadius;
};

struct CsgCapsuleShapeParameters{
    Float4 radiusHalfHeight = s_CsgCapsuleDefaultRadiusHalfHeight;
};

static_assert(IsStandardLayout_V<CsgPlaneShapeParameters>, "CsgPlaneShapeParameters must stay binary-stable");
static_assert(IsTriviallyCopyable_V<CsgPlaneShapeParameters>, "CsgPlaneShapeParameters must stay byte-copyable");
static_assert(IsStandardLayout_V<CsgBoxShapeParameters>, "CsgBoxShapeParameters must stay binary-stable");
static_assert(IsTriviallyCopyable_V<CsgBoxShapeParameters>, "CsgBoxShapeParameters must stay byte-copyable");
static_assert(IsStandardLayout_V<CsgSphereShapeParameters>, "CsgSphereShapeParameters must stay binary-stable");
static_assert(IsTriviallyCopyable_V<CsgSphereShapeParameters>, "CsgSphereShapeParameters must stay byte-copyable");
static_assert(IsStandardLayout_V<CsgCapsuleShapeParameters>, "CsgCapsuleShapeParameters must stay binary-stable");
static_assert(IsTriviallyCopyable_V<CsgCapsuleShapeParameters>, "CsgCapsuleShapeParameters must stay byte-copyable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgShapeRegistry final : NoCopy{
private:
    using ShapeVector = Vector<CsgShapeTypeInfo, Core::Alloc::GlobalArena>;
    using ShapeIdMap = HashMap<Name, CsgShapeTypeId, Core::Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;
    using ShapeIndexMap = HashMap<CsgShapeTypeId, usize, Core::Alloc::GlobalArena, Hasher<CsgShapeTypeId>, EqualTo<CsgShapeTypeId>>;


public:
    explicit CsgShapeRegistry(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] Expected<CsgShapeTypeId> registerShapeType(const CsgShapeTypeDesc& desc, bool replaceExisting = false);


public:
    [[nodiscard]] CsgShapeTypeId findShapeTypeId(const Name& name)const;
    [[nodiscard]] Expected<CsgShapeTypeInfo> findShapeType(const Name& name)const;
    [[nodiscard]] Expected<CsgShapeTypeInfo> findShapeType(CsgShapeTypeId typeId)const;
    [[nodiscard]] usize shapeTypeCount()const;
    [[nodiscard]] u64 revision()const;
    [[nodiscard]] Expected<ACompactString> findShaderModuleInclude(const Name& shaderModule)const;


public:
    [[nodiscard]] Expected<CsgShapeBounds> buildShapeBounds(
        const Name& name,
        const SIMDMatrix& shapeToWorld,
        const u8* parameterBytes,
        usize parameterByteSize
    )const;
    [[nodiscard]] Expected<CsgShapeBounds> buildShapeBounds(
        CsgShapeTypeId typeId,
        const SIMDMatrix& shapeToWorld,
        const u8* parameterBytes,
        usize parameterByteSize
    )const;


private:
    [[nodiscard]] Expected<CsgShapeTypeInfo> shapeTypeById(CsgShapeTypeId typeId)const;


private:
    mutable Futex m_mutex;
    ShapeVector m_shapeTypes;
    ShapeIdMap m_shapeTypeIds;
    ShapeIndexMap m_shapeTypeIndices;
    u64 m_revision = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RegisterBuiltInCsgShapeTypes(CsgShapeRegistry& registry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

