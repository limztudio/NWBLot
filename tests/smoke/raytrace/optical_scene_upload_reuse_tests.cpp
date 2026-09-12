// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/smoke/descriptor_buffer/round_trip/round_trip_fixture.h>

#include <impl/ecs_render/raytrace/task_graph_optical_scene_upload.h>
#include <impl/ecs_render/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_scene_upload_reuse_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class OpticalStorage final : NoCopy{
public:
    OpticalStorage(GraphicsBackend::Device& device, Alloc::GlobalArena& arena)
        : m_device(device)
        , m_arena(arena)
    {}
    ~OpticalStorage(){
        reset();
    }


public:
    [[nodiscard]] bool create(const usize byteSize){
        reset();
        m_snapshot.buffer = m_device.createBuffer(
            BufferDesc{}
                .setDebugName(Name("tests.optical_upload.native_buffer"))
                .setByteSize(byteSize)
                .setCanHaveRawViews(true)
                .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
                .enableAutomaticStateTracking(ResourceStates::Common)
        );
        if(!m_snapshot.buffer)
            return false;
        auto& heap = m_device.getDescriptorHeap();
        m_snapshot.descriptor = heap.allocate(GpuDescriptorClass::StorageBuffer);
        if(!m_snapshot.descriptor.valid() || !heap.write(m_snapshot.descriptor, DescriptorWriteItem::RawBuffer_SRV(0u, m_snapshot.buffer.get())))
            return false;
        m_snapshot.uploadState = Impl::CreateRayTracingOpticalUploadControl(
            m_arena, m_snapshot.buffer, m_device.getPrimaryPhysicalQueue(CommandQueue::Graphics)
        );
        return static_cast<bool>(m_snapshot.uploadState);
    }

    [[nodiscard]] Impl::RayTracingOpticalSceneSnapshot snapshot(const Impl::RayTracingOpticalSceneUploadHandle& upload)const{
        auto result = m_snapshot;
        result.upload = upload;
        result.transparentCount = upload ? upload->instanceCount : 0u;
        result.boundsComplete = true;
        return result;
    }

private:
    void reset(){
        if(m_snapshot.uploadState)
            m_snapshot.uploadState->invalidate();
        if(m_snapshot.descriptor.valid())
            m_device.getDescriptorHeap().free(m_snapshot.descriptor);
        m_snapshot = {};
    }

private:
    GraphicsBackend::Device& m_device;
    Alloc::GlobalArena& m_arena;
    Impl::RayTracingOpticalSceneSnapshot m_snapshot;
};

class OpticalSceneOwner final : NoCopy{
public:
    OpticalSceneOwner(Alloc::GlobalArena& arena, GraphicsRuntime& graphics)
        : resources(arena, graphics, Name("tests.optical_upload.owner"))
    {}
    ~OpticalSceneOwner(){ resources.invalidate(); }


public:
    Impl::RayTracingOpticalSceneResources resources;
};

struct CopyContext{
    Impl::RayTracingOpticalSceneSnapshot optical;
    BufferHandle readback;
    usize taskCount = 0u;
    usize uploadBlobCount = 0u;
    bool reused = false;
};

[[nodiscard]] Impl::RayTracingOpticalSceneUploadHandle MakeUpload(
    Alloc::GlobalArena& arena, Alloc::ScratchArena& scratch, const u32 entityIndex){
    Impl::RayTracingOpticalSceneGather gather(scratch, 1u);
    Impl::RendererComponent renderer;
    renderer.opticalBoundaryMode = Impl::OpticalBoundaryMode::ClosedPriority;
    renderer.opticalMediumPriority = static_cast<i32>(entityIndex);
    gather.append(ECS::EntityID(entityIndex, 1u), renderer, true, {-2.f, -3.f, -4.f}, {2.f, 3.f, 4.f}, true);
    return Impl::RayTracingOpticalSceneUploadHandle(
        NewArenaObject<Impl::RayTracingOpticalSceneUploadControl>(arena, arena, gather),
        ArenaRefDeleter<Impl::RayTracingOpticalSceneUploadControl, Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}

[[nodiscard]] GpuTaskId DeclareOpticalReadback(void* const rawContext, GpuTaskGraph& graph){
    if(!rawContext)
        return {};
    auto& context = *static_cast<CopyContext*>(rawContext);
    if(!context.readback)
        return {};
    const auto optical = Impl::ImportRayTracingOpticalSceneBuffer(graph, context.optical);
    if(!optical.valid())
        return {};
    context.reused = optical.reused;
    const auto destination = graph.importBuffer(
        context.readback,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests.optical_upload.readback"))
            .setMarkerLabel("Optical Metadata Readback")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
            .setExternalFinalState(ResourceStates::Common)
    );
    if(!destination.valid())
        return {};
    const GpuCopyBufferTaskRegion region{
        .source = optical.resource,
        .sourceOffsetBytes = 0u,
        .destination = destination,
        .destinationOffsetBytes = 0u,
        .dataSizeBytes = context.optical.upload->bytes.size(),
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests.optical_upload.copy"))
        .setMarkerLabel("Optical Metadata Copy")
        .setQueue(GpuQueueRequest{GpuQueueCapability::Transfer, GpuQueuePreference::Graphics, false, false})
        .setScheduling(scheduling)
    ;
    if(optical.uploadTask.valid())
        desc.setDependencies(&optical.uploadTask, 1u);
    const auto copied = graph.addCopyBufferTask(desc, GpuCopyBufferTaskDesc{.regions = &region, .regionCount = 1u});
    {
        const GpuTaskGraph::DeclarationReadView view(graph);
        if(!view.valid())
            return {};
        context.taskCount = view.taskCount();
        context.uploadBlobCount = view.uploadBlobCount();
    }
    return copied;
}

void SubmitAndVerify(
    GraphicsRuntime& graphics,
    const Impl::RayTracingOpticalSceneSnapshot& optical,
    const BufferHandle& readback,
    const bool expectedReuse){
    auto& device = graphics.getDevice();
    const auto before = optical.uploadState->plan(optical.upload);
    ASSERT_TRUE(before.valid());
    EXPECT_EQ(before.reused, expectedReuse);
    CopyContext context{.optical = optical, .readback = readback};
    QueueSubmissionToken terminal;
    ASSERT_TRUE(graphics.submitStandaloneTaskGraph(
        &context, &DeclareOpticalReadback, terminal, device.getPrimaryPhysicalQueue(CommandQueue::Graphics)
    ));
    ASSERT_TRUE(terminal.valid());
    EXPECT_EQ(context.reused, expectedReuse);
    EXPECT_EQ(context.uploadBlobCount, expectedReuse ? 0u : 1u);
    EXPECT_EQ(context.taskCount, expectedReuse ? 1u : 2u);
    const auto accepted = optical.uploadState->plan(optical.upload);
    ASSERT_TRUE(accepted.valid());
    EXPECT_TRUE(accepted.reused);
    ASSERT_TRUE(accepted.acceptedToken.valid());
    const auto primary = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    EXPECT_TRUE(accepted.acceptedToken.matchesPhysicalQueue(primary.index, primary.deviceGeneration));
    EXPECT_LE(accepted.acceptedToken.value, terminal.value);
    if(expectedReuse)
        EXPECT_EQ(accepted.acceptedToken.value, before.acceptedToken.value);
    else
        EXPECT_GT(accepted.acceptedToken.value, before.acceptedToken.value);
    ASSERT_TRUE(device.waitForIdle());
    const void* const bytes = device.mapBuffer(*readback, CpuAccessMode::Read);
    ASSERT_NE(bytes, nullptr);
    const bool bytesMatch = NWB_MEMCMP(bytes, optical.upload->bytes.data(), optical.upload->bytes.size()) == 0;
    device.unmapBuffer(*readback);
    EXPECT_TRUE(bytesMatch) << "actual GPU optical metadata differs from the immutable requested header/instance payload";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferRoundTripTest, OpticalMetadataReuseTracksActualAcceptedGpuContentsAcrossReplacementAndRecreation){
    using namespace __hidden_optical_scene_upload_reuse_tests;
    auto& graphics = s_scope->graphics();
    auto& device = graphics.getDevice();
    Alloc::ScratchArena scratch(Name("tests.optical_upload.native_gather"));
    const auto first = MakeUpload(arena(), scratch, 11u);
    const auto second = MakeUpload(arena(), scratch, 22u);
    const auto rejected = MakeUpload(arena(), scratch, 33u);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(rejected);
    ASSERT_EQ(first->bytes.size(), second->bytes.size());
    ASSERT_EQ(first->bytes.size(), rejected->bytes.size());
    OpticalStorage storage(device, arena());
    ASSERT_TRUE(storage.create(first->bytes.size()));
    const auto readback = device.createBuffer(
        BufferDesc{}
            .setByteSize(first->bytes.size())
            .setCpuAccess(CpuAccessMode::Read)
            .enableAutomaticStateTracking(ResourceStates::Common)
    );
    ASSERT_TRUE(readback);
    {
        SCOPED_TRACE("initial A");
        ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, storage.snapshot(first), readback, false));
    }
    {
        SCOPED_TRACE("unchanged accepted A has no upload");
        ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, storage.snapshot(first), readback, true));
    }
    {
        SCOPED_TRACE("changed B replaces accepted A");
        ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, storage.snapshot(second), readback, false));
    }
    const auto beforeRejected = storage.snapshot(second).uploadState->plan(second);
    ASSERT_TRUE(beforeRejected.reused);
    {
        SCOPED_TRACE("native submission rejects replacement C");
        CopyContext context{.optical = storage.snapshot(rejected), .readback = readback};
        const auto primary = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
        const VkQueue nativeQueue = static_cast<VkQueue>(device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, primary).pointer());
        ASSERT_NE(nativeQueue, VK_NULL_HANDLE);
        VulkanTestQueueSubmit2Observer observer(device);
        ASSERT_TRUE(observer.valid());
        ASSERT_TRUE(observer.armSubmissionFailures(nativeQueue));
        QueueSubmissionToken terminal;
        EXPECT_FALSE(graphics.submitStandaloneTaskGraph(&context, &DeclareOpticalReadback, terminal, primary));
        EXPECT_FALSE(terminal.valid());
        EXPECT_FALSE(context.reused);
        EXPECT_EQ(context.uploadBlobCount, 1u);
        EXPECT_EQ(observer.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(observer.pendingSubmissionFailureCount(), 0u);
        EXPECT_FALSE(graphics.isDeviceRecreationRequested());
    }
    {
        SCOPED_TRACE("rejected C leaves actual B bytes and accepted B identity");
        const auto afterRejected = storage.snapshot(second).uploadState->plan(second);
        ASSERT_TRUE(afterRejected.valid());
        EXPECT_TRUE(afterRejected.reused);
        EXPECT_EQ(afterRejected.acceptedToken.value, beforeRejected.acceptedToken.value);
        EXPECT_FALSE(storage.snapshot(rejected).uploadState->plan(rejected).reused);
        ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, storage.snapshot(second), readback, true));
    }
    {
        SCOPED_TRACE("A reversion must replace B, despite retained A CPU identity");
        ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, storage.snapshot(first), readback, false));
    }
    {
        SCOPED_TRACE("new physical buffer cannot inherit old A residency");
        const auto old = storage.snapshot(first);
        ASSERT_TRUE(storage.create(first->bytes.size()));
        EXPECT_NE(storage.snapshot(first).buffer.get(), old.buffer.get());
        EXPECT_FALSE(old.uploadState->plan(first).valid());
        ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, storage.snapshot(first), readback, false));
        ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, storage.snapshot(first), readback, true));
    }
}

TEST_F(DescriptorBufferRoundTripTest, OpticalSceneOwnerComparesActualGatherBytesBeforeAcceptedReuse){
    using namespace __hidden_optical_scene_upload_reuse_tests;
    auto& graphics = s_scope->graphics();
    auto& device = graphics.getDevice();
    Alloc::ScratchArena scratch(Name("tests.optical_upload.owner_gather"));
    OpticalSceneOwner owner(arena(), graphics);
    Impl::RendererComponent renderer;
    renderer.opticalBoundaryMode = Impl::OpticalBoundaryMode::ClosedPriority;
    renderer.opticalMediumPriority = 10;
    Impl::RayTracingOpticalSceneGather firstGather(scratch, 1u);
    firstGather.append(ECS::EntityID(17u, 3u), renderer, true, {-2.f, -3.f, -4.f}, {2.f, 3.f, 4.f}, true);
    ASSERT_TRUE(owner.resources.prepare(firstGather));
    const auto initial = owner.resources.snapshot();
    ASSERT_TRUE(initial.valid());
    const auto readback = device.createBuffer(
        BufferDesc{}
            .setByteSize(initial.upload->bytes.size())
            .setCpuAccess(CpuAccessMode::Read)
            .enableAutomaticStateTracking(ResourceStates::Common)
    );
    ASSERT_TRUE(readback);
    ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, initial, readback, false));

    // A new gather allocation with byte-identical metadata must preserve the CPU payload identity.
    Impl::RayTracingOpticalSceneGather identicalGather(scratch, 1u);
    identicalGather.append(ECS::EntityID(17u, 3u), renderer, true, {-2.f, -3.f, -4.f}, {2.f, 3.f, 4.f}, true);
    ASSERT_NE(firstGather.instances.data(), identicalGather.instances.data());
    owner.resources.resetPrepared();
    EXPECT_FALSE(owner.resources.snapshot().valid());
    ASSERT_TRUE(owner.resources.prepare(identicalGather));
    const auto identical = owner.resources.snapshot();
    EXPECT_EQ(identical.buffer.get(), initial.buffer.get());
    EXPECT_EQ(identical.upload.get(), initial.upload.get());
    ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, identical, readback, true));

    renderer.opticalMediumPriority = -20;
    Impl::RayTracingOpticalSceneGather changedGather(scratch, 1u);
    changedGather.append(ECS::EntityID(17u, 3u), renderer, true, {-2.f, -3.f, -4.f}, {2.f, 3.f, 4.f}, true);
    ASSERT_TRUE(owner.resources.prepare(changedGather));
    const auto changed = owner.resources.snapshot();
    EXPECT_EQ(changed.buffer.get(), initial.buffer.get());
    EXPECT_NE(changed.upload.get(), initial.upload.get());
    ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, changed, readback, false));

    // A header-only completeness change is a byte change even though instance order and priority are unchanged.
    changedGather.markIncomplete();
    ASSERT_TRUE(owner.resources.prepare(changedGather));
    const auto incomplete = owner.resources.snapshot();
    EXPECT_FALSE(incomplete.boundsComplete);
    EXPECT_NE(incomplete.upload.get(), changed.upload.get());
    EXPECT_EQ(NWB_MEMCMP(
        incomplete.upload->bytes.data() + NWB_RT_OPTICAL_SCENE_HEADER_BYTES,
        changed.upload->bytes.data() + NWB_RT_OPTICAL_SCENE_HEADER_BYTES,
        incomplete.upload->bytes.size() - NWB_RT_OPTICAL_SCENE_HEADER_BYTES
    ), 0);
    ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, incomplete, readback, false));

    ASSERT_TRUE(owner.resources.prepare(firstGather));
    const auto reverted = owner.resources.snapshot();
    EXPECT_TRUE(reverted.boundsComplete);
    EXPECT_NE(reverted.upload.get(), incomplete.upload.get());
    EXPECT_EQ(reverted.upload->bytes.size(), initial.upload->bytes.size());
    EXPECT_EQ(NWB_MEMCMP(reverted.upload->bytes.data(), initial.upload->bytes.data(), initial.upload->bytes.size()), 0);
    ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, reverted, readback, false));
    ASSERT_TRUE(owner.resources.prepare(identicalGather));
    EXPECT_EQ(owner.resources.snapshot().upload.get(), reverted.upload.get());
    ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, owner.resources.snapshot(), readback, true));

    owner.resources.invalidate();
    ASSERT_TRUE(owner.resources.prepare(firstGather));
    const auto recreated = owner.resources.snapshot();
    EXPECT_NE(recreated.buffer.get(), initial.buffer.get());
    EXPECT_NE(recreated.uploadState.get(), initial.uploadState.get());
    EXPECT_FALSE(initial.uploadState->plan(initial.upload).valid());
    ASSERT_NO_FATAL_FAILURE(SubmitAndVerify(graphics, recreated, readback, false));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

