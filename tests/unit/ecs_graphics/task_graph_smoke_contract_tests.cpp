// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_smoke_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TESTS = "tests";
static constexpr AStringView s_SMOKE = "smoke";
static constexpr AStringView s_TRANSPARENT_MULTI_PROJECT_CPP = "transparent_multi_project.cpp";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Windows may deny foreground activation to the parent smoke harness after bootstrap. Keep the opt-in local to the
// frame-lagged executable so it bypasses only its focus throttle, uses Graphics' normal render-pass extension, and
// unregisters before the renderer/world can be destroyed.
TEST(EcsGraphics, FrameLaggedSmokeRemovesPassBeforeDestroyingCapturedWorld){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_TESTS / s_SMOKE / s_TRANSPARENT_MULTI_PROJECT_CPP, smokeSource));
    const AStringView smoke(smokeSource.data(), smokeSource.size());


    const usize shutdownOffset = smoke.find("virtual void onShutdown()override");
    const usize updateOffset = smoke.find("virtual bool onUpdate", shutdownOffset);
    ASSERT_NE(shutdownOffset, AStringView::npos);
    ASSERT_NE(updateOffset, AStringView::npos);
    const AStringView shutdown = smoke.substr(shutdownOffset, updateOffset - shutdownOffset);
    const usize removeOffset = shutdown.find("removeFrameLaggedAsyncLightingUnfocusedPass();");
    const usize destroyOffset = shutdown.find("destroyWorld();");
    ASSERT_NE(removeOffset, AStringView::npos);
    ASSERT_NE(destroyOffset, AStringView::npos);
    EXPECT_LT(removeOffset, destroyOffset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

