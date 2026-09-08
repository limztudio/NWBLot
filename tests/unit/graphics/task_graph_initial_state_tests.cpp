// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_initial_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, MarksAndMaterializesDeclaredInitialResourceStates){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/initial_state_texture"),
        "Initial State Texture",
        Graphics::ResourceStates::ShaderResource
    );
    const Graphics::GpuGraphResourceId buffer = AddBufferMetadata(
        graph,
        Name("tests/task_graph/initial_state_buffer"),
        "Initial State Buffer",
        Graphics::ResourceStates::ShaderResource
    );
    const Graphics::GpuGraphResourceId accelStruct = AddAccelStructMetadata(
        graph,
        Name("tests/task_graph/initial_state_accel_struct"),
        "Initial State Accel Struct",
        Graphics::ResourceStates::AccelStructRead
    );
    ASSERT_TRUE(texture.valid());
    ASSERT_TRUE(buffer.valid());
    ASSERT_TRUE(accelStruct.valid());

    const Graphics::GpuTaskResourceUse initialUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = texture,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = buffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accelStruct,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId initialTask = AddTask(
        graph,
        Name("tests/task_graph/initial_state_materialize"),
        "Initial State Materialize",
        nullptr,
        0u,
        initialUses,
        LengthOf(initialUses)
    );
    ASSERT_TRUE(initialTask.valid());

    const Graphics::GpuTaskId internalDependencies[] = { initialTask };
    const Graphics::GpuTaskResourceUse internalUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = buffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    const Graphics::GpuTaskId internalTask = AddTask(
        graph,
        Name("tests/task_graph/initial_state_internal"),
        "Initial State Internal",
        internalDependencies,
        LengthOf(internalDependencies),
        internalUses,
        LengthOf(internalUses)
    );
    ASSERT_TRUE(internalTask.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const auto findPrologueBarrier = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource) -> const Graphics::GpuCompiledBarrier*{
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        if(!compiledTask || !barriers)
            return nullptr;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            if(barriers[barrierIndex].resource == resource)
                return &barriers[barrierIndex];
        }
        return nullptr;
    };

    const Graphics::GpuCompiledBarrier* const textureBarrier = findPrologueBarrier(initialTask, texture);
    ASSERT_NE(textureBarrier, nullptr);
    EXPECT_EQ(textureBarrier->type, Graphics::GpuCompiledBarrierType::TextureTransition);
    EXPECT_EQ(textureBarrier->before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(textureBarrier->after, Graphics::ResourceStates::ShaderResource);
    EXPECT_TRUE(textureBarrier->isGraphInitialState);

    const Graphics::GpuCompiledBarrier* const bufferBarrier = findPrologueBarrier(initialTask, buffer);
    ASSERT_NE(bufferBarrier, nullptr);
    EXPECT_EQ(bufferBarrier->type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(bufferBarrier->before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(bufferBarrier->after, Graphics::ResourceStates::ShaderResource);
    EXPECT_TRUE(bufferBarrier->isGraphInitialState);

    const Graphics::GpuCompiledBarrier* const accelStructBarrier = findPrologueBarrier(initialTask, accelStruct);
    ASSERT_NE(accelStructBarrier, nullptr);
    EXPECT_EQ(accelStructBarrier->type, Graphics::GpuCompiledBarrierType::AccelStructTransition);
    EXPECT_EQ(accelStructBarrier->before, Graphics::ResourceStates::AccelStructRead);
    EXPECT_EQ(accelStructBarrier->after, Graphics::ResourceStates::AccelStructRead);
    EXPECT_TRUE(accelStructBarrier->isGraphInitialState);

    const Graphics::GpuCompiledBarrier* const internalBarrier = findPrologueBarrier(internalTask, buffer);
    ASSERT_NE(internalBarrier, nullptr);
    EXPECT_EQ(internalBarrier->type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(internalBarrier->before, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(internalBarrier->after, Graphics::ResourceStates::CopySource);
    EXPECT_FALSE(internalBarrier->isGraphInitialState);
}

TEST(GpuStateTracker, DistinguishesRetainedDescriptorFallbackFromExplicitState){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::Alloc::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);

    Graphics::Buffer* const bufferObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        Graphics::BufferDesc().setByteSize(256u).enableAutomaticStateTracking(Graphics::ResourceStates::ShaderResource),
        true
    );
    ASSERT_NE(bufferObject, nullptr);
    Graphics::BufferHandle buffer(
        bufferObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        Graphics::TextureDesc{}
    );
    ASSERT_NE(textureObject, nullptr);
    Graphics::TextureHandle texture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::GraphicsBackend::StateTracker stateTracker(context);
    EXPECT_EQ(stateTracker.getPermanentBufferState(nullptr), Graphics::ResourceStates::Unknown);
    EXPECT_EQ(stateTracker.getPermanentTextureState(nullptr), Graphics::ResourceStates::Unknown);
    EXPECT_EQ(stateTracker.getPermanentBufferState(buffer.get()), Graphics::ResourceStates::Unknown);
    EXPECT_EQ(stateTracker.getPermanentTextureState(texture.get()), Graphics::ResourceStates::Unknown);
    // A retained descriptor provides a useful native fallback, but it is not a packet handoff. The graph may
    // authoritatively materialize its declared initial state over it.
    EXPECT_EQ(stateTracker.getBufferState(buffer.get()), Graphics::ResourceStates::ShaderResource);
    EXPECT_FALSE(stateTracker.hasExplicitBufferState(buffer.get()));

    stateTracker.beginTrackingBuffer(buffer.get(), Graphics::ResourceStates::CopySource);
    EXPECT_TRUE(stateTracker.hasExplicitBufferState(buffer.get()));
    EXPECT_EQ(stateTracker.getBufferState(buffer.get()), Graphics::ResourceStates::CopySource);

    stateTracker.reset();
    stateTracker.setPermanentBufferState(*buffer, Graphics::ResourceStates::Unknown);
    EXPECT_EQ(stateTracker.getPermanentBufferState(buffer.get()), Graphics::ResourceStates::Unknown);
    stateTracker.setPermanentBufferState(*buffer, Graphics::ResourceStates::CopyDest);
    EXPECT_TRUE(stateTracker.hasExplicitBufferState(buffer.get()));
    EXPECT_EQ(stateTracker.getPermanentBufferState(buffer.get()), Graphics::ResourceStates::CopyDest);
    stateTracker.setPermanentBufferState(*buffer, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(stateTracker.getPermanentBufferState(buffer.get()), Graphics::ResourceStates::CopyDest);
    stateTracker.beginTrackingBuffer(buffer.get(), Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(stateTracker.getBufferState(buffer.get()), Graphics::ResourceStates::CopyDest);

    stateTracker.setPermanentTextureState(*texture, Graphics::ResourceStates::Unknown);
    EXPECT_EQ(stateTracker.getPermanentTextureState(texture.get()), Graphics::ResourceStates::Unknown);
    stateTracker.setPermanentTextureState(*texture, Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(stateTracker.getPermanentTextureState(texture.get()), Graphics::ResourceStates::ShaderResource);
    stateTracker.setPermanentTextureState(*texture, Graphics::ResourceStates::CopyDest);
    EXPECT_EQ(stateTracker.getPermanentTextureState(texture.get()), Graphics::ResourceStates::ShaderResource);
    EXPECT_EQ(stateTracker.getTextureState(texture.get(), 0u, 0u), Graphics::ResourceStates::ShaderResource);
}

TEST(GpuTaskGraph, MarksUnknownTypedFirstReadsForExplicitNativeStateValidation){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::Alloc::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);

    Graphics::Buffer* const readWriteBufferObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        Graphics::BufferDesc().setByteSize(64u).setInitialState(Graphics::ResourceStates::Unknown)
    );
    Graphics::Buffer* const writeBufferObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        Graphics::BufferDesc().setByteSize(64u).setInitialState(Graphics::ResourceStates::Unknown)
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
    Graphics::Texture* const writeTextureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        Graphics::TextureDesc().setInitialState(Graphics::ResourceStates::Unknown)
    );
    Graphics::RayTracingAccelStruct* const accelStructObject = NewArenaObject<Graphics::RayTracingAccelStruct>(
        testArena.arena,
        context
    );
    ASSERT_NE(readWriteBufferObject, nullptr);
    ASSERT_NE(writeBufferObject, nullptr);
    ASSERT_NE(textureObject, nullptr);
    ASSERT_NE(writeTextureObject, nullptr);
    ASSERT_NE(accelStructObject, nullptr);
    Graphics::BufferHandle readWriteBuffer(
        readWriteBufferObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle writeBuffer(
        writeBufferObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::TextureHandle texture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::TextureHandle writeTexture(
        writeTextureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::RayTracingAccelStructHandle accelStruct(
        accelStructObject,
        Graphics::RayTracingAccelStructHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    // A graph importer can explicitly preserve Vulkan's fresh-resource origin instead of inheriting this logical
    // descriptor state.

    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId textureResource = graph.importTexture(
        texture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/unknown_first_read_texture"))
            .setMarkerLabel("Unknown First Read Texture")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Unknown)
    );
    const Graphics::GpuGraphResourceId bufferResource = graph.importBuffer(
        readWriteBuffer,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/unknown_first_read_buffer"))
            .setMarkerLabel("Unknown First Read Buffer")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Unknown)
    );
    const Graphics::GpuGraphResourceId accelStructResource = graph.importAccelStruct(
        accelStruct,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/unknown_first_read_accel_struct"))
            .setMarkerLabel("Unknown First Read Accel Struct")
            .setType(Graphics::GpuGraphResourceType::AccelStruct)
            .setInitialState(Graphics::ResourceStates::Unknown)
    );
    const Graphics::GpuGraphResourceId writeResource = graph.importBuffer(
        writeBuffer,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/unknown_first_write_buffer"))
            .setMarkerLabel("Unknown First Write Buffer")
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(Graphics::ResourceStates::Unknown)
    );
    const Graphics::GpuGraphResourceId writeTextureResource = graph.importTexture(
        writeTexture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/unknown_first_write_texture"))
            .setMarkerLabel("Unknown First Write Texture")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Unknown)
    );
    ASSERT_TRUE(textureResource.valid());
    ASSERT_TRUE(bufferResource.valid());
    ASSERT_TRUE(accelStructResource.valid());
    ASSERT_TRUE(writeResource.valid());
    ASSERT_TRUE(writeTextureResource.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.resourceAt(textureResource.index).initialState, Graphics::ResourceStates::Unknown);
    }

    const Graphics::GpuTaskResourceUse uses[]{
        Graphics::GpuTaskResourceUse{
            .resource = textureResource,
            .range = Graphics::GpuTaskResourceRange{
                .textureSubresources = Graphics::TextureSubresourceSet(1u, 1u, 1u, 1u),
            },
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = bufferResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
        Graphics::GpuTaskResourceUse{
            .resource = accelStructResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::AccelStructRead,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = writeResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = writeTextureResource,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId task = AddTask(
        graph,
        Name("tests/task_graph/unknown_first_read"),
        "Unknown First Read",
        nullptr,
        0u,
        uses,
        LengthOf(uses)
    );
    ASSERT_TRUE(task.valid());

    const Graphics::GpuPhysicalQueueInfo queues[]{ GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const auto findPrologueBarrier = [&](const Graphics::GpuGraphResourceId resource) -> const Graphics::GpuCompiledBarrier*{
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        if(!compiledTask || !barriers)
            return nullptr;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            if(barriers[barrierIndex].resource == resource)
                return &barriers[barrierIndex];
        }
        return nullptr;
    };

    const Graphics::GpuCompiledBarrier* const textureBarrier = findPrologueBarrier(textureResource);
    ASSERT_NE(textureBarrier, nullptr);
    EXPECT_EQ(textureBarrier->before, Graphics::ResourceStates::Unknown);
    EXPECT_TRUE(textureBarrier->isGraphInitialState);
    EXPECT_EQ(
        textureBarrier->range.textureSubresources,
        Graphics::TextureSubresourceSet(1u, 1u, 1u, 1u)
    );

    const Graphics::GpuCompiledBarrier* const bufferBarrier = findPrologueBarrier(bufferResource);
    ASSERT_NE(bufferBarrier, nullptr);
    EXPECT_EQ(bufferBarrier->before, Graphics::ResourceStates::Unknown);
    EXPECT_TRUE(bufferBarrier->isGraphInitialState);

    const Graphics::GpuCompiledBarrier* const accelStructBarrier = findPrologueBarrier(accelStructResource);
    ASSERT_NE(accelStructBarrier, nullptr);
    EXPECT_EQ(accelStructBarrier->before, Graphics::ResourceStates::Unknown);
    EXPECT_TRUE(accelStructBarrier->isGraphInitialState);

    const Graphics::GpuCompiledBarrier* const writeBarrier = findPrologueBarrier(writeResource);
    ASSERT_NE(writeBarrier, nullptr);
    EXPECT_EQ(writeBarrier->before, Graphics::ResourceStates::Unknown);
    EXPECT_FALSE(writeBarrier->isGraphInitialState);

    const Graphics::GpuCompiledBarrier* const writeTextureBarrier = findPrologueBarrier(writeTextureResource);
    ASSERT_NE(writeTextureBarrier, nullptr);
    EXPECT_EQ(writeTextureBarrier->before, Graphics::ResourceStates::Unknown);
    EXPECT_EQ(writeTextureBarrier->after, Graphics::ResourceStates::UnorderedAccess);
    EXPECT_FALSE(writeTextureBarrier->isGraphInitialState);
}

TEST(GpuTaskGraph, AccelStructImportsInheritBackingBufferStateKnowledge){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::Alloc::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);

    const Graphics::BufferDesc managedBackingDesc = Graphics::BufferDesc()
        .setInitialState(Graphics::ResourceStates::AccelStructRead)
    ;
    const Graphics::BufferDesc unknownRetainedBackingDesc = Graphics::BufferDesc()
        .setInitialState(Graphics::ResourceStates::AccelStructRead)
        .setKeepInitialState(true)
    ;
    Graphics::Buffer* const managedBackingObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        managedBackingDesc
    );
    Graphics::Buffer* const unknownRetainedBackingObject = NewMetadataOnlyBuffer(
        testArena.arena,
        context,
        allocator,
        unknownRetainedBackingDesc
    );
    Graphics::RayTracingAccelStruct* const managedAccelStructObject =
        NewArenaObject<Graphics::RayTracingAccelStruct>(testArena.arena, context);
    Graphics::RayTracingAccelStruct* const unknownAccelStructObject =
        NewArenaObject<Graphics::RayTracingAccelStruct>(testArena.arena, context);
    ASSERT_NE(managedBackingObject, nullptr);
    ASSERT_NE(unknownRetainedBackingObject, nullptr);
    ASSERT_NE(managedAccelStructObject, nullptr);
    ASSERT_NE(unknownAccelStructObject, nullptr);

    Graphics::BufferHandle managedBacking(
        managedBackingObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle unknownRetainedBacking(
        unknownRetainedBackingObject,
        Graphics::BufferHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::RayTracingAccelStructHandle managedAccelStruct(
        managedAccelStructObject,
        Graphics::RayTracingAccelStructHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::RayTracingAccelStructHandle unknownAccelStruct(
        unknownAccelStructObject,
        Graphics::RayTracingAccelStructHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    Graphics::BufferHandle& managedAccelStructBacking = const_cast<Graphics::BufferHandle&>(
        managedAccelStruct->getBackingBufferHandle()
    );
    Graphics::BufferHandle& unknownAccelStructBacking = const_cast<Graphics::BufferHandle&>(
        unknownAccelStruct->getBackingBufferHandle()
    );
    managedAccelStructBacking = managedBacking;
    unknownAccelStructBacking = unknownRetainedBacking;

    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId managedResource = graph.importAccelStruct(
        managedAccelStruct,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/managed_accel_struct_initial_state"))
            .setMarkerLabel("Managed Accel Struct Initial State")
            .setType(Graphics::GpuGraphResourceType::AccelStruct)
    );
    const Graphics::GpuGraphResourceId unknownResource = graph.importAccelStruct(
        unknownAccelStruct,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/unknown_accel_struct_initial_state"))
            .setMarkerLabel("Unknown Accel Struct Initial State")
            .setType(Graphics::GpuGraphResourceType::AccelStruct)
    );
    ASSERT_TRUE(managedResource.valid());
    ASSERT_TRUE(unknownResource.valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    EXPECT_EQ(
        declarations.resourceAt(managedResource.index).initialState,
        Graphics::ResourceStates::AccelStructRead
    );
    EXPECT_EQ(declarations.resourceAt(unknownResource.index).initialState, Graphics::ResourceStates::Unknown);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

