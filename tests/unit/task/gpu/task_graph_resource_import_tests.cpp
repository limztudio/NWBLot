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
    constexpr Array<Core::ResourceQueueSharing::Mask, 2u> s_InvalidQueueSharingMasks = {
        static_cast<Core::ResourceQueueSharing::Mask>(s_UnknownQueueSharingBit),
        static_cast<Core::ResourceQueueSharing::Mask>(static_cast<u8>(Core::ResourceQueueSharing::Graphics) | s_UnknownQueueSharingBit),
    };
    constexpr Array<Core::GpuGraphResourceType::Enum, 4u> s_MetadataResourceTypes = { Core::GpuGraphResourceType::Texture, Core::GpuGraphResourceType::Buffer, Core::GpuGraphResourceType::AccelStruct, Core::GpuGraphResourceType::HazardDomain, };
    const auto expectRejectedWithoutMutation = [](const Core::GpuGraphResourceType::Enum resourceType, const Core::ResourceQueueSharing::Mask queueSharing, const b8 useHazardDomainWrapper){
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        const Core::GpuGraphResourceDesc desc = Core::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/invalid_queue_sharing_metadata"))
            .setMarkerLabel("Invalid Queue Sharing Metadata")
            .setType(resourceType)
            .setInitialState(resourceType == Core::GpuGraphResourceType::HazardDomain ? Core::ResourceStates::Unknown : Core::ResourceStates::Common)
            .setQueueSharing(queueSharing)
        ;
        usize resourceCount = 0u;
        u64 declarationRevision = 0u;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            resourceCount = declarations.resourceCount();
            declarationRevision = declarations.declarationRevision();
        }
        const ArenaMemoryStats memoryStats = testArena.arena.memoryStats();
        const auto resource = useHazardDomainWrapper ? graph.importHazardDomain(desc) : graph.importResource(desc);

        EXPECT_FALSE(resource.has_value());
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceCount(), resourceCount);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
        ExpectMemoryStatsEqual(memoryStats, testArena.arena.memoryStats());
    };

    for(const Core::ResourceQueueSharing::Mask queueSharing : s_InvalidQueueSharingMasks){
        SCOPED_TRACE(static_cast<u32>(queueSharing));
        for(const Core::GpuGraphResourceType::Enum resourceType : s_MetadataResourceTypes){
            SCOPED_TRACE(static_cast<u32>(resourceType));
            expectRejectedWithoutMutation(resourceType, queueSharing, false);
        }
        expectRejectedWithoutMutation(Core::GpuGraphResourceType::HazardDomain, queueSharing, true);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

