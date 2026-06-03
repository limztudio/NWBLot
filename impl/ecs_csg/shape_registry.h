// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CsgShapeTypeId = u32;

inline constexpr CsgShapeTypeId s_InvalidCsgShapeTypeId = 0u;
inline constexpr Name s_CsgBuiltInShapeShaderModuleName("engine/csg/builtin_shapes");
inline constexpr Name s_CsgPlaneShapeName("engine/csg/plane");
inline constexpr Name s_CsgBoxShapeName("engine/csg/box");
inline constexpr Name s_CsgSphereShapeName("engine/csg/sphere");
inline constexpr Name s_CsgCapsuleShapeName("engine/csg/capsule");


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
    Name name = NAME_NONE;
    Name shaderModule = NAME_NONE;
    ACompactString shaderModuleInclude;

    u32 parameterByteSize = 0u;
    CsgShapeBoundsCallback boundsCallback = nullptr;

    bool supportsAnalyticGradient = false;
};

struct CsgShapeTypeInfo{
    CsgShapeTypeId id = s_InvalidCsgShapeTypeId;
    CsgShapeTypeDesc desc;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgPlaneShapeParameters{
    Float4 normalDistance = Float4(0.0f, 1.0f, 0.0f, 0.0f);
};

struct CsgBoxShapeParameters{
    Float4 halfExtents = Float4(1.0f, 1.0f, 1.0f, 0.0f);
};

struct CsgSphereShapeParameters{
    Float4 radius = Float4(1.0f, 0.0f, 0.0f, 0.0f);
};

struct CsgCapsuleShapeParameters{
    Float4 radiusHalfHeight = Float4(1.0f, 1.0f, 0.0f, 0.0f);
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


public:
    explicit CsgShapeRegistry(Core::Alloc::GlobalArena& arena);


public:
    bool registerShapeType(const CsgShapeTypeDesc& desc, CsgShapeTypeId& outTypeId, bool replaceExisting = false);


public:
    [[nodiscard]] CsgShapeTypeId findShapeTypeId(const Name& name)const;
    [[nodiscard]] bool findShapeType(const Name& name, CsgShapeTypeInfo& outShapeType)const;
    [[nodiscard]] bool findShapeType(CsgShapeTypeId typeId, CsgShapeTypeInfo& outShapeType)const;
    [[nodiscard]] usize shapeTypeCount()const;
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RegisterBuiltInCsgShapeTypes(CsgShapeRegistry& registry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

