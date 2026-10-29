// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_csg/components.adhd.h>
#include <impl/ecs_csg/shape_types.adhd.h>

#include <impl/assets_csg/shape_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Call-scoped scratch; retained scene/GPU values use Float storage.
struct alignas(SIMDVectorSingle) CsgShapeBounds{
    SIMDVectorSingle minBounds = VectorZero();
    SIMDVectorSingle maxBounds = VectorZero();
    b8 finiteBounds = false;
};
static_assert(alignof(Expected<CsgShapeBounds>) >= alignof(CsgShapeBounds));

using CsgShapeBoundsCallback = Expected<CsgShapeBounds>(*)(const SIMDMatrix& shapeToWorld, const u8* parameterBytes, usize parameterByteSize);

struct CsgShapeTypeDesc{
    using DefaultParameterByteVector = FixedVector<u8, s_CsgShapeInlineParameterMaxBytes>;


public:
    Name name = s_NameNone;
    Name shaderModule = s_NameNone;
    AString<Core::Alloc::GlobalArena> shaderModuleInclude;

    u32 parameterByteSize = 0u;
    DefaultParameterByteVector defaultParameterBytes;
    CsgShapeBoundsCallback boundsCallback = nullptr;


public:
    explicit CsgShapeTypeDesc(Core::Alloc::GlobalArena& arena)noexcept
        : shaderModuleInclude(arena)
    {}
};

struct CsgShapeTypeInfo{
    CsgShapeTypeId id = s_InvalidCsgShapeTypeId;
    CsgShapeTypeDesc desc;


public:
    explicit CsgShapeTypeInfo(Core::Alloc::GlobalArena& arena)noexcept
        : desc(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    friend b8 RegisterBuiltInCsgShapeTypes(CsgShapeRegistry& registry);


private:
    using ShapeVector = Vector<CsgShapeTypeInfo, Core::Alloc::GlobalArena>;
    using ShapeIdMap = HashMap<Name, CsgShapeTypeId, Core::Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;
    using ShapeIndexMap = HashMap<CsgShapeTypeId, usize, Core::Alloc::GlobalArena, Hasher<CsgShapeTypeId>, EqualTo<CsgShapeTypeId>>;


public:
    explicit CsgShapeRegistry(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] Expected<CsgShapeTypeId> registerShapeType(const CsgShapeTypeDesc& desc, b8 replaceExisting = false);


public:
    [[nodiscard]] CsgShapeTypeId findShapeTypeId(const Name& name)const noexcept;
    [[nodiscard]] Expected<CsgShapeTypeInfo> findShapeType(const Name& name)const;
    [[nodiscard]] Expected<CsgShapeTypeInfo> findShapeType(CsgShapeTypeId typeId)const;
    [[nodiscard]] usize shapeTypeCount()const noexcept;
    [[nodiscard]] u64 revision()const noexcept;
    [[nodiscard]] Expected<AString<Core::Alloc::GlobalArena>> findShaderModuleInclude(const Name& shaderModule, Core::Alloc::GlobalArena& resultArena)const;


public:
    [[nodiscard]] Expected<CsgShapeBounds> buildShapeBounds(const Name& name, const SIMDMatrix& shapeToWorld, const u8* parameterBytes, usize parameterByteSize)const;
    [[nodiscard]] Expected<CsgShapeBounds> buildShapeBounds(CsgShapeTypeId typeId, const SIMDMatrix& shapeToWorld, const u8* parameterBytes, usize parameterByteSize)const;


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


b8 RegisterBuiltInCsgShapeTypes(CsgShapeRegistry& registry);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

