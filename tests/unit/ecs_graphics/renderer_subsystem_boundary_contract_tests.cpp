// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_subsystem_boundary_contract_tests{
using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct RendererSubsystemBoundaryContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<RendererSubsystemBoundaryContractTestArenaTag>;


static bool ContainsText(const AStringView text, const AStringView expected){
    return text.find(expected) != AStringView::npos;
}

static bool ReadCompactSource(const TestPath& path, AString& outSource){
    if(!ReadTextFile(path, outSource))
        return false;
    AString compact;
    compact.reserve(outSource.size());
    for(const char ch : outSource){
        if(ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' && ch != '\f' && ch != '\v')
            compact += ch;
    }
    outSource = Move(compact);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, FailedMeshBindingRegistrationPreservesCurrentGenerationAndPublishesBeforeRetirement){
    TestArena testArena;
    const TestPath repoRoot = NWB::Tests::RepoRootOf(testArena.arena, __FILE__);
    AString frameBindings;
    AString meshBindings;
    AString materialDraw;
    ASSERT_TRUE(ReadCompactSource(repoRoot / "impl/ecs_render/shared/renderer_frame_bindings.h", frameBindings));
    ASSERT_TRUE(ReadCompactSource(repoRoot / "impl/ecs_render/mesh/mesh_bindings.cpp", meshBindings));
    ASSERT_TRUE(ReadCompactSource(repoRoot / "impl/ecs_render/material/material_pass_draw.cpp", materialDraw));

    EXPECT_TRUE(ContainsText(frameBindings, "instanceBufferCapacity>=instanceCount"));
    EXPECT_TRUE(ContainsText(frameBindings, "materialTypedBufferCapacity>=Max<usize>(materialTypedByteCount,sizeof(u32))"));
    EXPECT_TRUE(ContainsText(frameBindings, "instanceBuffer==materialBuffers.instanceBuffer"));
    EXPECT_TRUE(ContainsText(frameBindings, "materialTypedBuffer==materialBuffers.materialTypedBuffer"));
    EXPECT_TRUE(ContainsText(frameBindings, "instanceBufferCapacity==materialBuffers.instanceBufferCapacity"));
    EXPECT_TRUE(ContainsText(frameBindings, "materialTypedBufferCapacity==materialBuffers.materialTypedBufferCapacity"));
    EXPECT_TRUE(ContainsText(materialDraw, "if(!context.frameBindings.bindingValid())returnfalse;"));

    const usize failedRegistration = meshBindings.find("if(!registered){");
    const usize retainedGeneration = meshBindings.find("MeshFrameBindingSnapshotretired=Move(m_meshState.m_frameBindings);");
    const usize publishedGeneration = meshBindings.find("m_meshState.m_frameBindings=Move(replacement);");
    const usize retiredInstanceRelease = meshBindings.find("heap.free(retired.instanceHeapHandle)", publishedGeneration);
    ASSERT_NE(failedRegistration, AStringView::npos);
    ASSERT_NE(retainedGeneration, AStringView::npos);
    ASSERT_NE(publishedGeneration, AStringView::npos);
    ASSERT_NE(retiredInstanceRelease, AStringView::npos);
    EXPECT_LT(failedRegistration, retainedGeneration);
    EXPECT_LT(retainedGeneration, publishedGeneration);
    EXPECT_LT(publishedGeneration, retiredInstanceRelease);
}

TEST(EcsGraphics, RejectedFramePreparationRestoresLightingClassificationAndDefersPublication){
    TestArena testArena;
    const TestPath repoRoot = NWB::Tests::RepoRootOf(testArena.arena, __FILE__);
    AString rootPrefix;
    AString rayTracingState;
    AString rootExecute;
    ASSERT_TRUE(ReadCompactSource(repoRoot / "impl/ecs_render/renderer_frame_pipeline_graphics_prefix.cpp", rootPrefix));
    ASSERT_TRUE(ReadCompactSource(repoRoot / "impl/ecs_render/raytrace/renderer_raytracing_state.cpp", rayTracingState));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl/ecs_render/renderer_frame_pipeline_execute.cpp", rootExecute));

    const usize inputSnapshot = rootPrefix.find("m_raytracingSystem.snapshotLightingClassificationInput()");
    const usize deferredClassification = rootPrefix.find("if(!prefixSceneUploadBuilder.declare(");
    const usize graphicsPrefixAcceptance = rootPrefix.find("if(!m_graphicsPrefixTask.valid())");
    const usize classificationPublication = rootPrefix.find("m_raytracingSystem.publishPreparedLightingClassification(");
    ASSERT_NE(inputSnapshot, AStringView::npos);
    ASSERT_NE(deferredClassification, AStringView::npos);
    ASSERT_NE(graphicsPrefixAcceptance, AStringView::npos);
    ASSERT_NE(classificationPublication, AStringView::npos);
    EXPECT_LT(inputSnapshot, deferredClassification);
    EXPECT_LT(deferredClassification, graphicsPrefixAcceptance);
    EXPECT_LT(graphicsPrefixAcceptance, classificationPublication);

    EXPECT_TRUE(ContainsText(rayTracingState, ".softShadowSlotMask=m_softShadowSlotMask"));
    EXPECT_TRUE(ContainsText(rayTracingState, ".causticLightCount=m_causticLightCount"));
    EXPECT_TRUE(ContainsText(rayTracingState, ".causticEmissionGateLogged=m_causticEmissionGateLogged"));
    EXPECT_TRUE(ContainsText(rayTracingState, "m_softShadowSlotMask=snapshot.softShadowSlotMask;"));
    EXPECT_TRUE(ContainsText(rayTracingState, "m_causticLightCount=snapshot.causticLightCount;"));
    EXPECT_TRUE(ContainsText(rayTracingState, "m_causticEmissionGateLogged=snapshot.causticEmissionGateLogged;"));
    EXPECT_TRUE(ContainsText(rootExecute, "const RayTracingFrameCpuStateSnapshot rayTracingCpuState = m_rayTracingState.captureFrameCpuState();"));
    EXPECT_TRUE(ContainsText(rootExecute, "m_rayTracingState.restorePreparedLightingCpuState(rayTracingCpuState);"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

