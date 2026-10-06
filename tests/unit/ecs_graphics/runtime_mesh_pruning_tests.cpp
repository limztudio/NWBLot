// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/system.h>
#include <impl/ecs_render/mesh/runtime_mesh_pruning.h>

#include <core/ecs/entity.h>

#include <global/name.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_mesh_pruning_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

struct PruneContext{
    Core::Alloc::GlobalArena arena{ Name("tests/runtime_mesh_pruning/owner") };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::ECS::World world{ arena, cpuScheduler };
    MeshSystem& meshSystem = world.addSystem<MeshSystem>(world);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(RenderableMeshResolution, StaticAttachmentWithoutAssetIsUnavailable){
    PruneContext context;
    auto entity = context.world.createEntity();
    auto& mesh = entity.addComponent<MeshComponent>();
    RenderableMeshDesc description;
    EXPECT_EQ(context.meshSystem.resolveRenderableMeshStatus(entity.id(), description), RenderableMeshResolution::Unavailable);
    EXPECT_FALSE(description.valid());
    mesh.mesh = Core::Assets::AssetRef<Mesh>("tests/runtime_mesh_pruning/static_attachment");
    EXPECT_EQ(context.meshSystem.resolveRenderableMeshStatus(entity.id(), description), RenderableMeshResolution::Ready);
    EXPECT_TRUE(description.valid());
    EXPECT_FALSE(description.runtime);
}

TEST(RenderableMeshResolution, SkinnedAttachmentWithoutProviderIsUnavailable){
    PruneContext context;
    auto entity = context.world.createEntity();
    entity.addComponent<SkinnedMeshBindingComponent>();
    RenderableMeshDesc description;
    EXPECT_EQ(context.meshSystem.resolveRenderableMeshStatus(entity.id(), description), RenderableMeshResolution::Unavailable);
    EXPECT_FALSE(description.valid());
}

TEST(RuntimeMeshPruning, RequestedIdentitiesKeepFullNameAndVersionAcrossHintedAndGrowingIndices){
    Name names[40u];
    for(usize index = 0u; index < 40u; ++index){
        NameHash hash = ComputeNameHash("tests/runtime_mesh_pruning/full_identity");
        hash.qwords[s_NameHashLaneCount - 1u] ^= index + 1u;
        names[index] = Name(hash);
    }
    for(const usize capacityHint : { 0u, 64u }){
        Core::Alloc::ScratchArena scratch(Name("tests/runtime_mesh_pruning/request_scratch"));
        RuntimeMeshRequestSet requests(scratch, capacityHint);
        requests.add(s_NameNone, 7u);
        requests.markLive(s_NameNone, 7u);
        EXPECT_TRUE(requests.complete());
        EXPECT_FALSE(requests.containsLive(s_NameNone, 7u));
        for(usize index = 0u; index < 40u; ++index){
            requests.add(names[index], 7u);
            requests.add(names[index], 7u);
            if(index < 16u)
                requests.markLive(names[index], 7u);
        }
        requests.add(names[0u], 0u);
        requests.add(names[0u], (1ull << 40u) + 7u);
        for(usize index = 0u; index < 40u; ++index){
            EXPECT_EQ(requests.containsLive(names[index], 7u), index < 16u);
            EXPECT_FALSE(requests.containsLive(names[index], 8u));
        }
        requests.markLive(Name("tests/runtime_mesh_pruning/not_requested"), 7u);
        EXPECT_FALSE(requests.complete());
        for(usize index = 0u; index < 40u; ++index){
            requests.markLive(names[index], 7u);
            requests.markLive(names[index], 7u);
        }
        EXPECT_FALSE(requests.complete());
        EXPECT_FALSE(requests.containsLive(names[0u], 0u));
        EXPECT_FALSE(requests.containsLive(names[0u], (1ull << 40u) + 7u));
        requests.markLive(names[0u], (1ull << 40u) + 7u);
        EXPECT_FALSE(requests.complete());
        requests.markLive(names[0u], 0u);
        EXPECT_TRUE(requests.complete());
        requests.add(names[0u], 7u);
        EXPECT_TRUE(requests.complete());
        for(usize index = 0u; index < 40u; ++index)
            EXPECT_TRUE(requests.containsLive(names[index], 7u));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

