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


TEST(GpuTaskGraph, AcceptsEveryDefinedQueueSharingMaskForMetadataResources){
    constexpr Graphics::ResourceQueueSharing::Mask s_ValidQueueSharingMasks[] = {
        Graphics::ResourceQueueSharing::Exclusive,
        Graphics::ResourceQueueSharing::Graphics,
        Graphics::ResourceQueueSharing::AsyncCompute,
        Graphics::ResourceQueueSharing::Transfer,
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        Graphics::ResourceQueueSharing::GraphicsAndTransfer,
        Graphics::ResourceQueueSharing::AsyncComputeAndTransfer,
        Graphics::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer,
    };
    constexpr Graphics::GpuGraphResourceType::Enum s_MetadataResourceTypes[] = {
        Graphics::GpuGraphResourceType::Texture,
        Graphics::GpuGraphResourceType::Buffer,
        Graphics::GpuGraphResourceType::AccelStruct,
        Graphics::GpuGraphResourceType::HazardDomain,
    };

    for(const Graphics::GpuGraphResourceType::Enum resourceType : s_MetadataResourceTypes){
        for(const Graphics::ResourceQueueSharing::Mask queueSharing : s_ValidQueueSharingMasks){
            SCOPED_TRACE(static_cast<u32>(resourceType));
            SCOPED_TRACE(static_cast<u32>(queueSharing));
            TestArena testArena;
            Graphics::GpuTaskGraph graph(testArena.arena);
            u64 declarationRevision = 0u;
            {
                const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
                declarationRevision = declarations.declarationRevision();
            }
            const Graphics::GpuGraphResourceId resource = graph.importResource(
                Graphics::GpuGraphResourceDesc{}
                    .setIdentity(Name("tests/task_graph/valid_queue_sharing_metadata"))
                    .setMarkerLabel("Valid Queue Sharing Metadata")
                    .setType(resourceType)
                    .setInitialState(
                        resourceType == Graphics::GpuGraphResourceType::HazardDomain
                            ? Graphics::ResourceStates::Unknown
                            : Graphics::ResourceStates::Common
                    )
                    .setQueueSharing(queueSharing)
            );
            ASSERT_TRUE(resource.valid());
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_EQ(declarations.resourceCount(), 1u);
            EXPECT_NE(declarations.declarationRevision(), declarationRevision);
            EXPECT_EQ(declarations.resourceAt(resource.index).queueSharing, queueSharing);
        }
    }
}

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

TEST(GpuTaskGraph, CompilerOwnershipTransferDefenseRejectsMalformedSharingBeforeSameFamilyNoOp){
    TestArena testArena;
    const TestPath repoRoot = TestPath(testArena.arena, __FILE__)
        .parent_path()
        .parent_path()
        .parent_path()
        .parent_path()
        .lexically_normal()
    ;
    TestAString compilerSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "task_graph" / "compiler.cpp", compilerSource));
    const AStringView source(compilerSource.data(), compilerSource.size());
    const usize functionBegin = source.find("[[nodiscard]] bool AppendCompiledOwnershipTransfer(");
    const usize functionEnd = source.find("////////////////////////////////////////////////////////////////", functionBegin);
    ASSERT_NE(functionBegin, AStringView::npos);
    ASSERT_NE(functionEnd, AStringView::npos);
    ASSERT_LT(functionBegin, functionEnd);

    const usize invalidSharingGuard = source.find(
        "|| !ResourceQueueSharing::IsValid(resource.queueSharing)",
        functionBegin
    );
    const usize invalidSharingRejection = source.find("return false;", invalidSharingGuard);
    const usize declaredResourceLookup = source.find(
        "const GpuTaskGraphResourceView declaredResource",
        invalidSharingGuard
    );
    const usize sameFamilyNoOp = source.find(
        "if(sourceQueueInfo->familyIndex == destinationQueueInfo->familyIndex)",
        declaredResourceLookup
    );
    ASSERT_NE(invalidSharingGuard, AStringView::npos);
    ASSERT_NE(invalidSharingRejection, AStringView::npos);
    ASSERT_NE(declaredResourceLookup, AStringView::npos);
    ASSERT_NE(sameFamilyNoOp, AStringView::npos);
    EXPECT_LT(invalidSharingGuard, invalidSharingRejection);
    EXPECT_LT(invalidSharingRejection, declaredResourceLookup);
    EXPECT_LT(declaredResourceLookup, sameFamilyNoOp);
    EXPECT_LT(sameFamilyNoOp, functionEnd);
}

TEST(GpuTaskGraph, TypedImportsInheritAndValidateImmutableNativeQueueSharing){
    constexpr Graphics::ResourceQueueSharing::Mask s_NativeQueueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute;
    constexpr Graphics::ResourceQueueSharing::Mask s_MismatchedQueueSharing =
        Graphics::ResourceQueueSharing::GraphicsAndTransfer;

    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);

    Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        Graphics::TextureDesc().setQueueSharing(s_NativeQueueSharing)
    );
    Graphics::Buffer* const bufferObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        Graphics::BufferDesc().setQueueSharing(s_NativeQueueSharing)
    );
    Graphics::Buffer* const mismatchedAccelStructBackingObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        Graphics::BufferDesc().setQueueSharing(s_MismatchedQueueSharing)
    );
    Graphics::RayTracingAccelStruct* const accelStructObject = NewArenaObject<Graphics::RayTracingAccelStruct>(
        testArena.arena,
        context,
        s_NativeQueueSharing
    );
    ASSERT_NE(textureObject, nullptr);
    ASSERT_NE(bufferObject, nullptr);
    ASSERT_NE(mismatchedAccelStructBackingObject, nullptr);
    ASSERT_NE(accelStructObject, nullptr);

    Graphics::TextureHandle texture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle buffer(
        bufferObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle mismatchedAccelStructBacking(
        mismatchedAccelStructBackingObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::RayTracingAccelStructHandle accelStruct(
        accelStructObject,
        Graphics::RayTracingAccelStructHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferDesc& bufferDesc = const_cast<Graphics::BufferDesc&>(buffer->getDescription());
    Graphics::RayTracingAccelStructDesc& accelStructDesc = const_cast<Graphics::RayTracingAccelStructDesc&>(
        accelStruct->getDescription()
    );

    Graphics::TextureDesc& textureDesc = const_cast<Graphics::TextureDesc&>(texture->getDescription());
    textureDesc.width += 1u;
    EXPECT_FALSE(texture->descriptionMatchesCreation());
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        u64 declarationRevision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            declarationRevision = declarations.declarationRevision();
        }
        EXPECT_FALSE(graph.importTexture(
            texture,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/native_queue_sharing_texture_drift"))
                .setMarkerLabel("Native Queue Sharing Texture Drift")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        ).valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceCount(), 0u);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
    }
    textureDesc = texture->getCreationDescription();
    EXPECT_TRUE(texture->descriptionMatchesCreation());

    bufferDesc.queueSharing = s_MismatchedQueueSharing;
    EXPECT_FALSE(buffer->descriptionMatchesCreation());
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        u64 declarationRevision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            declarationRevision = declarations.declarationRevision();
        }
        EXPECT_FALSE(graph.importBuffer(
            buffer,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/native_queue_sharing_buffer_drift"))
                .setMarkerLabel("Native Queue Sharing Buffer Drift")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setInitialState(Graphics::ResourceStates::Common)
        ).valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceCount(), 0u);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
    }
    bufferDesc = buffer->getCreationDescription();
    EXPECT_TRUE(buffer->descriptionMatchesCreation());

    EXPECT_EQ(accelStruct->getCreationQueueSharing(), s_NativeQueueSharing);
    EXPECT_TRUE(accelStruct->queueSharingMatchesCreation());
    accelStructDesc.queueSharing = s_MismatchedQueueSharing;
    EXPECT_FALSE(accelStruct->queueSharingMatchesCreation());
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        u64 declarationRevision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            declarationRevision = declarations.declarationRevision();
        }
        EXPECT_FALSE(graph.importAccelStruct(
            accelStruct,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/native_queue_sharing_accel_struct_drift"))
                .setMarkerLabel("Native Queue Sharing Accel Struct Drift")
                .setType(Graphics::GpuGraphResourceType::AccelStruct)
                .setInitialState(Graphics::ResourceStates::Common)
        ).valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceCount(), 0u);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
    }
    accelStructDesc.queueSharing = accelStruct->getCreationQueueSharing();
    EXPECT_TRUE(accelStruct->queueSharingMatchesCreation());

    Graphics::BufferHandle& accelStructBacking = const_cast<Graphics::BufferHandle&>(
        accelStruct->getBackingBufferHandle()
    );
    accelStructBacking = mismatchedAccelStructBacking;
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        u64 declarationRevision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            declarationRevision = declarations.declarationRevision();
        }
        EXPECT_FALSE(graph.importAccelStruct(
            accelStruct,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/native_queue_sharing_accel_struct_backing_mismatch"))
                .setMarkerLabel("Native Queue Sharing Accel Struct Backing Mismatch")
                .setType(Graphics::GpuGraphResourceType::AccelStruct)
                .setInitialState(Graphics::ResourceStates::Common)
        ).valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceCount(), 0u);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
    }
    accelStructBacking = buffer;

    const auto expectQueueSharingContract = [&](
        const auto& import,
        const Graphics::GpuGraphResourceType::Enum type,
        const Name& identity,
        const AStringView markerLabel
    ){
        {
            Graphics::GpuTaskGraph graph(testArena.arena);
            const Graphics::GpuGraphResourceId resource = import(
                graph,
                Graphics::GpuGraphResourceDesc{}
                    .setIdentity(identity)
                    .setMarkerLabel(markerLabel)
                    .setType(type)
                    .setInitialState(Graphics::ResourceStates::Common)
            );
            ASSERT_TRUE(resource.valid());
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            const Graphics::GpuTaskGraphResourceView resourceView = declarations.resourceAt(resource.index);
            EXPECT_EQ(resourceView.queueSharing, s_NativeQueueSharing);
            ASSERT_TRUE(resourceView.hasQueueAdmission);
            ASSERT_TRUE(resourceView.queueAdmission.valid());
            EXPECT_EQ(resourceView.queueAdmission.admittedQueueClasses, s_NativeQueueSharing);
            EXPECT_FALSE(resourceView.queueAdmission.usesConcurrentSharing);
            EXPECT_EQ(resourceView.queueAdmission.queueFamilyIndexCount, 0u);
            EXPECT_EQ(resourceView.queueAdmission.queueFamilyIndices, nullptr);
        }

        {
            Graphics::GpuTaskGraph graph(testArena.arena);
            const Graphics::GpuGraphResourceId resource = import(
                graph,
                Graphics::GpuGraphResourceDesc{}
                    .setIdentity(identity)
                    .setMarkerLabel(markerLabel)
                    .setType(type)
                    .setInitialState(Graphics::ResourceStates::Common)
                    .setQueueSharing(s_NativeQueueSharing)
            );
            ASSERT_TRUE(resource.valid());
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_EQ(declarations.resourceAt(resource.index).queueSharing, s_NativeQueueSharing);
        }

        {
            Graphics::GpuTaskGraph graph(testArena.arena);
            EXPECT_FALSE(import(
                graph,
                Graphics::GpuGraphResourceDesc{}
                    .setIdentity(identity)
                    .setMarkerLabel(markerLabel)
                    .setType(type)
                    .setInitialState(Graphics::ResourceStates::Common)
                    .setQueueSharing(s_MismatchedQueueSharing)
            ).valid());
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_EQ(declarations.resourceCount(), 0u);
        }
    };

    expectQueueSharingContract(
        [&](Graphics::GpuTaskGraph& graph, const Graphics::GpuGraphResourceDesc& desc){
            return graph.importTexture(texture, desc);
        },
        Graphics::GpuGraphResourceType::Texture,
        Name("tests/task_graph/native_queue_sharing_texture"),
        "Native Queue Sharing Texture"
    );
    expectQueueSharingContract(
        [&](Graphics::GpuTaskGraph& graph, const Graphics::GpuGraphResourceDesc& desc){
            return graph.importBuffer(buffer, desc);
        },
        Graphics::GpuGraphResourceType::Buffer,
        Name("tests/task_graph/native_queue_sharing_buffer"),
        "Native Queue Sharing Buffer"
    );
    expectQueueSharingContract(
        [&](Graphics::GpuTaskGraph& graph, const Graphics::GpuGraphResourceDesc& desc){
            return graph.importAccelStruct(accelStruct, desc);
        },
        Graphics::GpuGraphResourceType::AccelStruct,
        Name("tests/task_graph/native_queue_sharing_accel_struct"),
        "Native Queue Sharing Accel Struct"
    );
}

TEST(GpuTaskGraph, TypedConcurrentResourceAdmissionConstrainsCompilationAndOwnership){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    const u32 queueFamilyIndices[] = {
        queues[0u].familyIndex,
        queues[1u].familyIndex,
    };
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
    imageInfo.queueFamilyIndexCount = static_cast<u32>(LengthOf(queueFamilyIndices));
    imageInfo.pQueueFamilyIndices = queueFamilyIndices;

    const Graphics::TextureDesc textureDesc = Graphics::TextureDesc()
        .setInitialState(Graphics::ResourceStates::Common)
        .setQueueSharing(Graphics::ResourceQueueSharing::Graphics)
    ;
    Graphics::Texture* const textureObject = NewArenaObject<Graphics::Texture>(
        testArena.arena,
        context,
        allocator,
        textureDesc,
        imageInfo,
        false
    );
    ASSERT_NE(textureObject, nullptr);
    Graphics::TextureHandle texture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    const Graphics::BufferDesc bufferDesc = Graphics::BufferDesc()
        .setByteSize(64u)
        .setInitialState(Graphics::ResourceStates::Common)
        .setQueueSharing(Graphics::ResourceQueueSharing::Graphics)
    ;
    Graphics::Buffer* const bufferObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        bufferDesc,
        false,
        VK_SHARING_MODE_CONCURRENT,
        static_cast<u32>(LengthOf(queueFamilyIndices)),
        queueFamilyIndices
    );
    Graphics::RayTracingAccelStruct* const accelStructObject = NewArenaObject<Graphics::RayTracingAccelStruct>(
        testArena.arena,
        context,
        Graphics::ResourceQueueSharing::Graphics
    );
    ASSERT_NE(bufferObject, nullptr);
    ASSERT_NE(accelStructObject, nullptr);
    Graphics::BufferHandle buffer(
        bufferObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::RayTracingAccelStructHandle accelStruct(
        accelStructObject,
        Graphics::RayTracingAccelStructHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle& accelStructBacking = const_cast<Graphics::BufferHandle&>(
        accelStruct->getBackingBufferHandle()
    );
    accelStructBacking = buffer;

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importTexture(
            texture,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/typed_concurrent_admission"))
                .setMarkerLabel("Typed Concurrent Admission")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        ASSERT_TRUE(resource.valid());
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            const Graphics::GpuTaskGraphResourceView resourceView = declarations.resourceAt(resource.index);
            const Graphics::ResourceQueueAdmissionSnapshot textureAdmission = texture->getQueueAdmissionSnapshot();
            ASSERT_TRUE(resourceView.hasQueueAdmission);
            ASSERT_TRUE(resourceView.queueAdmission.valid());
            EXPECT_TRUE(resourceView.queueAdmission.usesConcurrentSharing);
            EXPECT_EQ(
                resourceView.queueAdmission.admittedQueueClasses,
                Graphics::ResourceQueueSharing::Graphics
            );
            ASSERT_EQ(resourceView.queueAdmission.queueFamilyIndexCount, LengthOf(queueFamilyIndices));
            EXPECT_NE(resourceView.queueAdmission.queueFamilyIndices, textureAdmission.queueFamilyIndices);
            EXPECT_EQ(resourceView.queueAdmission.queueFamilyIndices[0u], queues[0u].familyIndex);
            EXPECT_EQ(resourceView.queueAdmission.queueFamilyIndices[1u], queues[1u].familyIndex);
        }

        const Graphics::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        Graphics::GpuTaskDesc taskDesc;
        taskDesc
            .setIdentity(Name("tests/task_graph/typed_concurrent_unadmitted_compute"))
            .setMarkerLabel("Typed Concurrent Unadmitted Compute")
            .setQueue(Graphics::GpuQueueRequest{
                Graphics::GpuQueueCapability::Compute,
                Graphics::GpuQueuePreference::Compute,
                false,
                false,
            })
            .setResourceUses(&use, 1u)
        ;
        const Graphics::GpuTaskId task = graph.addTask(taskDesc);
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        EXPECT_FALSE(Assign(graph, analysis, topology, assignments));
        EXPECT_EQ(
            assignments.diagnostic().status,
            Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue
        );
        EXPECT_EQ(assignments.diagnostic().task, task);
    }

    const auto expectUnadmittedComputeRejected = [&](
        const auto& import,
        const Graphics::GpuGraphResourceType::Enum type,
        const Graphics::ResourceStates::Mask requiredState,
        const Graphics::ResourceQueueAdmissionSnapshot& sourceAdmission,
        const Name& resourceIdentity,
        const AStringView resourceMarkerLabel,
        const Name& taskIdentity,
        const AStringView taskMarkerLabel
    ){
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = import(
            graph,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(resourceIdentity)
                .setMarkerLabel(resourceMarkerLabel)
                .setType(type)
                .setInitialState(Graphics::ResourceStates::Common)
        );
        ASSERT_TRUE(resource.valid());
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            const Graphics::GpuTaskGraphResourceView resourceView = declarations.resourceAt(resource.index);
            ASSERT_TRUE(resourceView.hasQueueAdmission);
            ASSERT_TRUE(resourceView.queueAdmission.valid());
            EXPECT_TRUE(resourceView.queueAdmission.usesConcurrentSharing);
            EXPECT_EQ(resourceView.queueAdmission.admittedQueueClasses, Graphics::ResourceQueueSharing::Graphics);
            ASSERT_EQ(resourceView.queueAdmission.queueFamilyIndexCount, LengthOf(queueFamilyIndices));
            EXPECT_NE(resourceView.queueAdmission.queueFamilyIndices, sourceAdmission.queueFamilyIndices);
            EXPECT_EQ(resourceView.queueAdmission.queueFamilyIndices[0u], queues[0u].familyIndex);
            EXPECT_EQ(resourceView.queueAdmission.queueFamilyIndices[1u], queues[1u].familyIndex);
        }

        const Graphics::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = requiredState,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        Graphics::GpuTaskDesc taskDesc;
        taskDesc
            .setIdentity(taskIdentity)
            .setMarkerLabel(taskMarkerLabel)
            .setQueue(Graphics::GpuQueueRequest{
                Graphics::GpuQueueCapability::Compute,
                Graphics::GpuQueuePreference::Compute,
                false,
                false,
            })
            .setResourceUses(&use, 1u)
        ;
        const Graphics::GpuTaskId task = graph.addTask(taskDesc);
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        EXPECT_FALSE(Assign(graph, analysis, topology, assignments));
        EXPECT_EQ(
            assignments.diagnostic().status,
            Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue
        );
        EXPECT_EQ(assignments.diagnostic().task, task);
    };

    expectUnadmittedComputeRejected(
        [&](Graphics::GpuTaskGraph& graph, const Graphics::GpuGraphResourceDesc& desc){
            return graph.importBuffer(buffer, desc);
        },
        Graphics::GpuGraphResourceType::Buffer,
        Graphics::ResourceStates::ShaderResource,
        buffer->getQueueAdmissionSnapshot(),
        Name("tests/task_graph/typed_concurrent_buffer_admission"),
        "Typed Concurrent Buffer Admission",
        Name("tests/task_graph/typed_concurrent_buffer_unadmitted_compute"),
        "Typed Concurrent Buffer Unadmitted Compute"
    );
    expectUnadmittedComputeRejected(
        [&](Graphics::GpuTaskGraph& graph, const Graphics::GpuGraphResourceDesc& desc){
            return graph.importAccelStruct(accelStruct, desc);
        },
        Graphics::GpuGraphResourceType::AccelStruct,
        Graphics::ResourceStates::AccelStructRead,
        buffer->getQueueAdmissionSnapshot(),
        Name("tests/task_graph/typed_concurrent_accel_struct_admission"),
        "Typed Concurrent Accel Struct Admission",
        Name("tests/task_graph/typed_concurrent_accel_struct_unadmitted_compute"),
        "Typed Concurrent Accel Struct Unadmitted Compute"
    );

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId resource = graph.importTexture(
            texture,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/typed_concurrent_initial_owner"))
                .setMarkerLabel("Typed Concurrent Initial Owner")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
                .setInitialOwnerQueue(queues[0u].id)
        );
        ASSERT_TRUE(resource.valid());
        const Graphics::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        };
        Graphics::GpuTaskDesc taskDesc;
        taskDesc
            .setIdentity(Name("tests/task_graph/typed_concurrent_owned_graphics"))
            .setMarkerLabel("Typed Concurrent Owned Graphics")
            .setQueue(Graphics::GpuQueueRequest{
                Graphics::GpuQueueCapability::Graphics,
                Graphics::GpuQueuePreference::Graphics,
                false,
                false,
            })
            .setResourceUses(&use, 1u)
        ;
        ASSERT_TRUE(graph.addTask(taskDesc).valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        EXPECT_FALSE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);

        EXPECT_FALSE(compiledPlan.valid());
        EXPECT_EQ(compiledPlan.logicalOwnershipTransferCount(), 0u);
    }
}

TEST(GpuTaskGraph, TypedImportsRejectMalformedInheritedNativeQueueSharing){
    constexpr u8 s_UnknownQueueSharingBit = 1u << 7u;
    constexpr Graphics::ResourceQueueSharing::Mask s_InvalidQueueSharingMasks[] = {
        static_cast<Graphics::ResourceQueueSharing::Mask>(s_UnknownQueueSharingBit),
        static_cast<Graphics::ResourceQueueSharing::Mask>(
            static_cast<u8>(Graphics::ResourceQueueSharing::Graphics) | s_UnknownQueueSharingBit
        ),
    };

    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);

    for(const Graphics::ResourceQueueSharing::Mask queueSharing : s_InvalidQueueSharingMasks){
        SCOPED_TRACE(static_cast<u32>(queueSharing));
        Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
            testArena.arena,
            context,
            allocator,
            Graphics::TextureDesc().setQueueSharing(queueSharing)
        );
        Graphics::Buffer* const bufferObject = NewMetadataOnlyBuffer(
            testArena.arena,
            context,
            allocator,
            Graphics::BufferDesc().setQueueSharing(queueSharing)
        );
        Graphics::RayTracingAccelStruct* const accelStructObject = NewArenaObject<Graphics::RayTracingAccelStruct>(
            testArena.arena,
            context,
            queueSharing
        );
        ASSERT_NE(textureObject, nullptr);
        ASSERT_NE(bufferObject, nullptr);
        ASSERT_NE(accelStructObject, nullptr);

        Graphics::TextureHandle texture(
            textureObject,
            Graphics::TextureHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        Graphics::BufferHandle buffer(
            bufferObject,
            Graphics::BufferHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        Graphics::RayTracingAccelStructHandle accelStruct(
            accelStructObject,
            Graphics::RayTracingAccelStructHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        ASSERT_TRUE(texture->descriptionMatchesCreation());
        ASSERT_TRUE(buffer->descriptionMatchesCreation());
        ASSERT_TRUE(accelStruct->queueSharingMatchesCreation());
        ASSERT_EQ(texture->getCreationDescription().queueSharing, queueSharing);
        ASSERT_EQ(buffer->getCreationDescription().queueSharing, queueSharing);
        ASSERT_EQ(accelStruct->getCreationQueueSharing(), queueSharing);

        Graphics::GpuTaskGraph graph(testArena.arena);
        usize resourceCount = 0u;
        u64 declarationRevision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            resourceCount = declarations.resourceCount();
            declarationRevision = declarations.declarationRevision();
        }
        const ArenaMemoryStats memoryStats = testArena.arena.memoryStats();
        EXPECT_FALSE(graph.importTexture(
            texture,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/invalid_inherited_queue_sharing_texture"))
                .setMarkerLabel("Invalid Inherited Queue Sharing Texture")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        ).valid());
        EXPECT_FALSE(graph.importBuffer(
            buffer,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/invalid_inherited_queue_sharing_buffer"))
                .setMarkerLabel("Invalid Inherited Queue Sharing Buffer")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setInitialState(Graphics::ResourceStates::Common)
        ).valid());
        EXPECT_FALSE(graph.importAccelStruct(
            accelStruct,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/invalid_inherited_queue_sharing_accel_struct"))
                .setMarkerLabel("Invalid Inherited Queue Sharing Accel Struct")
                .setType(Graphics::GpuGraphResourceType::AccelStruct)
                .setInitialState(Graphics::ResourceStates::Common)
        ).valid());
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceCount(), resourceCount);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
        ExpectMemoryStatsEqual(memoryStats, testArena.arena.memoryStats());
    }
}

TEST(GpuTaskGraph, TypedImportsValidateRetainedExternalFinalState){
    constexpr Graphics::ResourceStates::Mask s_NativeInitialState = Graphics::ResourceStates::Common;
    const Graphics::GpuPhysicalQueueId releaseDestinationQueue = GraphicsQueue().id;

    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);

    const Graphics::TextureDesc retainedTextureDesc = Graphics::TextureDesc()
        .setInitialState(s_NativeInitialState)
        .setKeepInitialState(true)
    ;
    const Graphics::TextureDesc nonRetainedTextureDesc = Graphics::TextureDesc()
        .setInitialState(s_NativeInitialState)
        .setKeepInitialState(false)
    ;
    const Graphics::BufferDesc retainedBufferDesc = Graphics::BufferDesc()
        .setInitialState(s_NativeInitialState)
        .setKeepInitialState(true)
    ;
    const Graphics::BufferDesc nonRetainedBufferDesc = Graphics::BufferDesc()
        .setInitialState(s_NativeInitialState)
        .setKeepInitialState(false)
    ;
    Graphics::Texture* const retainedTextureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        retainedTextureDesc
    );
    Graphics::Texture* const nonRetainedTextureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        nonRetainedTextureDesc
    );
    Graphics::Buffer* const retainedBufferObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        retainedBufferDesc
    );
    Graphics::Buffer* const nonRetainedBufferObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        nonRetainedBufferDesc
    );
    Graphics::Buffer* const retainedAccelStructBackingObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        retainedBufferDesc
    );
    Graphics::Buffer* const nonRetainedAccelStructBackingObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        nonRetainedBufferDesc
    );
    Graphics::RayTracingAccelStruct* const accelStructObject = NewArenaObject<Graphics::RayTracingAccelStruct>(
        testArena.arena,
        context
    );
    ASSERT_NE(retainedTextureObject, nullptr);
    ASSERT_NE(nonRetainedTextureObject, nullptr);
    ASSERT_NE(retainedBufferObject, nullptr);
    ASSERT_NE(nonRetainedBufferObject, nullptr);
    ASSERT_NE(retainedAccelStructBackingObject, nullptr);
    ASSERT_NE(nonRetainedAccelStructBackingObject, nullptr);
    ASSERT_NE(accelStructObject, nullptr);

    Graphics::TextureHandle retainedTexture(
        retainedTextureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::TextureHandle nonRetainedTexture(
        nonRetainedTextureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::TextureHandle* activeTexture = &retainedTexture;
    Graphics::BufferHandle retainedBuffer(
        retainedBufferObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle nonRetainedBuffer(
        nonRetainedBufferObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle* activeBuffer = &retainedBuffer;
    Graphics::BufferHandle retainedAccelStructBacking(
        retainedAccelStructBackingObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle nonRetainedAccelStructBacking(
        nonRetainedAccelStructBackingObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::RayTracingAccelStructHandle accelStruct(
        accelStructObject,
        Graphics::RayTracingAccelStructHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle& accelStructBackingHandle = const_cast<Graphics::BufferHandle&>(
        accelStruct->getBackingBufferHandle()
    );
    accelStructBackingHandle = retainedAccelStructBacking;

    const auto expectExternalFinalContract = [&](
        const auto& import,
        const auto& setKeepInitialState,
        const Graphics::GpuGraphResourceType::Enum type,
        const Name& identity,
        const AStringView markerLabel,
        const Graphics::ResourceStates::Mask differingFinalState
    ){
        setKeepInitialState(true);
        {
            Graphics::GpuTaskGraph graph(testArena.arena);
            u64 declarationRevision = 0u;
            {
                const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
                declarationRevision = declarations.declarationRevision();
            }
            EXPECT_FALSE(import(
                graph,
                Graphics::GpuGraphResourceDesc{}
                    .setIdentity(identity)
                    .setMarkerLabel(markerLabel)
                    .setType(type)
                    .setInitialState(s_NativeInitialState)
                    .setExternalFinalState(differingFinalState)
                    .setExternalFinalReleaseDestinationQueue(releaseDestinationQueue)
            ).valid());
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_EQ(declarations.resourceCount(), 0u);
            EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
        }

        {
            Graphics::GpuTaskGraph graph(testArena.arena);
            const Graphics::GpuGraphResourceId resource = import(
                graph,
                Graphics::GpuGraphResourceDesc{}
                    .setIdentity(identity)
                    .setMarkerLabel(markerLabel)
                    .setType(type)
                    .setInitialState(s_NativeInitialState)
                    .setExternalFinalState(s_NativeInitialState)
                    .setExternalFinalReleaseDestinationQueue(releaseDestinationQueue)
            );
            ASSERT_TRUE(resource.valid());
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_EQ(declarations.resourceAt(resource.index).externalFinalState, s_NativeInitialState);
            EXPECT_EQ(
                declarations.resourceAt(resource.index).externalFinalReleaseDestinationQueue,
                releaseDestinationQueue
            );
        }

        {
            Graphics::GpuTaskGraph graph(testArena.arena);
            EXPECT_TRUE(import(
                graph,
                Graphics::GpuGraphResourceDesc{}
                    .setIdentity(identity)
                    .setMarkerLabel(markerLabel)
                    .setType(type)
                    .setInitialState(s_NativeInitialState)
                    .setExternalFinalState(Graphics::ResourceStates::Unknown)
            ).valid());
        }

        setKeepInitialState(false);
        {
            Graphics::GpuTaskGraph graph(testArena.arena);
            const Graphics::GpuGraphResourceId resource = import(
                graph,
                Graphics::GpuGraphResourceDesc{}
                    .setIdentity(identity)
                    .setMarkerLabel(markerLabel)
                    .setType(type)
                    .setInitialState(s_NativeInitialState)
                    .setExternalFinalState(differingFinalState)
                    .setExternalFinalReleaseDestinationQueue(releaseDestinationQueue)
            );
            ASSERT_TRUE(resource.valid());
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_EQ(declarations.resourceAt(resource.index).externalFinalState, differingFinalState);
        }
        setKeepInitialState(true);
    };

    expectExternalFinalContract(
        [&](Graphics::GpuTaskGraph& graph, const Graphics::GpuGraphResourceDesc& desc){
            return graph.importTexture(*activeTexture, desc);
        },
        [&](const bool keepInitialState){
            activeTexture = keepInitialState ? &retainedTexture : &nonRetainedTexture;
        },
        Graphics::GpuGraphResourceType::Texture,
        Name("tests/task_graph/retained_external_final_texture"),
        "Retained External Final Texture",
        Graphics::ResourceStates::ShaderResource
    );
    expectExternalFinalContract(
        [&](Graphics::GpuTaskGraph& graph, const Graphics::GpuGraphResourceDesc& desc){
            return graph.importBuffer(*activeBuffer, desc);
        },
        [&](const bool keepInitialState){
            activeBuffer = keepInitialState ? &retainedBuffer : &nonRetainedBuffer;
        },
        Graphics::GpuGraphResourceType::Buffer,
        Name("tests/task_graph/retained_external_final_buffer"),
        "Retained External Final Buffer",
        Graphics::ResourceStates::ShaderResource
    );
    expectExternalFinalContract(
        [&](Graphics::GpuTaskGraph& graph, const Graphics::GpuGraphResourceDesc& desc){
            return graph.importAccelStruct(accelStruct, desc);
        },
        [&](const bool keepInitialState){
            accelStructBackingHandle = keepInitialState
                ? retainedAccelStructBacking
                : nonRetainedAccelStructBacking
            ;
        },
        Graphics::GpuGraphResourceType::AccelStruct,
        Name("tests/task_graph/retained_external_final_accel_struct"),
        "Retained External Final Accel Struct",
        Graphics::ResourceStates::AccelStructRead
    );

    Graphics::GpuTaskGraph metadataOnlyGraph(testArena.arena);
    EXPECT_TRUE(metadataOnlyGraph.importResource(
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/metadata_only_external_final"))
            .setMarkerLabel("Metadata Only External Final")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(s_NativeInitialState)
            .setExternalFinalState(Graphics::ResourceStates::ShaderResource)
            .setExternalFinalReleaseDestinationQueue(releaseDestinationQueue)
    ).valid());
}

TEST(GpuTaskGraph, RejectsTypedImportsFromMismatchedDeviceGeneration){
    constexpr u16 s_SourceDeviceGeneration = 7u;
    constexpr u16 s_TargetDeviceGeneration = 8u;

    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext sourceContext(
        graphicsAllocator,
        cpuScheduler,
        s_SourceDeviceGeneration
    );
    Graphics::GraphicsBackend::VulkanAllocator sourceAllocator(sourceContext);

    Graphics::Buffer* const bufferObject = NewMetadataOnlyBuffer(
        testArena.arena,
        sourceContext,
        sourceAllocator,
        Graphics::BufferDesc{}
    );
    ASSERT_NE(bufferObject, nullptr);
    Graphics::BufferHandle buffer(
        bufferObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );

    Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
        testArena.arena,
        sourceContext,
        sourceAllocator,
        Graphics::TextureDesc{}
    );
    ASSERT_NE(textureObject, nullptr);
    Graphics::TextureHandle texture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );

    Graphics::RayTracingAccelStruct* const accelStructObject = NewArenaObject<Graphics::RayTracingAccelStruct>(
        testArena.arena,
        sourceContext
    );
    ASSERT_NE(accelStructObject, nullptr);
    Graphics::RayTracingAccelStructHandle accelStruct(
        accelStructObject,
        Graphics::RayTracingAccelStructHandle::deleter_type(&testArena.arena),
        AdoptRef
    );

    Graphics::GraphicsPipeline* const graphicsPipelineObject = NewArenaObject<Graphics::GraphicsPipeline>(
        testArena.arena,
        sourceContext
    );
    ASSERT_NE(graphicsPipelineObject, nullptr);
    Graphics::GraphicsPipelineHandle graphicsPipeline(
        graphicsPipelineObject,
        Graphics::GraphicsPipelineHandle::deleter_type(&testArena.arena),
        AdoptRef
    );

    Graphics::ComputePipeline* const computePipelineObject = NewArenaObject<Graphics::ComputePipeline>(
        testArena.arena,
        sourceContext
    );
    ASSERT_NE(computePipelineObject, nullptr);
    Graphics::ComputePipelineHandle computePipeline(
        computePipelineObject,
        Graphics::ComputePipelineHandle::deleter_type(&testArena.arena),
        AdoptRef
    );

    Graphics::MeshletPipeline* const meshletPipelineObject = NewArenaObject<Graphics::MeshletPipeline>(
        testArena.arena,
        sourceContext
    );
    ASSERT_NE(meshletPipelineObject, nullptr);
    Graphics::MeshletPipelineHandle meshletPipeline(
        meshletPipelineObject,
        Graphics::MeshletPipelineHandle::deleter_type(&testArena.arena),
        AdoptRef
    );

    Graphics::GpuPhysicalQueueInfo sourceQueue = GraphicsQueue();
    sourceQueue.id.deviceGeneration = s_SourceDeviceGeneration;
    const Graphics::GpuTaskGraphQueueTopology sourceTopology{
        .queues = &sourceQueue,
        .queueCount = 1u,
    };
    Graphics::GpuPhysicalQueueInfo targetQueue = GraphicsQueue();
    targetQueue.id.deviceGeneration = s_TargetDeviceGeneration;
    const Graphics::GpuTaskGraphQueueTopology targetTopology{
        .queues = &targetQueue,
        .queueCount = 1u,
    };

    const auto expectTypedImportMismatch = [&](const auto& import){
        Graphics::GpuTaskGraph graph(testArena.arena);
        ASSERT_TRUE(import(graph).valid());
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_TRUE(declarations.validForDeviceGeneration(s_SourceDeviceGeneration));
            EXPECT_FALSE(declarations.validForDeviceGeneration(s_TargetDeviceGeneration));
        }
        ASSERT_TRUE(AddTask(
            graph,
            Name("tests/task_graph/device_generation_task"),
            "Device Generation Task"
        ).valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        ASSERT_TRUE(Compile(graph, analysis, sourceTopology, assignments, compiledGraph));
        {
            const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_TRUE(compiledPlan.valid());
        }
        EXPECT_FALSE(Compile(graph, analysis, targetTopology, assignments, compiledGraph));
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        EXPECT_FALSE(compiledPlan.valid());
    };

    expectTypedImportMismatch([&](Graphics::GpuTaskGraph& graph){
        return graph.importBuffer(
            buffer,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/device_generation_buffer"))
                .setMarkerLabel("Device Generation Buffer")
                .setType(Graphics::GpuGraphResourceType::Buffer)
                .setInitialState(Graphics::ResourceStates::Common)
        );
    });
    expectTypedImportMismatch([&](Graphics::GpuTaskGraph& graph){
        return graph.importTexture(
            texture,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/device_generation_texture"))
                .setMarkerLabel("Device Generation Texture")
                .setType(Graphics::GpuGraphResourceType::Texture)
                .setInitialState(Graphics::ResourceStates::Common)
        );
    });
    expectTypedImportMismatch([&](Graphics::GpuTaskGraph& graph){
        return graph.importAccelStruct(
            accelStruct,
            Graphics::GpuGraphResourceDesc{}
                .setIdentity(Name("tests/task_graph/device_generation_accel_struct"))
                .setMarkerLabel("Device Generation Accel Struct")
                .setType(Graphics::GpuGraphResourceType::AccelStruct)
                .setInitialState(Graphics::ResourceStates::Common)
        );
    });
    expectTypedImportMismatch([&](Graphics::GpuTaskGraph& graph){
        return graph.importGraphicsPipeline(
            graphicsPipeline,
            Graphics::GpuGraphPipelineDesc{}
                .setIdentity(Name("tests/task_graph/device_generation_graphics_pipeline"))
                .setMarkerLabel("Device Generation Graphics Pipeline")
                .setType(Graphics::GpuGraphPipelineType::Graphics)
        );
    });
    expectTypedImportMismatch([&](Graphics::GpuTaskGraph& graph){
        return graph.importComputePipeline(
            computePipeline,
            Graphics::GpuGraphPipelineDesc{}
                .setIdentity(Name("tests/task_graph/device_generation_compute_pipeline"))
                .setMarkerLabel("Device Generation Compute Pipeline")
                .setType(Graphics::GpuGraphPipelineType::Compute)
        );
    });
    expectTypedImportMismatch([&](Graphics::GpuTaskGraph& graph){
        return graph.importMeshletPipeline(
            meshletPipeline,
            Graphics::GpuGraphPipelineDesc{}
                .setIdentity(Name("tests/task_graph/device_generation_meshlet_pipeline"))
                .setMarkerLabel("Device Generation Meshlet Pipeline")
                .setType(Graphics::GpuGraphPipelineType::Meshlet)
        );
    });

    Graphics::GpuTaskGraph metadataGraph(testArena.arena);
    ASSERT_TRUE(AddTextureMetadata(
        metadataGraph,
        Name("tests/task_graph/device_generation_metadata_texture"),
        "Device Generation Metadata Texture"
    ).valid());
    ASSERT_TRUE(AddPipelineMetadata(
        metadataGraph,
        Name("tests/task_graph/device_generation_metadata_pipeline"),
        "Device Generation Metadata Pipeline",
        Graphics::GpuGraphPipelineType::Compute
    ).valid());
    ASSERT_TRUE(AddTask(
        metadataGraph,
        Name("tests/task_graph/device_generation_metadata_task"),
        "Device Generation Metadata Task"
    ).valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(metadataGraph);
        EXPECT_TRUE(declarations.validForDeviceGeneration(s_TargetDeviceGeneration));
    }
    Graphics::GpuTaskGraphAnalysis metadataAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments metadataAssignments(testArena.arena);
    Graphics::GpuCompiledGraph metadataCompiledGraph(testArena.arena);
    EXPECT_TRUE(Compile(
        metadataGraph,
        metadataAnalysis,
        targetTopology,
        metadataAssignments,
        metadataCompiledGraph
    ));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

