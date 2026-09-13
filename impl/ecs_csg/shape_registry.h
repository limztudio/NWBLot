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


using CsgShapeBoundsCallback = bool(*)(
    const SIMDMatrix& shapeToWorld,
    const u8* parameterBytes,
    usize parameterByteSize,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds,
    bool& outFiniteBounds
);

struct CsgShapeTypeDesc{
    using DefaultParameterByteVector = FixedVector<u8, s_CsgShapeInlineParameterMaxBytes>;

    Name name = NAME_NONE;
    Name shaderModule = NAME_NONE;
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
    using ShapeIdMap = HashMap<Name, CsgShapeTypeId, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena>;
    using ShapeIndexMap = HashMap<CsgShapeTypeId, usize, Hasher<CsgShapeTypeId>, EqualTo<CsgShapeTypeId>, Core::Alloc::GlobalArena>;


public:
    explicit CsgShapeRegistry(Core::Alloc::GlobalArena& arena);


public:
    bool registerShapeType(const CsgShapeTypeDesc& desc, CsgShapeTypeId& outTypeId, bool replaceExisting = false);


public:
    [[nodiscard]] CsgShapeTypeId findShapeTypeId(const Name& name)const;
    [[nodiscard]] bool findShapeType(const Name& name, CsgShapeTypeInfo& outShapeType)const;
    [[nodiscard]] bool findShapeType(CsgShapeTypeId typeId, CsgShapeTypeInfo& outShapeType)const;
    [[nodiscard]] usize shapeTypeCount()const;
    [[nodiscard]] u64 revision()const;
    [[nodiscard]] bool findShaderModuleInclude(const Name& shaderModule, ACompactString& outShaderModuleInclude)const;


public:
    [[nodiscard]] bool buildShapeBounds(
        const Name& name,
        const SIMDMatrix& shapeToWorld,
        const u8* parameterBytes,
        usize parameterByteSize,
        SIMDVector& outMinBounds,
        SIMDVector& outMaxBounds,
        bool& outFiniteBounds
    )const;
    [[nodiscard]] bool buildShapeBounds(
        CsgShapeTypeId typeId,
        const SIMDMatrix& shapeToWorld,
        const u8* parameterBytes,
        usize parameterByteSize,
        SIMDVector& outMinBounds,
        SIMDVector& outMaxBounds,
        bool& outFiniteBounds
    )const;


private:
    [[nodiscard]] bool shapeTypeById(CsgShapeTypeId typeId, CsgShapeTypeInfo& outShapeType)const;


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

