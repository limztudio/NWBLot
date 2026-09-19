// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"

#include <impl/assets/graphics/avboit/binding_slots.h>
#include <impl/ecs_render/avboit/avboit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;

TEST(EcsGraphics, RefractionSelectorAppendsTwoLanesWithoutMovingExistingResources){
    using Slots = NWB::Impl::DeferredBindlessResourceSlots;
    EXPECT_EQ(sizeof(Slots), 176u);
    EXPECT_EQ(offsetof(Slots, gbufferBaseColor), 0u);
    EXPECT_EQ(offsetof(Slots, opaqueColor), 32u);
    EXPECT_EQ(offsetof(Slots, avboitTransmittance), 48u);
    EXPECT_EQ(offsetof(Slots, compositeColor), 88u);
    EXPECT_EQ(offsetof(Slots, csgCapBackNormal), 96u);
    EXPECT_EQ(offsetof(Slots, csgRemovedIntervalCapNormal), 128u);
    EXPECT_EQ(offsetof(Slots, refractionDepth), 144u);
    EXPECT_EQ(offsetof(Slots, refractionResolveStorage), 156u);
    EXPECT_EQ(offsetof(Slots, refractionResolve), 160u);
    EXPECT_EQ(offsetof(Slots, avboitForegroundColor), 164u);
    EXPECT_EQ(offsetof(Slots, avboitForegroundExtinction), 168u);
    EXPECT_EQ(offsetof(Slots, refractionInstance), 172u);
    EXPECT_EQ(sizeof(NWB::Impl::RendererAvboitPushConstants), 64u);
    EXPECT_EQ(NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE, 128u);

    TestArena testArena;
    AString shaderSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "deferred" / "bindless_resources.slangi", shaderSource));
    EXPECT_EQ(CountText(AStringView(shaderSource.data(), shaderSource.size()), "    uint4 "), 11u);
}

TEST(EcsGraphics, RefractionUsesSharedNearestSurfaceHitWithExplicitMaterialContext){
    TestArena testArena;
    const TestPath graphics = RepoRoot(testArena) / "impl" / "assets" / "graphics";
    AString helperSource;
    AString resolverSource;
    ASSERT_TRUE(ReadTextFile(graphics / "raytrace" / "surface_hit.slangi", helperSource));
    ASSERT_TRUE(ReadTextFile(graphics / "refraction" / "resolve_hw_cs.slang", resolverSource));
    const AStringView helper(helperSource.data(), helperSource.size());
    const AStringView resolver(resolverSource.data(), resolverSource.size());
    EXPECT_FALSE(ContainsText(helper, "g_NwbRefraction"));
    EXPECT_FALSE(ContainsText(helper, "RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH"));
    EXPECT_TRUE(ContainsText(helper, "RayQuery<RAY_FLAG_FORCE_OPAQUE> query"));
    EXPECT_TRUE(ContainsText(helper, "NwbHeapRtInstanceMaterials(instanceMaterialHeapSlot)[hit.instance]"));
    const usize closestFunction = helper.find("NwbRayTraceSurfaceHit nwbRayTraceClosestSurfaceHit(");
    const usize reconstructFunction = helper.find("NwbRayTraceSurfaceHit nwbRayTraceReconstructSurfaceHit(");
    ASSERT_NE(closestFunction, AStringView::npos);
    ASSERT_NE(reconstructFunction, AStringView::npos);
    const usize completeQuery = helper.find("while(query.Proceed()){}", closestFunction);
    const usize committedGuard = helper.find("if(query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)", closestFunction);
    const usize reconstructCall = helper.find("return nwbRayTraceReconstructSurfaceHit(", closestFunction);
    const usize materialRead = helper.find("const NwbRtInstanceMaterial material", reconstructFunction);
    ASSERT_NE(completeQuery, AStringView::npos);
    ASSERT_NE(committedGuard, AStringView::npos);
    ASSERT_NE(materialRead, AStringView::npos);
    ASSERT_NE(reconstructCall, AStringView::npos);
    EXPECT_LT(completeQuery, committedGuard);
    EXPECT_LT(committedGuard, reconstructCall);
    EXPECT_LT(reconstructFunction, materialRead);
    EXPECT_TRUE(ContainsText(helper, "hit.flags = material.flags;"));
    EXPECT_TRUE(ContainsText(helper, "hit.primitive = primitive;"));
    EXPECT_TRUE(ContainsText(helper, "nwbRayTraceScaleGeometryVector(cross(edge0, edge1))"));
    EXPECT_TRUE(ContainsText(helper, "return nwbRayTraceNormalizeGeometryVector(worldNormal);"));
    EXPECT_TRUE(ContainsText(helper, "return hit;"));
    EXPECT_TRUE(ContainsText(helper, "nwbShadowDispatchSurface(material.shadowTransmittanceModelId, surfaceHit)"));
    EXPECT_TRUE(ContainsText(resolver, "#include \"../raytrace/surface_hit.slangi\""));
    EXPECT_EQ(CountText(resolver, "nwbRayTraceClosestSurfaceHit("), 3u);
    EXPECT_EQ(CountText(resolver, "g_NwbRefractionMaterialContext.sceneSlots.z,"), 3u);
    EXPECT_FALSE(ContainsText(resolver, "RayQuery<"));
}

TEST(EcsGraphics, RefractionCaptureWritesOneNearestSurfaceWithoutBlendingOpticalFields){
    const auto capture = NWB::Impl::BuildRendererAvboitRefractionCaptureRenderState();
    EXPECT_TRUE(capture.depthStencilState.depthTestEnable);
    EXPECT_TRUE(capture.depthStencilState.depthWriteEnable);
    EXPECT_EQ(capture.depthStencilState.depthFunc, NWB::Core::ComparisonFunc::LessOrEqual);
    for(u32 index = 0u; index < NWB_AVBOIT_ACCUM_TARGET_COUNT; ++index){
        EXPECT_FALSE(capture.blendState.targets[index].blendEnable);
        EXPECT_EQ(capture.blendState.targets[index].colorWriteMask, NWB::Core::ColorMask::All);
    }
}

TEST(EcsGraphics, AvboitForegroundAndBackgroundUseMatchingAdditiveAccumulation){
    const auto accumulate = NWB::Impl::BuildRendererAvboitAccumulateRenderState();
    EXPECT_TRUE(accumulate.depthStencilState.depthTestEnable);
    EXPECT_FALSE(accumulate.depthStencilState.depthWriteEnable);
    EXPECT_EQ(NWB_AVBOIT_ACCUM_TARGET_COUNT, 4);
    for(u32 index = 0u; index < NWB_AVBOIT_ACCUM_TARGET_COUNT; ++index){
        const auto& target = accumulate.blendState.targets[index];
        EXPECT_TRUE(target.blendEnable);
        EXPECT_EQ(target.srcBlend, NWB::Core::BlendFactor::One);
        EXPECT_EQ(target.destBlend, NWB::Core::BlendFactor::One);
        EXPECT_EQ(target.blendOp, NWB::Core::BlendOp::Add);
    }
    EXPECT_TRUE(accumulate.blendState.targets[NWB_AVBOIT_ACCUM_COLOR_LOCATION]
        == accumulate.blendState.targets[NWB_AVBOIT_ACCUM_FOREGROUND_COLOR_LOCATION]);
    EXPECT_TRUE(accumulate.blendState.targets[NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION]
        == accumulate.blendState.targets[NWB_AVBOIT_ACCUM_FOREGROUND_EXTINCTION_LOCATION]);
    EXPECT_EQ(accumulate.blendState.targets[NWB_AVBOIT_ACCUM_FOREGROUND_EXTINCTION_LOCATION].colorWriteMask,
        NWB::Core::ColorMask::Red);
}

TEST(EcsGraphics, ClearRefractionCapturePrecedesCoverageRejectionAndHonorsOpaqueDepth){
    TestArena testArena;
    AString shaderSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "avboit" / "accumulate_ps_authoring.slangi", shaderSource));
    const AStringView shader(shaderSource.data(), shaderSource.size());
    const usize capture = shader.find("if(nwbAvboitRefractionCapture())");
    const usize captureReturn = shader.find("return capture;", capture);
    const usize coverageRejection = shader.find("if(alpha <= half(0.0))");
    ASSERT_NE(capture, AStringView::npos);
    ASSERT_NE(captureReturn, AStringView::npos);
    ASSERT_NE(coverageRejection, AStringView::npos);
    EXPECT_LT(captureReturn, coverageRejection);
    const AStringView captureBody = shader.substr(capture, captureReturn - capture);
    EXPECT_TRUE(ContainsText(captureBody, "!isfinite(ior) || ior <= 1.0001"));
    EXPECT_TRUE(ContainsText(captureBody, "if(input.position.z > opaqueDepth)"));
    EXPECT_FALSE(ContainsText(captureBody, "if(alpha"));
    EXPECT_TRUE(ContainsText(captureBody, "float(nwbMeshInstanceIndex()) + 1.0"));
}

TEST(EcsGraphics, StandaloneAvboitModeNeverSamplesMissingRefractionDescriptors){
    TestArena testArena;
    AString shaderSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "avboit" / "common.slangi", shaderSource));
    const AStringView shader(shaderSource.data(), shaderSource.size());
    for(const AStringView function : { AStringView("bool nwbAvboitPrimaryRefractionInstance"), AStringView("float nwbAvboitPrimaryRefractionDepth") }){
        const usize entry = shader.find(function);
        ASSERT_NE(entry, AStringView::npos);
        const usize guard = shader.find("if(!nwbAvboitRefractionEnabled())", entry);
        const usize imageAccess = shader.find("NwbHeapSampledImage2D", entry);
        ASSERT_NE(guard, AStringView::npos);
        ASSERT_NE(imageAccess, AStringView::npos);
        EXPECT_LT(guard, imageAccess);
        EXPECT_TRUE(ContainsText(shader.substr(guard, imageAccess - guard), "return "));
    }
}

TEST(EcsGraphics, CompositeOpticalPolicyMatchesFiveWordPushAbiAndGatesAuxiliaryReads){
    TestArena testArena;
    const TestPath root = RepoRoot(testArena);
    AString cppSource;
    AString shaderSource;
    ASSERT_TRUE(ReadTextFile(root / "impl" / "ecs_render" / "deferred" / "deferred_composite.cpp", cppSource));
    ASSERT_TRUE(ReadTextFile(root / "impl" / "assets" / "graphics" / "deferred" / "composite_cs.slang", shaderSource));
    const AStringView cpp(cppSource.data(), cppSource.size());
    const AStringView shader(shaderSource.data(), shaderSource.size());
    EXPECT_TRUE(ContainsText(cpp, "static_assert(sizeof(CompositePushConstants) == sizeof(u32) * 5u)"));
    EXPECT_TRUE(ContainsText(shader, "uint resourceSlots;\n    uint refractionResources;"));
    const usize guard = shader.find("if(g_NwbDeferredCompositePushConstants.refractionResources != 0u)");
    const usize auxiliaryRead = shader.find("g_NwbDeferredBindlessResources.refractionSlots1");
    ASSERT_NE(guard, AStringView::npos);
    ASSERT_NE(auxiliaryRead, AStringView::npos);
    EXPECT_LT(guard, auxiliaryRead);
    EXPECT_TRUE(ContainsText(shader, "uint opaqueReflectionSlot;\n    uint glassReflectionSlot;\n    uint reflectionDebugView;"));
    EXPECT_TRUE(ContainsText(shader, "opaqueReflectionSlot != 0xffffffffu"));
    EXPECT_TRUE(ContainsText(shader, "glassReflectionSlot != 0xffffffffu"));
    EXPECT_TRUE(ContainsText(shader, "frontColor + frontTransmittance * (transmitted.rgb + glassReflection.rgb)"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

