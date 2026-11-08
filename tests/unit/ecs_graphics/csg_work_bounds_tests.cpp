// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_csg/module.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <tests/common/test_context.h>

#include <global/limit.h>
#include <global/span.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_work_bounds_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, CsgNonFiniteReceiverBoundsDisableAabbCulling){
    NWB::Impl::CsgReceiverCpuBounds posedReceiverBounds;
    posedReceiverBounds.minBounds = Float3Int(-1.0f, -1.0f, -1.0f, NWB::Impl::s_CsgBoundsValidFlag);
    posedReceiverBounds.maxBounds = Float3Int(1.0f, 1.0f, 1.0f, 0);

    EXPECT_FALSE(NWB::Impl::CsgReceiverBoundsCanCull(posedReceiverBounds));

    posedReceiverBounds.minBounds.w |= NWB::Impl::s_CsgBoundsFiniteFlag;
    EXPECT_TRUE(NWB::Impl::CsgReceiverBoundsCanCull(posedReceiverBounds));
}


TEST(EcsGraphics, CsgCutterWorkBoundsIgnoreUntrustedReceiverAndKeepTrustedRejection){
    NWB::Tests::TestArena<> arena;
    NWB::Impl::CsgShapeRegistry registry(arena.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    NWB::Impl::CsgShapeTypeInfo shape(arena.arena);
    const auto foundShape1 = registry.findShapeType(NWB::Impl::s_CsgBoxShapeName);
    ASSERT_TRUE(foundShape1);
    shape = *foundShape1;
    NWB::Impl::CsgBoxShapeParameters parameters;
    parameters.halfExtents = Float4(0.25f, 0.125f, 0.5f, 0.0f);
    const SIMDMatrix shapeToWorld = MatrixTranslation(0.5f, 0.25f, 2.0f);
    NWB::Impl::CsgClipWorkBounds receiver;
    receiver.minBounds = Float4(-20.0f, -20.0f, -20.0f, 0.0f);
    receiver.maxBounds = Float4(-10.0f, -10.0f, -10.0f, 0.0f);
    NWB::Impl::CsgClipWorkBounds work;
    const BinaryByteView parameterBytes(reinterpret_cast<const u8*>(&parameters), sizeof(parameters));
    ASSERT_TRUE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, shapeToWorld, parameterBytes, receiver, work));
    ASSERT_TRUE(work.valid);
    EXPECT_FLOAT_EQ(work.minBounds.x, 0.25f);
    EXPECT_FLOAT_EQ(work.minBounds.y, 0.125f);
    EXPECT_FLOAT_EQ(work.minBounds.z, 1.5f);
    EXPECT_FLOAT_EQ(work.maxBounds.x, 0.75f);
    EXPECT_FLOAT_EQ(work.maxBounds.y, 0.375f);
    EXPECT_FLOAT_EQ(work.maxBounds.z, 2.5f);
    receiver.valid = true;
    EXPECT_FALSE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, shapeToWorld, parameterBytes, receiver, work));
    EXPECT_FALSE(work.valid);
    receiver.minBounds = Float4(0.5f, 0.25f, 1.75f, 0.0f);
    receiver.maxBounds = Float4(1.0f, 1.0f, 3.0f, 0.0f);
    ASSERT_TRUE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, shapeToWorld, parameterBytes, receiver, work));
    ASSERT_TRUE(work.valid);
    EXPECT_FLOAT_EQ(work.minBounds.x, 0.5f);
    EXPECT_FLOAT_EQ(work.minBounds.y, 0.25f);
    EXPECT_FLOAT_EQ(work.minBounds.z, 1.75f);
    EXPECT_FLOAT_EQ(work.maxBounds.x, 0.75f);
    EXPECT_FLOAT_EQ(work.maxBounds.y, 0.375f);
    EXPECT_FLOAT_EQ(work.maxBounds.z, 2.5f);
}


TEST(EcsGraphics, CsgCutterWorkBoundsKeepUnknownReceiverFallbacksAndResetPreviousBounds){
    NWB::Tests::TestArena<> arena;
    NWB::Impl::CsgShapeRegistry registry(arena.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    NWB::Impl::CsgShapeTypeInfo shape(arena.arena);
    const auto foundShape2 = registry.findShapeType(NWB::Impl::s_CsgBoxShapeName);
    ASSERT_TRUE(foundShape2);
    shape = *foundShape2;
    NWB::Impl::CsgClipWorkBounds receiver;
    NWB::Impl::CsgClipWorkBounds work;
    ASSERT_TRUE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, MatrixIdentity(), BinaryByteView{}, receiver, work));
    ASSERT_TRUE(work.valid);

    NWB::Impl::CsgBoxShapeParameters invalidParameters;
    invalidParameters.halfExtents.x = 0.0f;
    const BinaryByteView invalidParameterBytes(reinterpret_cast<const u8*>(&invalidParameters), sizeof(invalidParameters));
    EXPECT_TRUE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, MatrixIdentity(), invalidParameterBytes, receiver, work));
    EXPECT_FALSE(work.valid);
    receiver.minBounds = Float4(-2.0f, -2.0f, -2.0f, -2.0f);
    receiver.maxBounds = Float4(2.0f, 2.0f, 2.0f, 2.0f);
    receiver.valid = true;
    EXPECT_FALSE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, MatrixIdentity(), invalidParameterBytes, receiver, work));
    EXPECT_FALSE(work.valid);

    const auto foundShape3 = registry.findShapeType(NWB::Impl::s_CsgPlaneShapeName);
    ASSERT_TRUE(foundShape3);
    shape = *foundShape3;
    ASSERT_TRUE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, MatrixIdentity(), BinaryByteView{}, receiver, work));
    ASSERT_TRUE(work.valid);
    EXPECT_FLOAT_EQ(work.minBounds.x, -2.0f);
    EXPECT_FLOAT_EQ(work.maxBounds.x, 2.0f);
    receiver.valid = false;
    EXPECT_TRUE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, MatrixIdentity(), BinaryByteView{}, receiver, work));
    EXPECT_FALSE(work.valid);

    const auto foundShape4 = registry.findShapeType(NWB::Impl::s_CsgBoxShapeName);
    ASSERT_TRUE(foundShape4);
    shape = *foundShape4;
    NWB::Impl::CsgShapeTypeDesc custom = shape.desc;
    custom.name = Name("tests/csg/finite_custom_cutter");
    custom.shaderModule = Name("tests/csg/finite_custom_cutter_eval");
    custom.shaderModuleInclude = "tests/csg/finite_custom_cutter_eval.slangi";
    const auto customId = registry.registerShapeType(custom);
    ASSERT_TRUE(customId);
    const auto foundShape5 = registry.findShapeType(*customId);
    ASSERT_TRUE(foundShape5);
    shape = *foundShape5;
    EXPECT_TRUE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, MatrixIdentity(), BinaryByteView{}, receiver, work));
    EXPECT_FALSE(work.valid);
    receiver.valid = true;
    ASSERT_TRUE(NWB::Impl::ResolveCsgClipWorkBounds(registry, shape, MatrixIdentity(), BinaryByteView{}, receiver, work));
    ASSERT_TRUE(work.valid);
    EXPECT_FLOAT_EQ(work.minBounds.x, -1.0f);
    EXPECT_FLOAT_EQ(work.maxBounds.x, 1.0f);
}


TEST(EcsGraphics, CsgReceiverWorkRegionFallsBackConservativelyAndClampsLargeProjections){
    NWB::Impl::CsgFrameWorkRegion unknown;
    const NWB::Impl::CsgWorkRect unknownRect = unknown.resolveRect(1001u, 701u);
    EXPECT_EQ(unknownRect.minX, 0);
    EXPECT_EQ(unknownRect.minY, 0);
    EXPECT_EQ(unknownRect.maxX, 1001);
    EXPECT_EQ(unknownRect.maxY, 701);

    NWB::Impl::CsgFrameWorkRegion crossing;
    const SIMDMatrix projMat = MatrixPerspectiveFovLH(s_PIDIV2, 1.0f, 0.1f, 100.0f);
    crossing.expandWorldBounds(projMat, VectorSet(-1.0f, -1.0f, -1.0f, 0.0f), VectorSet(1.0f, 1.0f, 1.0f, 0.0f), 1001u, 701u);
    EXPECT_TRUE(crossing.fullFrame);
    EXPECT_EQ(crossing.resolveRect(1001u, 701u).maxX, 1001);

    NWB::Impl::CsgFrameWorkRegion invalid;
    const SIMDMatrix identityMat = MatrixIdentity();
    invalid.expandWorldBounds(identityMat, VectorSet(1.0f, 1.0f, 1.0f, 0.0f), VectorSet(0.0f, 0.0f, 0.0f, 0.0f), 1001u, 701u);
    EXPECT_TRUE(invalid.fullFrame);
    NWB::Impl::CsgFrameWorkRegion nonfinite;
    nonfinite.expandWorldBounds(identityMat, VectorSet(-Limit<f32>::s_Infinity, -1.0f, 1.0f, 0.0f), VectorSet(1.0f, 1.0f, 2.0f, 0.0f), 1001u, 701u);
    EXPECT_TRUE(nonfinite.fullFrame);

    NWB::Impl::CsgFrameWorkRegion large;
    large.expandWorldBounds(identityMat, VectorSet(-1e20f, -0.25f, 1.0f, 0.0f), VectorSet(0.0f, 0.25f, 2.0f, 0.0f), 1000u, 600u);
    ASSERT_TRUE(large.bounded());
    EXPECT_EQ(large.minX, 0u);
    EXPECT_EQ(large.maxX, 502u);
    large.expandFull();
    EXPECT_EQ(large.resolveRect(1000u, 600u).maxX, 1000);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

