// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_csg/components.adhd.h>
#include <impl/ecs_csg/shape_registry_validation.adhd.h>
#include <impl/assets_csg/shape_id.adhd.h>

#include <global/span.h>


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

using CsgShapeBoundsCallback = Expected<CsgShapeBounds>(*)(const SIMDMatrix& shapeToWorld, Span<const u8> parameterBytes);
using CsgShapeSignedDistanceCallback = Expected<f32>(*)(SIMDVectorSingle shapePosition, const CsgShapeInlineParameters& parameters);

[[nodiscard]] Expected<CsgShapeBounds> BuildCsgShapeWorldBoundsFromLocal(const SIMDMatrix& shapeToWorld, const CsgShapeLocalBounds& localBounds)noexcept;

template<typename ShapeT>
[[nodiscard]] Expected<CsgShapeBounds> EvaluateCsgShapeBoundsCallback(const SIMDMatrix& shapeToWorld, const Span<const u8> parameterBytes)noexcept{
    static_assert(IsBaseOf_V<ICsgShape, ShapeT>, "ShapeT must implement ICsgShape");

    const ShapeT shape{};
    const Expected<CsgShapeLocalBounds> localBounds = ComputeCsgShapeLocalBoundsFromBytes(shape, parameterBytes);
    if(!localBounds)
        return MakeUnexpected(Failure{});
    return BuildCsgShapeWorldBoundsFromLocal(shapeToWorld, *localBounds);
}

template<typename ShapeT>
[[nodiscard]] Expected<f32> EvaluateCsgShapeSignedDistanceCallback(const SIMDVectorSingle shapePosition, const CsgShapeInlineParameters& parameters)noexcept{
    static_assert(IsBaseOf_V<ICsgShape, ShapeT>, "ShapeT must implement ICsgShape");

    const ShapeT shape{};
    return shape.evaluateSignedDistance(shapePosition, parameters);
}

struct CsgShapeTypeDesc{
    using DefaultParameterByteVector = FixedVector<u8, s_CsgShapeInlineParameterMaxBytes>;


public:
    Name name = s_NameNone;
    Name shaderModule = s_NameNone;
    AString<Core::Alloc::GlobalArena> shaderModuleInclude;

    u32 parameterByteSize = 0u;
    DefaultParameterByteVector defaultParameterBytes;
    CsgShapeBoundsCallback boundsCallback = nullptr;
    CsgShapeSignedDistanceCallback signedDistanceCallback = nullptr;


public:
    explicit CsgShapeTypeDesc(Core::Alloc::GlobalArena& arena)noexcept
        : shaderModuleInclude(arena)
    {}
};

template<typename ShapeT>
[[nodiscard]] Expected<CsgShapeTypeDesc> MakeCsgShapeTypeDesc(Core::Alloc::GlobalArena& arena, const Name& shaderModule = s_NameNone, const AStringView shaderModuleInclude = {}){
    static_assert(IsBaseOf_V<ICsgShape, ShapeT>, "ShapeT must implement ICsgShape");

    const ShapeT shape{};
    CsgShapeTypeDesc desc(arena);
    desc.name = shape.shapeTypeName();
    desc.shaderModule = shaderModule;
    desc.shaderModuleInclude = shaderModuleInclude;
    desc.parameterByteSize = shape.parameterByteSize();
    const CsgShapeInlineParameters defaults = shape.defaultParameters();
    const auto packedDefaults = PackCsgShapeDefaultParameterBytes(desc.parameterByteSize, defaults);
    if(!packedDefaults){
        LogCsgShapeInvalidParameterSize(desc.name);
        return MakeUnexpected(Failure{});
    }
    desc.defaultParameterBytes = *packedDefaults;
    desc.boundsCallback = &EvaluateCsgShapeBoundsCallback<ShapeT>;
    desc.signedDistanceCallback = &EvaluateCsgShapeSignedDistanceCallback<ShapeT>;
    return desc;
}

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
    explicit CsgShapeRegistry(Core::Alloc::GlobalArena& arena)
        : m_shapeTypes(arena)
        , m_shapeTypeIds(0, Hasher<Name>(), EqualTo<Name>(), arena)
        , m_shapeTypeIndices(0, Hasher<CsgShapeTypeId>(), EqualTo<CsgShapeTypeId>(), arena)
    {}


public:
    [[nodiscard]] Expected<CsgShapeTypeId> registerShapeType(const CsgShapeTypeDesc& desc, b8 replaceExisting = false);

    template<typename ShapeT>
    [[nodiscard]] Expected<CsgShapeTypeId> registerShape(const b8 replaceExisting = false, const Name& shaderModule = s_NameNone, const AStringView shaderModuleInclude = {}){
        auto desc = MakeCsgShapeTypeDesc<ShapeT>(m_shapeTypes.get_allocator().arena(), shaderModule, shaderModuleInclude);
        if(!desc)
            return MakeUnexpected(Failure{});
        return registerShapeType(*desc, replaceExisting);
    }


public:
    [[nodiscard]] CsgShapeTypeId findShapeTypeId(const Name& name)const noexcept;
    [[nodiscard]] Expected<CsgShapeTypeInfo> findShapeType(const Name& name)const;
    [[nodiscard]] Expected<CsgShapeTypeInfo> findShapeType(CsgShapeTypeId typeId)const;
    [[nodiscard]] usize shapeTypeCount()const noexcept;
    [[nodiscard]] u64 revision()const noexcept;
    [[nodiscard]] Expected<AString<Core::Alloc::GlobalArena>> findShaderModuleInclude(const Name& shaderModule, Core::Alloc::GlobalArena& resultArena)const;


public:
    [[nodiscard]] Expected<CsgShapeBounds> buildShapeBounds(const Name& name, const SIMDMatrix& shapeToWorld, const Span<const u8> parameterBytes)const{
        return buildShapeBounds(findShapeTypeId(name), shapeToWorld, parameterBytes);
    }
    [[nodiscard]] Expected<CsgShapeBounds> buildShapeBounds(CsgShapeTypeId typeId, const SIMDMatrix& shapeToWorld, Span<const u8> parameterBytes)const;
    [[nodiscard]] Expected<f32> evaluateSignedDistance(CsgShapeTypeId typeId, SIMDVectorSingle shapePosition, Span<const u8> parameterBytes)const;


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

