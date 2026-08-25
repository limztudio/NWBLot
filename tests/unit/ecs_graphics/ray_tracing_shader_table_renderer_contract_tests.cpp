// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ray_tracing_shader_table_renderer_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct RendererShaderTableContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<RendererShaderTableContractTestArenaTag>;


static usize CountText(const AStringView text, const AStringView expected){
    if(expected.empty())
        return 0u;

    usize count = 0u;
    usize offset = 0u;
    while(offset < text.size()){
        const usize found = text.find(expected, offset);
        if(found == AStringView::npos)
            break;
        ++count;
        offset = found + expected.size();
    }
    return count;
}

static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hardware-caustic pipeline construction must cache one complete, validated pipeline/table pair or neither member.
// This CPU-only source contract keeps the failure-path publication invariant covered on hosts without ray tracing.
TEST(EcsGraphics, CausticRayTracingPipelineAndShaderTablePublishAsOneValidatedPair){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString source;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_caustics.cpp", source));
    const AStringView fullSource(source.data(), source.size());
    const usize functionBegin = fullSource.find("bool RendererRayTracingSystem::ensureCausticRtPipeline(){");
    const usize functionEnd = fullSource.find("bool RendererRayTracingSystem::hasHwCausticWork()const noexcept", functionBegin);
    ASSERT_NE(functionBegin, AStringView::npos);
    ASSERT_NE(functionEnd, AStringView::npos);
    ASSERT_LT(functionBegin, functionEnd);
    const AStringView functionSource = fullSource.substr(functionBegin, functionEnd - functionBegin);

    EXPECT_NE(functionSource.find(
        "if(rayTracingState().m_hwCausticPipeline && rayTracingState().m_hwCausticShaderTable)"
    ), AStringView::npos);
    EXPECT_NE(functionSource.find(
        "if(rayTracingState().m_hwCausticPipeline || rayTracingState().m_hwCausticShaderTable){"
    ), AStringView::npos);
    EXPECT_EQ(CountText(functionSource, "rayTracingState().m_hwCausticPipeline.reset();"), 1u);
    EXPECT_EQ(CountText(functionSource, "rayTracingState().m_hwCausticShaderTable.reset();"), 1u);

    const usize pipelineCandidate = functionSource.find(
        "Core::RayTracingPipelineHandle pipeline = device.createRayTracingPipeline(pipelineDesc);"
    );
    const usize tableCandidate = functionSource.find(
        "Core::RayTracingShaderTableHandle shaderTable = pipeline->createShaderTable();"
    );
    const usize rayGenerationCheck = functionSource.find(
        "!shaderTable->setRayGenerationShader(__hidden_caustics::s_HwRaygenExportName)"
    );
    const usize missCheck = functionSource.find(
        "shaderTable->addMissShader(__hidden_caustics::s_HwMissExportName) != 0u"
    );
    const usize hitCheck = functionSource.find(
        "shaderTable->addHitGroup(__hidden_caustics::s_HwHitGroupExportName) != 0u"
    );
    const usize pipelinePublication = functionSource.find(
        "rayTracingState().m_hwCausticPipeline = Move(pipeline);"
    );
    const usize tablePublication = functionSource.find(
        "rayTracingState().m_hwCausticShaderTable = Move(shaderTable);"
    );

    ASSERT_NE(pipelineCandidate, AStringView::npos);
    ASSERT_NE(tableCandidate, AStringView::npos);
    ASSERT_NE(rayGenerationCheck, AStringView::npos);
    ASSERT_NE(missCheck, AStringView::npos);
    ASSERT_NE(hitCheck, AStringView::npos);
    ASSERT_NE(pipelinePublication, AStringView::npos);
    ASSERT_NE(tablePublication, AStringView::npos);
    EXPECT_LT(pipelineCandidate, tableCandidate);
    EXPECT_LT(tableCandidate, rayGenerationCheck);
    EXPECT_LT(rayGenerationCheck, missCheck);
    EXPECT_LT(missCheck, hitCheck);
    EXPECT_LT(hitCheck, pipelinePublication);
    EXPECT_LT(pipelinePublication, tablePublication);

    EXPECT_EQ(CountText(functionSource, "rayTracingState().m_hwCausticPipeline = Move(pipeline);"), 1u);
    EXPECT_EQ(CountText(functionSource, "rayTracingState().m_hwCausticShaderTable = Move(shaderTable);"), 1u);
    EXPECT_EQ(CountText(functionSource, "rayTracingState().m_hwCausticPipeline = device.createRayTracingPipeline"), 0u);
    EXPECT_EQ(CountText(functionSource, "rayTracingState().m_hwCausticShaderTable = pipeline->createShaderTable"), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

