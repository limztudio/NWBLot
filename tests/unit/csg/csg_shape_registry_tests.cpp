// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/ecs_test_world.h>
#include <tests/unit/csg/name_cases.adhd.h>

#include <impl/ecs_csg/module.h>
#include <impl/ecs_csg/deform_edit.h>

#include <core/common/module.h>
#include <core/ecs/module.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_shape_registry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_EngineCsgBox = NWB::Tests::CsgCases::s_CsgEngineBoxPath;
static constexpr AStringView s_EngineCsgPlane = NWB::Tests::CsgCases::s_CsgEnginePlanePath;
using TestWorld = NWB::Tests::EcsTestWorld;
inline constexpr Name s_ScratchArena = NWB::Tests::CsgCases::s_OneOffNameTestsCsgScratch;


TEST(Csg, ShapeIdentityRetains64BitFoldForCookedAssetsAndRejectsAnEmptyName){
    using namespace NWB::Impl;
    const Expected<CsgShapeTypeId> invalid = TryResolveCsgShapeTypeIdFromName(Name{});
    const Expected<CsgShapeTypeId> plane = TryResolveCsgShapeTypeIdFromName(s_CsgPlaneShapeName);
    const Expected<CsgShapeTypeId> box = TryResolveCsgShapeTypeIdFromName(s_CsgBoxShapeName);
    const Expected<CsgShapeTypeId> sphere = TryResolveCsgShapeTypeIdFromName(s_CsgSphereShapeName);
    const Expected<CsgShapeTypeId> capsule = TryResolveCsgShapeTypeIdFromName(s_CsgCapsuleShapeName);
    EXPECT_FALSE(invalid.has_value());
    ASSERT_TRUE(plane.has_value());
    ASSERT_TRUE(box.has_value());
    ASSERT_TRUE(sphere.has_value());
    ASSERT_TRUE(capsule.has_value());
    ASSERT_TRUE(*plane);
    ASSERT_TRUE(*box);
    ASSERT_TRUE(*sphere);
    ASSERT_TRUE(*capsule);
    EXPECT_EQ(*plane, 0x92f0021au);
    EXPECT_EQ(*box, 0xb957a584u);
    EXPECT_EQ(*sphere, 0x56176cbfu);
    EXPECT_EQ(*capsule, 0xe0e38c7bu);
}

TEST(Csg, RepeatedBuiltInRegistrationPreservesTypeIdentityAndCount){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    const NWB::Impl::CsgShapeTypeId boxId = registry.findShapeTypeId(Name(s_EngineCsgBox));
    ASSERT_NE(boxId, NWB::Impl::s_InvalidCsgShapeTypeId);

    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    EXPECT_EQ(registry.shapeTypeCount(), 4u);
    EXPECT_EQ(registry.findShapeTypeId(Name(s_EngineCsgBox)), boxId);
}

TEST(Csg, ShapeBoundsRejectShortPayloadAndKeepPlaneUnbounded){
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    const SIMDMatrix shapeToWorldMatrix = MatrixTranslation(10.0f, -5.0f, 1.0f);
    NWB::Impl::CsgPlaneShapeParameters planeParameters;
    const auto planeBounds = registry.buildShapeBounds(Name(s_EngineCsgPlane), shapeToWorldMatrix, Span<const u8>(reinterpret_cast<const u8*>(&planeParameters), sizeof(planeParameters)));
    ASSERT_TRUE(planeBounds);
    EXPECT_FALSE(planeBounds->finiteBounds);

    NWB::Impl::CsgBoxShapeParameters boxParameters;
    EXPECT_FALSE(registry.buildShapeBounds(Name(s_EngineCsgBox), shapeToWorldMatrix, Span<const u8>(reinterpret_cast<const u8*>(&boxParameters), sizeof(boxParameters) - 1u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TestProjectSlabShape final : public NWB::Impl::ICsgShape{
public:
    virtual Name shapeTypeName()const noexcept override{
        return Name(NWB::Tests::CsgCases::s_CsgInputNameProjectCsgSlab);
    }

    virtual u32 parameterByteSize()const noexcept override{
        return NWB::Impl::s_CsgShapeParameterRegisterBytes;
    }

    virtual NWB::Impl::CsgShapeInlineParameters defaultParameters()const noexcept override{
        return NWB::Impl::MakeCsgShapeInlineParameters(VectorSet(0.5f, 1.0f, 1.0f, 0.0f), LoadFloat(NWB::Impl::s_CsgZeroShapeParameter));
    }

    virtual Expected<NWB::Impl::CsgShapeLocalBounds> computeLocalBounds(const NWB::Impl::CsgShapeInlineParameters& parameters)const noexcept override{
        return NWB::Impl::MakeCsgSymmetricLocalBounds(LoadFloat(parameters.parameter0));
    }

    virtual f32 evaluateSignedDistance(const SIMDVectorSingle shapePosition, const NWB::Impl::CsgShapeInlineParameters& parameters)const noexcept override{
        return VectorGetX(shapePosition) + parameters.parameter0.x;
    }
};

class OversizedProjectShape final : public NWB::Impl::ICsgShape{
public:
    virtual Name shapeTypeName()const noexcept override{ return Name(NWB::Tests::CsgCases::s_CsgInputNameProjectCsgOversized); }
    virtual u32 parameterByteSize()const noexcept override{ return static_cast<u32>(NWB::Impl::s_CsgShapeInlineParameterMaxBytes) + 1u; }
    virtual NWB::Impl::CsgShapeInlineParameters defaultParameters()const noexcept override{ return {}; }
    virtual Expected<NWB::Impl::CsgShapeLocalBounds> computeLocalBounds(const NWB::Impl::CsgShapeInlineParameters&)const noexcept override{ return MakeUnexpected(Failure{}); }
    virtual f32 evaluateSignedDistance(const SIMDVectorSingle, const NWB::Impl::CsgShapeInlineParameters&)const noexcept override{ return 0.0f; }
};

TEST(Csg, OversizedProjectShapeDoesNotChangeRegistry){
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestWorld testWorld;
    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    const usize count = registry.shapeTypeCount();
    const u64 revision = registry.revision();
    EXPECT_FALSE(registry.registerShape<OversizedProjectShape>());
    EXPECT_EQ(registry.shapeTypeCount(), count);
    EXPECT_EQ(registry.revision(), revision);
    EXPECT_FALSE(registry.findShapeType(Name(NWB::Tests::CsgCases::s_CsgLookupNameProjectCsgOversized)));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("parameter size larger than the inline shader ABI")));
}

TEST(Csg, ProjectDefinedICsgShapeRegistersBoundsSignedDistanceAndDeformCuts){
    TestWorld testWorld;
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Core::Alloc::GlobalArena commitArena(s_ScratchArena);

    NWB::Impl::CsgShapeRegistry registry(testWorld.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    const auto slabId = registry.registerShape<TestProjectSlabShape>(false, Name(NWB::Tests::CsgCases::s_CsgInputNameProjectCsgCustom), "project/csg/generated/custom.slangi");
    ASSERT_TRUE(slabId);
    EXPECT_EQ(registry.shapeTypeCount(), 5u);

    const auto defaultBounds = registry.buildShapeBounds(*slabId, MatrixIdentity(), Span<const u8>{});
    ASSERT_TRUE(defaultBounds);
    EXPECT_TRUE(defaultBounds->finiteBounds);
    EXPECT_FLOAT_EQ(VectorGetX(defaultBounds->minBounds), -0.5f);
    EXPECT_FLOAT_EQ(VectorGetX(defaultBounds->maxBounds), 0.5f);

    const auto defaultDistance = registry.evaluateSignedDistance(*slabId, VectorSet(-0.25f, 0.0f, 0.0f, 1.0f), Span<const u8>{});
    ASSERT_TRUE(defaultDistance);
    EXPECT_FLOAT_EQ(*defaultDistance, 0.25f);

    auto makeVertex = [](const f32 x, const f32 y, const f32 z)noexcept{
        NWB::Impl::CsgDeformVertex vertex;
        vertex.position = Float3U(x, y, z);
        vertex.normal = Float4(0.0f, 0.0f, 1.0f, 0.0f);
        vertex.tangent = Float4(1.0f, 0.0f, 0.0f, 1.0f);
        vertex.uv0 = Float2U(0.5f, 0.5f);
        vertex.color = Float4(1.0f, 1.0f, 1.0f, 1.0f);
        return vertex;
    };

    const Array<NWB::Impl::CsgDeformVertex, 8u> inputVertices = {
        makeVertex(-1.0f, -1.0f, -1.0f),
        makeVertex(1.0f, -1.0f, -1.0f),
        makeVertex(1.0f, 1.0f, -1.0f),
        makeVertex(-1.0f, 1.0f, -1.0f),
        makeVertex(-1.0f, -1.0f, 1.0f),
        makeVertex(1.0f, -1.0f, 1.0f),
        makeVertex(1.0f, 1.0f, 1.0f),
        makeVertex(-1.0f, 1.0f, 1.0f),
    };
    const Array<NWB::Impl::CsgDeformTriangle, 12u> inputTriangles = {
        NWB::Impl::CsgDeformTriangle{ { 0u, 1u, 2u } }, NWB::Impl::CsgDeformTriangle{ { 0u, 2u, 3u } },
        NWB::Impl::CsgDeformTriangle{ { 4u, 6u, 5u } }, NWB::Impl::CsgDeformTriangle{ { 4u, 7u, 6u } },
        NWB::Impl::CsgDeformTriangle{ { 0u, 4u, 5u } }, NWB::Impl::CsgDeformTriangle{ { 0u, 5u, 1u } },
        NWB::Impl::CsgDeformTriangle{ { 2u, 6u, 7u } }, NWB::Impl::CsgDeformTriangle{ { 2u, 7u, 3u } },
        NWB::Impl::CsgDeformTriangle{ { 0u, 3u, 7u } }, NWB::Impl::CsgDeformTriangle{ { 0u, 7u, 4u } },
        NWB::Impl::CsgDeformTriangle{ { 1u, 5u, 6u } }, NWB::Impl::CsgDeformTriangle{ { 1u, 6u, 2u } },
    };

    NWB::Impl::CsgDeformCutDesc cut;
    cut.active = true;
    cut.shape.shapeType = Name(NWB::Tests::CsgCases::s_CsgInputNameProjectCsgSlab);
    cut.shape.worldToShape = ::MatrixIdentity<Float34>();
    cut.shape.parameter0 = Float4(0.5f, 1.0f, 1.0f, 0.0f);

    const NWB::Impl::CsgDeformBuildOptions options;
    const Span<const NWB::Impl::CsgDeformCutDesc> cuts(&cut, 1u);
    EXPECT_FALSE(NWB::Impl::CheckCsgDeformCutsViability(scratchArena, inputVertices, inputTriangles, cuts, options).viable);
    EXPECT_TRUE(NWB::Impl::CheckCsgDeformCutsViability(scratchArena, inputVertices, inputTriangles, cuts, options, &registry).viable);

    NWB::Impl::CsgDeformVertexVector<NWB::Core::Alloc::GlobalArena> commitVertices(commitArena);
    NWB::Impl::CsgDeformTriangleVector<NWB::Core::Alloc::GlobalArena> commitTriangles(commitArena);
    const auto commit = NWB::Impl::CommitCsgDeformCuts(scratchArena, commitArena, inputVertices, inputTriangles, cuts, options, &registry, commitVertices, commitTriangles);
    ASSERT_TRUE(commit);
    EXPECT_EQ(commit->appliedCutCount, 1u);
    ASSERT_FALSE(commitTriangles.empty());
    for(const NWB::Impl::CsgDeformTriangle& triangle : commitTriangles){
        for(const u32 index : triangle.indices){
            ASSERT_LT(index, commitVertices.size());
            EXPECT_GE(commitVertices[index].position.x, -0.5001f);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

