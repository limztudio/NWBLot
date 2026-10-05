// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"

#include <impl/assets/graphics/avboit/binding_slots.h>
#include <impl/ecs_render/avboit/avboit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_IMPL = "impl";
static constexpr AStringView s_ASSETS = "assets";
static constexpr AStringView s_GRAPHICS = "graphics";
static constexpr AStringView s_DEFERRED = "deferred";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


TEST(EcsGraphics, UncommittedQueriesCannotReconstructSurfaceAndNearestTraversalCompletes){
    TestArena testArena;
    const TestPath graphics = RepoRoot(testArena) / "impl" / "assets" / "graphics";
    AString helperSource;
    ASSERT_TRUE(ReadTextFile(graphics / "raytrace" / "surface_hit.slangi", helperSource));
    const AStringView helper(helperSource.data(), helperSource.size());
    EXPECT_FALSE(ContainsText(helper, "RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH"));
    EXPECT_TRUE(ContainsText(helper, "RayQuery<RAY_FLAG_FORCE_OPAQUE> query"));
    const usize closestFunction = helper.find("NwbRayTraceGeometryHit nwbRayTraceClosestGeometryHit(");
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
}

TEST(EcsGraphics, ClearRefractionCapturePrecedesCoverageRejectionAndHonorsOpaqueDepth){
    TestArena testArena;
    AString shaderSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / s_IMPL / s_ASSETS / s_GRAPHICS / "avboit" / "accumulate_ps_authoring.slangi", shaderSource));
    const AStringView shader(shaderSource.data(), shaderSource.size());
    const usize capture = shader.find("if(refractionCapture)");
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
}

TEST(EcsGraphics, StandaloneAvboitModeNeverSamplesMissingRefractionDescriptors){
    TestArena testArena;
    AString shaderSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / s_IMPL / s_ASSETS / s_GRAPHICS / "avboit" / "common.slangi", shaderSource));
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

TEST(EcsGraphics, AuxiliaryReadsRequireValidRefractionAndReflectionSlots){
    TestArena testArena;
    const TestPath root = RepoRoot(testArena);
    AString shaderSource;
    ASSERT_TRUE(ReadTextFile(root / s_IMPL / s_ASSETS / s_GRAPHICS / s_DEFERRED / "composite_cs.slang", shaderSource));
    const AStringView shader(shaderSource.data(), shaderSource.size());
    const usize guard = shader.find("if(g_NwbDeferredCompositePushConstants.refractionResources != 0u)");
    const usize auxiliaryRead = shader.find("g_NwbDeferredBindlessResources.refractionSlots1");
    ASSERT_NE(guard, AStringView::npos);
    ASSERT_NE(auxiliaryRead, AStringView::npos);
    EXPECT_LT(guard, auxiliaryRead);
    EXPECT_TRUE(ContainsText(shader, "opaqueReflectionSlot != 0xffffffffu"));
    EXPECT_TRUE(ContainsText(shader, "glassReflectionSlot != 0xffffffffu"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

