// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_resource_import_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RejectsMalformedQueueSharingWithoutDeclarationMutation){
    constexpr u8 s_UnknownQueueSharingBit = 1u << 7u;
    constexpr Graphics::ResourceQueueSharing::Mask s_InvalidQueueSharingMasks[] = {
        static_cast<Graphics::ResourceQueueSharing::Mask>(s_UnknownQueueSharingBit),
        static_cast<Graphics::ResourceQueueSharing::Mask>(
            static_cast<u8>(Graphics::ResourceQueueSharing::Graphics) | s_UnknownQueueSharingBit
        ),
    };
    constexpr Graphics::GpuGraphResourceType::Enum s_MetadataResourceTypes[] = {
        Graphics::GpuGraphResourceType::Texture,
        Graphics::GpuGraphResourceType::Buffer,
        Graphics::GpuGraphResourceType::AccelStruct,
        Graphics::GpuGraphResourceType::HazardDomain,
    };
    const auto expectRejectedWithoutMutation = [](
        const Graphics::GpuGraphResourceType::Enum resourceType,
        const Graphics::ResourceQueueSharing::Mask queueSharing,
        const bool useHazardDomainWrapper
    ){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceDesc desc = Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/invalid_queue_sharing_metadata"))
            .setMarkerLabel("Invalid Queue Sharing Metadata")
            .setType(resourceType)
            .setInitialState(
                resourceType == Graphics::GpuGraphResourceType::HazardDomain
                    ? Graphics::ResourceStates::Unknown
                    : Graphics::ResourceStates::Common
            )
            .setQueueSharing(queueSharing)
        ;
        usize resourceCount = 0u;
        u64 declarationRevision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            resourceCount = declarations.resourceCount();
            declarationRevision = declarations.declarationRevision();
        }
        const ArenaMemoryStats memoryStats = testArena.arena.memoryStats();
        const Graphics::GpuGraphResourceId resource = useHazardDomainWrapper
            ? graph.importHazardDomain(desc)
            : graph.importResource(desc)
        ;

        EXPECT_FALSE(resource.valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceCount(), resourceCount);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
        ExpectMemoryStatsEqual(memoryStats, testArena.arena.memoryStats());
    };

    for(const Graphics::ResourceQueueSharing::Mask queueSharing : s_InvalidQueueSharingMasks){
        SCOPED_TRACE(static_cast<u32>(queueSharing));
        for(const Graphics::GpuGraphResourceType::Enum resourceType : s_MetadataResourceTypes){
            SCOPED_TRACE(static_cast<u32>(resourceType));
            expectRejectedWithoutMutation(resourceType, queueSharing, false);
        }
        expectRejectedWithoutMutation(Graphics::GpuGraphResourceType::HazardDomain, queueSharing, true);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

