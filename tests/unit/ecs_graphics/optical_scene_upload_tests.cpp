// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/task_graph_optical_scene_upload.h>
#include <impl/ecs_render/raytrace/task_graph_optical_bounds_finalize.h>
#include <impl/ecs_render/components.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/perf/timing.h>
#include <core/task/gpu/compiler_internal.h>
#include <core/task/gpu/scheduler.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_scene_upload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct OpticalSceneUploadTestsTag>;

struct UploadContext{
    TestArena testArena;
    Core::Alloc::ScratchArena scratch{ Name("tests/optical_upload/gather") };
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GpuTaskScheduler gpuTasks;
    Core::Perf::TimingRecorder timing{ testArena.arena };
    Core::GraphicsRuntime graphics{ graphicsAllocator, cpuScheduler, gpuTasks, timing };
    Core::GraphicsBackend::VulkanContext context;
    Core::GraphicsBackend::VulkanAllocator allocator;
    Core::BufferHandle buffer;
    RayTracingOpticalUploadControlHandle control;
    Core::GpuTaskGraph graph{ testArena.arena };

    explicit UploadContext(const u16 deviceGeneration = 1u)
        : context(graphicsAllocator, cpuScheduler, deviceGeneration)
        , allocator(context)
        , buffer(makeBuffer(Name("tests/optical_upload/buffer")))
        , control(CreateRayTracingOpticalUploadControl(testArena.arena, buffer, {0u, deviceGeneration}))
    {}

    [[nodiscard]] Core::BufferHandle makeBuffer(
        const Name identity,
        const bool initialStateKnown = true,
        const Core::ResourceStates::Mask initialState = Core::ResourceStates::Common){
        Core::BufferDesc desc;
        desc.setByteSize(256u).setCanHaveRawViews(true).setCanHaveUAVs(true).setDebugName(identity).enableAutomaticStateTracking(initialState);
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(testArena.arena, context, allocator, desc, initialStateKnown);
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), s_AdoptRef);
    }

    [[nodiscard]] RayTracingOpticalSceneUploadHandle makeUpload(const u32 entityIndex, const i32 priority = 0){
        RayTracingOpticalSceneGather gather(scratch, 1u);
        RendererComponent renderer;
        renderer.opticalBoundaryMode = OpticalBoundaryMode::ClosedPriority;
        renderer.opticalMediumPriority = priority;
        gather.append(Core::ECS::EntityID(entityIndex, 0u), renderer, true, {-1.f, -2.f, -3.f}, {1.f, 2.f, 3.f}, true);
        return RayTracingOpticalSceneUploadHandle(
            NewArenaObject<RayTracingOpticalSceneUploadControl>(testArena.arena, testArena.arena, gather),
            ArenaRefDeleter<RayTracingOpticalSceneUploadControl, Core::Alloc::GlobalArena>(&testArena.arena),
            s_AdoptRef
        );
    }

    [[nodiscard]] RayTracingOpticalSceneSnapshot snapshot(const RayTracingOpticalSceneUploadHandle& upload){
        return RayTracingOpticalSceneSnapshot{
            .buffer = buffer,
            .uploadBuffer = {},
            .finalize = {},
            .upload = upload,
            .uploadState = control,
            .descriptor = Core::GpuDescriptorHandle::Make(Core::GpuDescriptorClass::StorageBuffer, 1u),
            .transparentCount = 1u,
            .boundsComplete = true,
        };
    }
};

[[nodiscard]] Core::QueueSubmissionToken Token(const u64 value, const u16 generation = 1u){
    return Core::QueueSubmissionToken{
        .value = value,
        .physicalQueueIndex = 0u,
        .deviceGeneration = generation,
        .queue = Core::CommandQueue::Graphics,
    };
}

void Accept(RayTracingOpticalUploadState& state, const RayTracingOpticalSceneUploadHandle& upload, const u64 value){
    const auto plan = state.plan(upload);
    ASSERT_TRUE(plan.valid());
    ASSERT_FALSE(plan.reused);
    ASSERT_TRUE(state.reserve(plan));
    ASSERT_TRUE(state.accept(plan, Token(value)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(OpticalSceneUpload, PreparedAndReservedPayloadsNeverClaimResidency){
    UploadContext context;
    const auto upload = context.makeUpload(1u);
    ASSERT_TRUE(context.control);
    const auto first = context.control->plan(upload);
    const auto second = context.control->plan(upload);
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_FALSE(first.reused);
    EXPECT_FALSE(second.reused);
    EXPECT_FALSE(first.acceptedToken.valid());
    ASSERT_TRUE(context.control->reserve(first));
    EXPECT_FALSE(context.control->plan(upload).valid());
    EXPECT_FALSE(context.control->reserve(second));
    context.control->discard(first);
    const auto retry = context.control->plan(upload);
    EXPECT_TRUE(retry.valid());
    EXPECT_FALSE(retry.reused);
}

TEST(OpticalSceneUpload, AcceptedResidencyCannotBeReservedAgain){
    UploadContext context;
    const auto upload = context.makeUpload(1u);
    ASSERT_NO_FATAL_FAILURE(Accept(*context.control, upload, 11u));
    const auto reused = context.control->plan(upload);
    ASSERT_TRUE(reused.valid());
    EXPECT_TRUE(reused.reused);
    EXPECT_FALSE(context.control->reserve(reused));
}

TEST(OpticalSceneUpload, RejectedReplacementPreservesOnlyThePreviouslyAcceptedPayload){
    UploadContext context;
    const auto first = context.makeUpload(1u);
    const auto replacement = context.makeUpload(s_ExpectedDualCount);
    ASSERT_NO_FATAL_FAILURE(Accept(*context.control, first, 4u));
    const auto plan = context.control->plan(replacement);
    ASSERT_TRUE(context.control->reserve(plan));
    context.control->discard(plan);
    EXPECT_TRUE(context.control->plan(first).reused);
    EXPECT_FALSE(context.control->plan(replacement).reused);
    EXPECT_EQ(context.control->plan(first).acceptedToken.value, 4u);
}

TEST(OpticalSceneUpload, AcceptedReplacementSurvivesLaterFrameFailureAndForcesReversionUpload){
    UploadContext context;
    const auto first = context.makeUpload(1u);
    const auto replacement = context.makeUpload(s_ExpectedDualCount);
    ASSERT_NO_FATAL_FAILURE(Accept(*context.control, first, 4u));
    const auto plan = context.control->plan(replacement);
    ASSERT_TRUE(context.control->reserve(plan));
    ASSERT_TRUE(context.control->accept(plan, Token(5u)));
    // A later packet can fail; discarding its already accepted upload cannot restore the old GPU bytes.
    context.control->discard(plan);
    EXPECT_TRUE(context.control->plan(replacement).reused);
    EXPECT_FALSE(context.control->plan(first).reused);
    ASSERT_NO_FATAL_FAILURE(Accept(*context.control, first, 6u));
    EXPECT_TRUE(context.control->plan(first).reused);
    EXPECT_FALSE(context.control->plan(replacement).reused);
}

TEST(OpticalSceneUpload, ByteIdenticalNewIdentityConservativelyReuploadsAndPolicyChangesMiss){
    UploadContext context;
    const auto first = context.makeUpload(1u, 3);
    const auto duplicateBytes = context.makeUpload(1u, 3);
    const auto changedPriority = context.makeUpload(1u, 4);
    ASSERT_EQ(first->bytes.size(), duplicateBytes->bytes.size());
    EXPECT_EQ(GLB_MEMCMP(first->bytes.data(), duplicateBytes->bytes.data(), first->bytes.size()), 0);
    EXPECT_NE(GLB_MEMCMP(first->bytes.data(), changedPriority->bytes.data(), first->bytes.size()), 0);
    ASSERT_NO_FATAL_FAILURE(Accept(*context.control, first, s_ExpectedDualCount));
    EXPECT_FALSE(context.control->plan(duplicateBytes).reused);
    EXPECT_FALSE(context.control->plan(changedPriority).reused);
}

TEST(OpticalSceneUpload, AStaleUnreservedPlanCannotReplaceARecentlyAcceptedWriter){
    UploadContext context;
    const auto first = context.makeUpload(1u);
    const auto second = context.makeUpload(s_ExpectedDualCount);
    const auto stale = context.control->plan(first);
    ASSERT_NO_FATAL_FAILURE(Accept(*context.control, second, 3u));
    EXPECT_FALSE(context.control->reserve(stale));
    EXPECT_FALSE(context.control->accept(stale, Token(99u)));
    EXPECT_TRUE(context.control->plan(second).reused);
}

TEST(OpticalSceneUpload, LateCallbacksCannotReleaseANewerPendingReplacement){
    UploadContext context;
    const auto first = context.makeUpload(1u);
    const auto second = context.makeUpload(s_ExpectedDualCount);
    const auto old = context.control->plan(first);
    ASSERT_TRUE(context.control->reserve(old));
    ASSERT_TRUE(context.control->accept(old, Token(3u)));
    const auto current = context.control->plan(second);
    ASSERT_TRUE(context.control->reserve(current));
    context.control->discard(old);
    EXPECT_FALSE(context.control->accept(old, Token(s_ExpectedDualCount)));
    EXPECT_TRUE(context.control->isReserved(current));
    ASSERT_TRUE(context.control->accept(current, Token(4u)));
    EXPECT_TRUE(context.control->plan(second).reused);
}

TEST(OpticalSceneUpload, InvalidAcceptedProvenanceQuarantinesRatherThanRestoringStaleBytes){
    for(u32 invalid = 0u; invalid < 7u; ++invalid){
        UploadContext context;
        const auto first = context.makeUpload(1u);
        const auto second = context.makeUpload(s_ExpectedDualCount);
        ASSERT_NO_FATAL_FAILURE(Accept(*context.control, first, 8u));
        const auto plan = context.control->plan(second);
        ASSERT_TRUE(context.control->reserve(plan));
        auto token = Token(9u);
        switch(invalid){
        case 0u: token.value = 0u; break;
        case 1u: token.physicalQueueIndex = Limit<u16>::s_Max; break;
        case s_ExpectedDualCount: token.deviceGeneration = s_ExpectedDualCount; break;
        case 3u: token.queue = Core::CommandQueue::Compute; break;
        case 4u: token.value = 8u; break;
        case 5u: token.value = 7u; break;
        case 6u: token.physicalQueueIndex = 1u; break;
        }
        EXPECT_FALSE(context.control->accept(plan, token));
        context.control->discard(plan);
        EXPECT_FALSE(context.control->plan(first).valid());
        EXPECT_FALSE(context.control->plan(second).valid());
    }
}

TEST(OpticalSceneUpload, BufferIdentityAndControlIdentityPreventCrossGenerationAdoption){
    UploadContext context;
    const auto upload = context.makeUpload(1u);
    const auto old = context.control->plan(upload);
    const auto otherBuffer = context.makeBuffer(Name("tests/optical_upload/replacement"));
    auto other = CreateRayTracingOpticalUploadControl(context.testArena.arena, otherBuffer, {0u, 1u});
    auto sameBufferNewControl = CreateRayTracingOpticalUploadControl(context.testArena.arena, context.buffer, {0u, 1u});
    ASSERT_TRUE(other);
    ASSERT_TRUE(sameBufferNewControl);
    EXPECT_FALSE(other->reserve(old));
    EXPECT_FALSE(sameBufferNewControl->reserve(old));
    EXPECT_FALSE(other->plan(upload).reused);
    EXPECT_FALSE(sameBufferNewControl->plan(upload).reused);
    EXPECT_FALSE(CreateRayTracingOpticalUploadControl(context.testArena.arena, context.buffer, {0u, s_ExpectedDualCount}));
}

TEST(OpticalSceneUpload, OwnerInvalidationBlocksOldCallbacksWithoutAffectingNewDeviceResources){
    UploadContext oldContext;
    UploadContext newContext(s_ExpectedDualCount);
    const auto upload = oldContext.makeUpload(1u);
    const auto old = oldContext.control->plan(upload);
    ASSERT_TRUE(oldContext.control->reserve(old));
    oldContext.control->invalidate();
    const auto next = newContext.control->plan(upload);
    ASSERT_TRUE(next.valid());
    EXPECT_FALSE(next.reused);
    ASSERT_TRUE(newContext.control->reserve(next));
    oldContext.control->discard(old);
    EXPECT_FALSE(oldContext.control->accept(old, Token(1u)));
    EXPECT_FALSE(oldContext.control->plan(upload).valid());
    EXPECT_TRUE(newContext.control->isReserved(next));
    ASSERT_TRUE(newContext.control->accept(next, Token(1u, s_ExpectedDualCount)));
    EXPECT_TRUE(newContext.control->plan(upload).reused);
}

TEST(OpticalSceneUpload, MoveOnlyReservationPublishesOnceAndDestructorKeepsAcceptedResidency){
    UploadContext context;
    const auto upload = context.makeUpload(1u);
    const auto plan = context.control->plan(upload);
    {
        RayTracingOpticalUploadReservation original(context.control, plan);
        ASSERT_TRUE(original.valid());
        RayTracingOpticalUploadReservation moved(Move(original));
        EXPECT_FALSE(original.valid());
        ASSERT_TRUE(moved.valid());
        EXPECT_TRUE(moved.accept(Token(3u)));
        EXPECT_FALSE(moved.accept(Token(99u)));
    }
    EXPECT_TRUE(context.control->plan(upload).reused);
    EXPECT_EQ(context.control->plan(upload).acceptedToken.value, 3u);
}

TEST(OpticalSceneUpload, UnacceptedReservationDestructionReleasesOnlyItsOwnAttempt){
    UploadContext context;
    const auto first = context.makeUpload(1u);
    const auto second = context.makeUpload(s_ExpectedDualCount);
    ASSERT_NO_FATAL_FAILURE(Accept(*context.control, first, s_ExpectedDualCount));
    {
        RayTracingOpticalUploadReservation pending(context.control, context.control->plan(second));
        ASSERT_TRUE(pending.valid());
        EXPECT_FALSE(context.control->plan(first).valid());
    }
    EXPECT_TRUE(context.control->plan(first).reused);
    EXPECT_FALSE(context.control->plan(second).reused);
}


TEST(OpticalSceneUpload, BoundsFinalizeRejectsComputeOnlyAndSelectsPrimaryGraphics){
    UploadContext context;
    const auto upload = context.makeUpload(1u);
    auto resources = context.snapshot(upload);
    resources.uploadBuffer = context.buffer;
    resources.finalize = RayTracingOpticalBoundsFinalizeHandle(
        NewArenaObject<RayTracingOpticalBoundsFinalizeControl>(context.testArena.arena, context.testArena.arena, context.graphics),
        ArenaRefDeleter<RayTracingOpticalBoundsFinalizeControl, Core::Alloc::GlobalArena>(&context.testArena.arena), s_AdoptRef
    );
    resources.finalize->inputBuffer = context.makeBuffer(Name("tests/optical_finalize/inputs"));
    resources.finalize->outputBuffer = context.makeBuffer(Name("tests/optical_finalize/output"));
    resources.finalize->inputs.resize(1u);
    resources.finalize->boundsBuffers.resize(1u);
    resources.finalize->boundsBuffers[0u] = context.makeBuffer(
        Name("tests/optical_finalize/bounds"), true, Core::ResourceStates::ShaderResource
    );
    resources.finalize->queue = { .index = 0u, .deviceGeneration = 1u };
    const Core::GpuGraphResourceId source = context.graph.importBuffer(
        context.buffer,
        Core::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/optical_finalize/source"))
            .setMarkerLabel("Optical Finalize Source")
            .setType(Core::GpuGraphResourceType::Buffer)
            .setInitialState(Core::ResourceStates::Common)
            .setExternalFinalState(Core::ResourceStates::Common)
    );
    ASSERT_TRUE(source.valid());
    const auto result = DeclareRayTracingOpticalBoundsFinalize(context.graph, resources, source, context.scratch);
    ASSERT_TRUE(result.valid());
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    ASSERT_EQ(view.taskCount(), 1u);
    const auto task = view.taskAt(result.uploadTask.index);
    EXPECT_EQ(task.commands.requiredCapabilities, Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer);

    const Core::GpuPhysicalQueueInfo computeQueue{
        .familyIndex = 0u,
        .queueIndex = 1u,
        .id = { .index = 1u, .deviceGeneration = 1u },
        .queueClass = Core::CommandQueue::Compute,
        .capabilities = Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer,
    };
    const Core::GpuPhysicalQueueInfo primaryGraphicsQueue{
        .familyIndex = 0u,
        .queueIndex = 0u,
        .id = resources.finalize->queue,
        .queueClass = Core::CommandQueue::Graphics,
        .capabilities = Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer,
    };
    // Both candidates admit every import; only the finalize task's retained writer timeline forbids Compute.
    for(usize resourceIndex = 0u; resourceIndex < view.resourceCount(); ++resourceIndex){
        const auto resource = view.resourceAt(resourceIndex);
        ASSERT_TRUE(resource.hasQueueAdmission);
        ASSERT_FALSE(resource.initialOwnerQueue.valid());
        ASSERT_FALSE(resource.directConsumerQueue.valid());
        ASSERT_TRUE(Core::ResourceQueueAdmissionAdmitsQueue(resource.queueAdmission, computeQueue));
        ASSERT_TRUE(Core::ResourceQueueAdmissionAdmitsQueue(resource.queueAdmission, primaryGraphicsQueue));
    }
    Core::GpuTaskGraphAnalysis analysis(context.testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(context.testArena.arena);
    Core::GpuCompiledGraph compiled(context.testArena.arena);
    const Core::GpuTaskGraphCompiler compiler;
    const Core::GpuPhysicalQueueTopology computeOnlyTopology{ .queues = &computeQueue, .queueCount = 1u };
    EXPECT_FALSE(compiler.compile(view, analysis, computeOnlyTopology, assignments, compiled, context.scratch));
    ASSERT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::Success);
    EXPECT_EQ(assignments.diagnostic().status, Core::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue);
    EXPECT_EQ(assignments.diagnostic().task, result.uploadTask);

    const Core::GpuPhysicalQueueInfo queues[] = { computeQueue, primaryGraphicsQueue };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };
    ASSERT_TRUE(compiler.compile(view, analysis, topology, assignments, compiled, context.scratch))
        << "analysis=" << static_cast<u32>(analysis.diagnostic().status)
        << ", queue=" << static_cast<u32>(assignments.diagnostic().status);
    const Core::GpuTaskQueueAssignment* const assignment = assignments.find(result.uploadTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queue, primaryGraphicsQueue.id);
    EXPECT_EQ(assignment->queueClass, Core::CommandQueue::Graphics);
}


TEST(OpticalSceneUpload, RealGraphResetDiscardsTheProductionUploadReservation){
    UploadContext context;
    const auto first = context.makeUpload(1u);
    const auto second = context.makeUpload(s_ExpectedDualCount);
    ASSERT_NO_FATAL_FAILURE(Accept(*context.control, first, 3u));
    ASSERT_TRUE(ImportRayTracingOpticalSceneBuffer(context.graph, context.snapshot(second), context.scratch).valid());
    EXPECT_FALSE(context.control->plan(first).valid());
    context.graph.reset();
    EXPECT_TRUE(context.control->plan(first).reused);
    EXPECT_FALSE(context.control->plan(second).reused);
    ASSERT_TRUE(ImportRayTracingOpticalSceneBuffer(context.graph, context.snapshot(second), context.scratch).valid());
}

TEST(OpticalSceneUpload, ConflictingGraphImportReleasesTheAttemptWithoutPublishingResidency){
    UploadContext context;
    const auto upload = context.makeUpload(1u);
    Core::GpuGraphResourceDesc conflict;
    conflict.setIdentity(context.buffer->getCreationDescription().debugName)
        .setMarkerLabel("Conflicting Optical Metadata")
        .setType(Core::GpuGraphResourceType::Buffer)
        .setInitialState(Core::ResourceStates::CopyDest);
    ASSERT_TRUE(context.graph.importBuffer(context.buffer, conflict).valid());
    EXPECT_FALSE(ImportRayTracingOpticalSceneBuffer(context.graph, context.snapshot(upload), context.scratch).valid());
    const auto retry = context.control->plan(upload);
    EXPECT_TRUE(retry.valid());
    EXPECT_FALSE(retry.reused);
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    EXPECT_EQ(view.taskCount(), 0u);
    EXPECT_EQ(view.uploadBlobCount(), 0u);
}

TEST(OpticalSceneUpload, ProductionPayloadRetainsTheControlAfterSnapshotAndOwnerReferencesDrop){
    UploadContext context;
    const auto upload = context.makeUpload(1u);
    auto snapshot = context.snapshot(upload);
    RayTracingOpticalUploadState* const retained = snapshot.uploadState.get();
    ASSERT_TRUE(ImportRayTracingOpticalSceneBuffer(context.graph, snapshot, context.scratch).valid());
    snapshot = {};
    context.control = nullptr;
    EXPECT_FALSE(retained->plan(upload).valid());
    context.graph.reset();
    // The production graph payload was the final control owner and is destroyed here without an escaping sink.
}

TEST(OpticalSceneUpload, UnknownRetainedStateAndMismatchedBufferControlFailBeforeImport){
    UploadContext context;
    const auto upload = context.makeUpload(1u);
    auto snapshot = context.snapshot(upload);
    snapshot.buffer = context.makeBuffer(Name("tests/optical_upload/unknown"), false);
    EXPECT_FALSE(ImportRayTracingOpticalSceneBuffer(context.graph, snapshot, context.scratch).valid());
    snapshot.buffer = context.makeBuffer(Name("tests/optical_upload/different"));
    EXPECT_FALSE(ImportRayTracingOpticalSceneBuffer(context.graph, snapshot, context.scratch).valid());
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    EXPECT_EQ(view.resourceCount(), 0u);
    EXPECT_EQ(view.taskCount(), 0u);
    EXPECT_EQ(view.uploadBlobCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

