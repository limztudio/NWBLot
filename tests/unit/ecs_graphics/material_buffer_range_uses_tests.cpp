// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>

#include <core/graphics/task_graph/compiler.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_buffer_range_uses_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MaterialBufferRangeUses, ActiveFramePrefixesDoNotSynchronizeUnusedCapacity){
    // Exercise the renderer reader helper with all four growable frame-stream element sizes, including the
    // smallest valid typed payload. Each allocation retains three times as much unused capacity as live data.
    const usize activeByteCounts[] = {
        3u * sizeof(Impl::InstanceGpuData),
        sizeof(u32),
        2u * sizeof(Impl::CsgReceiverRangeGpuData),
        3u * sizeof(Impl::CsgCutterGpuData),
    };
    for(const usize activeByteCount : activeByteCounts){
        TestArena<> testArena;
        Core::GraphicsAllocator graphicsAllocator(testArena.arena);
        Core::Alloc::CpuTaskScheduler cpuScheduler(0u);
        Core::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
        Core::GraphicsBackend::VulkanAllocator allocator(context);
        Core::GpuTaskGraph graph(testArena.arena);
        Core::Alloc::ScratchArena scratchArena(Name("tests/material_buffer_range_uses/scratch"));
        const usize capacityByteCount = 4u * activeByteCount;
        const Core::BufferHandle buffer(
            NewMetadataOnlyBuffer(
                testArena.arena,
                context,
                allocator,
                Core::BufferDesc{}.setByteSize(capacityByteCount).setInitialState(Core::ResourceStates::Common)
            ),
            Core::BufferHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        ASSERT_TRUE(buffer);
        const Core::GpuGraphResourceId resource = graph.importBuffer(
            buffer,
            Impl::RendererTaskGraphDetail::BufferResourceDesc(Name("tests/material_buffer_range_uses/stream"), "Frame Stream")
        );
        ASSERT_TRUE(resource.valid());
        Vector<u8, Core::Alloc::ScratchArena> uploadBytes{ scratchArena };
        uploadBytes.resize(activeByteCount, 0u);
        const Core::GpuUploadBlobId uploadBlob = graph.copyUploadData(uploadBytes.data(), uploadBytes.size());
        ASSERT_TRUE(uploadBlob.valid());
        const Core::GpuTaskId upload = graph.addUploadBufferTask(
            Core::GpuTaskDesc{}
                .setIdentity(Name("tests/material_buffer_range_uses/upload"))
                .setMarkerLabel("Frame Upload")
                .setQueue(Impl::RendererTaskGraphDetail::GraphicsUploadQueueRequest()),
            Core::GpuUploadBufferTaskDesc{
                .source = uploadBlob,
                .destination = resource,
                .finalState = Core::ResourceStates::Common,
            }
        );
        ASSERT_TRUE(upload.valid());
        const Core::GpuTaskResourceUse unusedTailUse{
            .resource = resource,
            .range = { .bufferRange = Core::BufferRange(activeByteCount, capacityByteCount - activeByteCount) },
            .requiredState = Core::ResourceStates::CopyDest,
            .access = Core::GpuTaskResourceAccess::Write,
        };
        const Core::GpuTaskId tailWrite = graph.addTask(
            Core::GpuTaskDesc{}
                .setIdentity(Name("tests/material_buffer_range_uses/tail"))
                .setMarkerLabel("Unused Tail Write")
                .setQueue(Impl::RendererTaskGraphDetail::GraphicsUploadQueueRequest())
                .setResourceUses(&unusedTailUse, 1u)
        );
        ASSERT_TRUE(tailWrite.valid());
        const Core::BufferRange activeRange(0u, activeByteCount);
        const Core::GpuTaskResourceUse readerUse = Impl::RendererTaskGraphDetail::ReadBufferUse(resource, activeRange);
        const Core::GpuTaskId reader = graph.addTask(
            Core::GpuTaskDesc{}
                .setIdentity(Name("tests/material_buffer_range_uses/read"))
                .setMarkerLabel("Frame Reader")
                .setQueue(Impl::RendererTaskGraphDetail::GraphicsQueueRequest())
                .setResourceUses(&readerUse, 1u)
        );
        ASSERT_TRUE(reader.valid());

        const Core::GpuPhysicalQueueInfo queue{
            .id = { 0u, 1u },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = static_cast<Core::GpuQueueCapability::Mask>(
                Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer
            ),
            .familyIndex = 0u,
            .queueIndex = 0u,
            .dedicated = false,
        };
        const Core::GpuTaskGraphQueueTopology topology{ .queues = &queue, .queueCount = 1u };
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::GpuCompiledGraph compiledGraph(testArena.arena);
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Core::GpuTaskGraphCompiler compiler;
        Core::GpuTaskGraphCompileOptions compileOptions;
        compileOptions.allowMetadataOnlyTasks = true;
        ASSERT_TRUE(compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions))
            << "analysis status=" << static_cast<u32>(analysis.diagnostic().status)
            << ", queue status=" << static_cast<u32>(assignments.diagnostic().status)
        ;
        ASSERT_EQ(analysis.edges().size(), 1u);
        EXPECT_EQ(analysis.edges().front().producer, upload);
        EXPECT_EQ(analysis.edges().front().consumer, reader);

        const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
        const Core::GpuCompiledTaskView readerPlan = plan.findTask(reader);
        ASSERT_TRUE(readerPlan.valid());
        ASSERT_EQ(readerPlan.plan->prologueBarrierCount, 1u);
        EXPECT_EQ(readerPlan.prologueBarriers[0u].range.bufferRange, activeRange);
        EXPECT_EQ(readerPlan.prologueBarriers[0u].after, Core::ResourceStates::ShaderResource);
        EXPECT_LT(readerPlan.prologueBarriers[0u].range.bufferRange.byteSize, buffer->getCreationDescription().byteSize);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

