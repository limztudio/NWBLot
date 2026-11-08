// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/runtime/runtime_standalone_graph.h>

#include <global/exception.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_standalone_workspace_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(StandaloneGraphWorkspacePool, OverlappingLeasesKeepDistinctWorkspacesAndReuseReleasedStorage){
    TestArena<> arena;
    Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool pool(arena.arena);
    Core::GraphicsModuleDetail::StandaloneGraphWorkspace* firstWorkspace = nullptr;
    {
        const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease first(pool);
        firstWorkspace = first.workspace();
        ASSERT_NE(firstWorkspace, nullptr);
        {
            const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease second(pool);
            ASSERT_NE(second.workspace(), nullptr);
            EXPECT_NE(firstWorkspace, second.workspace());
            EXPECT_FALSE(pool.releaseCommandListCaches());
        }
        EXPECT_FALSE(pool.releaseCommandListCaches());
    }
    EXPECT_TRUE(pool.releaseCommandListCaches());
    {
        const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease reused(pool);
        EXPECT_EQ(reused.workspace(), firstWorkspace);
    }
    EXPECT_TRUE(pool.releaseCommandListCaches());
}

TEST(StandaloneGraphWorkspacePool, ExceptionDiscardsLeaseWithoutBlockingNextSubmission){
    TestArena<> arena;
    Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool pool(arena.arena);
    EXPECT_THROW({
        const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease lease(pool);
        ASSERT_NE(lease.workspace(), nullptr);
        throw RuntimeException("expected workspace unwind");
    }, RuntimeException);
    EXPECT_TRUE(pool.releaseCommandListCaches());
    {
        const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease next(pool);
        ASSERT_NE(next.workspace(), nullptr);
        EXPECT_FALSE(pool.releaseCommandListCaches());
    }
    EXPECT_TRUE(pool.releaseCommandListCaches());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


