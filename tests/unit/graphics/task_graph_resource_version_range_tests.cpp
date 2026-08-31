// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_version_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphResourceVersion, RejectsTypedRangesOutsideBackendBoundsAndAcceptsWholeSentinels){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::Alloc::ThreadPool threadPool(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, threadPool, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);

    Graphics::Buffer* const bufferObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        Graphics::BufferDesc().setByteSize(64u).setInitialState(Graphics::ResourceStates::Common)
    );
    Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        Graphics::TextureDesc()
            .setMipLevels(2u)
            .setArraySize(2u)
            .setDimension(Graphics::TextureDimension::Texture2DArray)
            .setInitialState(Graphics::ResourceStates::Common)
    );
    ASSERT_NE(bufferObject, nullptr);
    ASSERT_NE(textureObject, nullptr);
    Graphics::BufferHandle buffer(
        bufferObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::TextureHandle texture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importBuffer(
            buffer,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph_resource_version/typed_buffer_out_of_bounds"))
                .setMarkerLabel("Typed Buffer Out Of Bounds")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
            BufferRange(48u, 32u)
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importBuffer(
            buffer,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph_resource_version/typed_buffer_whole"))
                .setMarkerLabel("Typed Buffer Whole")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
            Graphics::GpuTaskResourceRange{}
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_TRUE(Analyze(graph, analysis));
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importTexture(
            texture,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph_resource_version/typed_texture_out_of_bounds"))
                .setMarkerLabel("Typed Texture Out Of Bounds")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
            TextureRange(1u, 2u, 0u, 2u)
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_FALSE(Analyze(graph, analysis));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_EQ(analysis.diagnostic().resourceVersion, version);
    }

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importTexture(
            texture,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph_resource_version/typed_texture_whole"))
                .setMarkerLabel("Typed Texture Whole")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
            Graphics::GpuTaskResourceRange{}
        );
        ASSERT_TRUE(resource.valid());
        ASSERT_TRUE(version.valid());
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        EXPECT_TRUE(Analyze(graph, analysis));
    }
}

TEST(GpuTaskGraphResourceVersion, RequiresWholeResourceRangesForAccelStructAndHazardDomain){
    const auto analyzeVersion = [](
        const Graphics::GpuGraphResourceType::Enum type,
        const Name& identity,
        const Graphics::GpuTaskResourceRange& range,
        Graphics::GpuTaskGraphAnalysisStatus::Enum& outStatus
    ){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuGraphResourceDesc resourceDesc;
        resourceDesc
            .setIdentity(identity)
            .setMarkerLabel("Whole Resource Version")
            .setType(type)
        ;
        if(type == Graphics::GpuGraphResourceType::AccelStruct)
            resourceDesc.setInitialState(Graphics::ResourceStates::Common);
        const Graphics::GpuGraphResourceId resource = graph.importResource(resourceDesc);
        if(!resource.valid())
            return false;
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
            range
        );
        if(!version.valid())
            return false;
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        const bool valid = Analyze(graph, analysis);
        outStatus = analysis.diagnostic().status;
        return valid;
    };

    Graphics::GpuTaskResourceRange partialRange;
    partialRange.bufferRange = Graphics::BufferRange(16u, 32u);
    Graphics::GpuTaskGraphAnalysisStatus::Enum status = Graphics::GpuTaskGraphAnalysisStatus::Success;
    EXPECT_FALSE(analyzeVersion(
        Graphics::GpuGraphResourceType::AccelStruct,
        Name("tests/task_graph_resource_version/partial_accel_struct"),
        partialRange,
        status
    ));
    EXPECT_EQ(status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
    EXPECT_FALSE(analyzeVersion(
        Graphics::GpuGraphResourceType::HazardDomain,
        Name("tests/task_graph_resource_version/partial_hazard_domain"),
        partialRange,
        status
    ));
    EXPECT_EQ(status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
    EXPECT_TRUE(analyzeVersion(
        Graphics::GpuGraphResourceType::AccelStruct,
        Name("tests/task_graph_resource_version/whole_accel_struct"),
        Graphics::GpuTaskResourceRange{},
        status
    ));
    EXPECT_TRUE(analyzeVersion(
        Graphics::GpuGraphResourceType::HazardDomain,
        Name("tests/task_graph_resource_version/whole_hazard_domain"),
        Graphics::GpuTaskResourceRange{},
        status
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

