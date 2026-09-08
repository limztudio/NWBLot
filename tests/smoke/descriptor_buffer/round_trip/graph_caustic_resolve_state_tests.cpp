// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_probes_test_support.h"
#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolve prepare reads the photon-written accumulator, geometry cache, and selected ping-pong input as sampled
// textures, then writes the opposite ping-pong target. The callback intentionally has no native state setup.
struct NativePacketCausticResolvePrepareProbeTask{
    struct Payload{
        Texture* accumulator = nullptr;
        u32 layerCount = 0u;
        Texture* geometry = nullptr;
        Texture* input = nullptr;
        Texture* output = nullptr;
        bool* accumulatorReady = nullptr;
        bool* geometryReady = nullptr;
        bool* inputReady = nullptr;
        bool* outputReady = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        bool accumulatorReady = payload.accumulator && payload.layerCount > 0u;
        for(u32 layer = 0u; layer < payload.layerCount; ++layer){
            accumulatorReady = accumulatorReady
                && commandList.getTextureSubresourceState(payload.accumulator, layer, 0u)
                    == ResourceStates::ShaderResource
            ;
        }
        const bool geometryReady =
            payload.geometry
            && commandList.getTextureSubresourceState(payload.geometry, 0u, 0u)
                == ResourceStates::ShaderResource
        ;
        const bool inputReady =
            payload.input
            && commandList.getTextureSubresourceState(payload.input, 0u, 0u)
                == ResourceStates::ShaderResource
        ;
        const bool outputReady =
            payload.output
            && commandList.getTextureSubresourceState(payload.output, 0u, 0u)
                == ResourceStates::UnorderedAccess
        ;
        if(payload.accumulatorReady)
            *payload.accumulatorReady = accumulatorReady;
        if(payload.geometryReady)
            *payload.geometryReady = geometryReady;
        if(payload.inputReady)
            *payload.inputReady = inputReady;
        if(payload.outputReady)
            *payload.outputReady = outputReady;
        const bool ready = accumulatorReady && geometryReady && inputReady && outputReady;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// A graph-owned wavelet pass must see its input as sampled input and its target in UAV state. Five instances prove
// the compiler owns every fixed ping-pong UAV-to-SRV handoff before the native upsample tail begins.
struct NativePacketCausticResolveWaveletProbeTask{
    struct Payload{
        Texture* geometry = nullptr;
        Texture* input = nullptr;
        Texture* output = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.geometry
            && payload.input
            && payload.output
            && commandList.getTextureSubresourceState(payload.geometry, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.input, 0u, 0u)
                == ResourceStates::ShaderResource
            && commandList.getTextureSubresourceState(payload.output, 0u, 0u)
                == ResourceStates::UnorderedAccess
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// The fixed half-B upsample must receive the fifth graph-owned wavelet output as sampled input. This getter-only
// probe verifies the final graph/native handoff without relying on a native state bridge.
struct NativePacketCausticResolveTailProbeTask{
    struct Payload{
        Texture* resolveHalf = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready =
            payload.resolveHalf
            && commandList.getTextureSubresourceState(payload.resolveHalf, 0u, 0u)
                == ResourceStates::ShaderResource
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


// Timing close owns no image state. It must still record in the same packet after graph-owned upsample so the
// renderer can retain its established resolve timing endpoint without reintroducing a native resource bridge.
struct NativePacketCausticResolveTimingCloseProbeTask{
    struct Payload{
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        if(payload.recorded)
            *payload.recorded = true;
        return true;
    }
};


// Caustic photon splats, geometry downsample, resolve prepare, five wavelets, and upsample remain in one native
// packet. Getter-only callbacks prove Vulkan lowers both producer handoffs and every fixed ping-pong transition.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedCausticPhotonGeometryPrepareFiveWaveletAndUpsampleHandoffsRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 layerCount = 3u;
    const TextureHandle accumulator = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setArraySize(layerCount)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::R32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(accumulator.get(), nullptr);
    const TextureHandle geometry = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setDimension(TextureDimension::Texture2D)
            .setFormat(Format::R32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(geometry.get(), nullptr);
    const TextureHandle history = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setDimension(TextureDimension::Texture2D)
            .setFormat(Format::R32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(history.get(), nullptr);
    const TextureHandle resolveHalf = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setDimension(TextureDimension::Texture2D)
            .setFormat(Format::R32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(resolveHalf.get(), nullptr);
    Texture* const initialTextures[] = {
        accumulator.get(),
        geometry.get(),
        history.get(),
        resolveHalf.get(),
    };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::ShaderResource
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importTexture = [&graph](
        const TextureHandle& texture,
        const Name identity,
        const AStringView label
    ){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Texture)
                .setInitialState(ResourceStates::ShaderResource)
        );
    };
    const GpuGraphResourceId accumulatorResource = importTexture(
        accumulator,
        Name("tests/descriptor_buffer/caustic_photon_resolve_accumulator"),
        "Caustic Photon Resolve Accumulator"
    );
    ASSERT_TRUE(accumulatorResource.valid());
    const GpuGraphResourceId geometryResource = importTexture(
        geometry,
        Name("tests/descriptor_buffer/caustic_photon_resolve_geometry"),
        "Caustic Photon Resolve Geometry"
    );
    ASSERT_TRUE(geometryResource.valid());
    const GpuGraphResourceId historyResource = importTexture(
        history,
        Name("tests/descriptor_buffer/caustic_photon_resolve_history"),
        "Caustic Photon Resolve History"
    );
    ASSERT_TRUE(historyResource.valid());
    const GpuGraphResourceId resolveHalfResource = importTexture(
        resolveHalf,
        Name("tests/descriptor_buffer/caustic_photon_resolve_half"),
        "Caustic Photon Resolve Half"
    );
    ASSERT_TRUE(resolveHalfResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const TextureSubresourceSet accumulatorSubresources(0u, 1u, 0u, layerCount);
    const TextureSubresourceSet geometrySubresources(0u, 1u, 0u, 1u);
    const TextureSubresourceSet pingPongSubresources(0u, 1u, 0u, 1u);
    // Persistent caustic scratch comes from the previous accepted frame in the renderer. Declare that immutable
    // state source with each first semantic consumer so the graph snapshots and filters it before compilation.
    CommandListResourceStateHandoff causticScratchState(DescriptorBufferRoundTripTest::arena());
    auto causticScratchProducer = device.createCommandList();
    ASSERT_NE(causticScratchProducer.get(), nullptr);
    causticScratchProducer->open();
    causticScratchProducer->setTextureState(
        accumulator.get(),
        accumulatorSubresources,
        ResourceStates::ShaderResource
    );
    causticScratchProducer->setTextureState(
        geometry.get(),
        geometrySubresources,
        ResourceStates::ShaderResource
    );
    causticScratchProducer->setTextureState(
        history.get(),
        pingPongSubresources,
        ResourceStates::ShaderResource
    );
    causticScratchProducer->setTextureState(
        resolveHalf.get(),
        pingPongSubresources,
        ResourceStates::ShaderResource
    );
    causticScratchProducer->close(&causticScratchState);
    ASSERT_TRUE(causticScratchState.valid());
    const GpuTaskExternalStateSource causticScratchStateSources[] = {
        GpuTaskExternalStateSource{ .states = &causticScratchState },
    };
    const GpuTaskResourceUse photonUses[] = {
        GpuTaskResourceUse{
            .resource = accumulatorResource,
            .range = GpuTaskResourceRange{ .textureSubresources = accumulatorSubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint photonScheduling;
    photonScheduling.cost = GpuTaskCostHint::Large;
    photonScheduling.allowPacketMerge = true;
    GpuTaskDesc photonDesc;
    photonDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_photon_stage"))
        .setMarkerLabel("Caustic Photons")
        .setQueue(graphicsQueue)
        .setScheduling(photonScheduling)
        .setExternalStateSources(causticScratchStateSources, LengthOf(causticScratchStateSources))
        .setResourceUses(photonUses, LengthOf(photonUses))
    ;
    bool photonRecorded = false;
    const GpuTaskId photonTask = graph.addTask<NativePacketCausticAccumulatorDecayProbeTask>(
        photonDesc,
        NativePacketCausticAccumulatorDecayProbeTask::Payload{
            .accumulator = accumulator.get(),
            .layerCount = layerCount,
            .recorded = &photonRecorded,
        }
    );
    ASSERT_TRUE(photonTask.valid());

    const GpuTaskResourceUse geometryUses[] = {
        GpuTaskResourceUse{
            .resource = geometryResource,
            .range = GpuTaskResourceRange{ .textureSubresources = geometrySubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint geometryScheduling = photonScheduling;
    geometryScheduling.mergeWithPrevious = true;
    GpuTaskDesc geometryDesc;
    geometryDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_geometry_stage"))
        .setMarkerLabel("Caustic Geometry")
        .setQueue(graphicsQueue)
        .setScheduling(geometryScheduling)
        .setDependencies(&photonTask, 1u)
        .setExternalStateSources(causticScratchStateSources, LengthOf(causticScratchStateSources))
        .setResourceUses(geometryUses, LengthOf(geometryUses))
    ;
    bool geometryRecorded = false;
    const GpuTaskId geometryTask = graph.addTask<NativePacketCausticAccumulatorDecayProbeTask>(
        geometryDesc,
        NativePacketCausticAccumulatorDecayProbeTask::Payload{
            .accumulator = geometry.get(),
            .layerCount = 1u,
            .recorded = &geometryRecorded,
        }
    );
    ASSERT_TRUE(geometryTask.valid());

    // Five wavelet passes make prepare read resolveHalf and write history. The graph then makes the first wavelet
    // read history and write resolveHalf, the second reads resolveHalf and writes history, the third reads history
    // and writes resolveHalf, and the fourth reads resolveHalf and writes history; the dynamic tail starts after
    // those exact ping-pong handoffs.
    const GpuTaskResourceUse prepareUses[] = {
        GpuTaskResourceUse{
            .resource = accumulatorResource,
            .range = GpuTaskResourceRange{ .textureSubresources = accumulatorSubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = geometryResource,
            .range = GpuTaskResourceRange{ .textureSubresources = geometrySubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = resolveHalfResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = historyResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint prepareScheduling = geometryScheduling;
    prepareScheduling.mergeWithPrevious = true;
    GpuTaskDesc prepareDesc;
    prepareDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_resolve_prepare_stage"))
        .setMarkerLabel("Caustics Resolve Prepare")
        .setQueue(graphicsQueue)
        .setScheduling(prepareScheduling)
        .setDependencies(&geometryTask, 1u)
        .setExternalStateSources(causticScratchStateSources, LengthOf(causticScratchStateSources))
        .setResourceUses(prepareUses, LengthOf(prepareUses))
    ;
    bool prepareAccumulatorReady = false;
    bool prepareGeometryReady = false;
    bool prepareInputReady = false;
    bool prepareOutputReady = false;
    bool prepareRecorded = false;
    const GpuTaskId prepareTask = graph.addTask<NativePacketCausticResolvePrepareProbeTask>(
        prepareDesc,
        NativePacketCausticResolvePrepareProbeTask::Payload{
            .accumulator = accumulator.get(),
            .layerCount = layerCount,
            .geometry = geometry.get(),
            .input = resolveHalf.get(),
            .output = history.get(),
            .accumulatorReady = &prepareAccumulatorReady,
            .geometryReady = &prepareGeometryReady,
            .inputReady = &prepareInputReady,
            .outputReady = &prepareOutputReady,
            .recorded = &prepareRecorded,
        }
    );
    ASSERT_TRUE(prepareTask.valid());

    const GpuTaskResourceUse waveletUses[] = {
        GpuTaskResourceUse{
            .resource = geometryResource,
            .range = GpuTaskResourceRange{ .textureSubresources = geometrySubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = historyResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = resolveHalfResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint waveletScheduling = prepareScheduling;
    waveletScheduling.mergeWithPrevious = true;
    GpuTaskDesc waveletDesc;
    waveletDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_resolve_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Wavelet")
        .setQueue(graphicsQueue)
        .setScheduling(waveletScheduling)
        .setDependencies(&prepareTask, 1u)
        .setResourceUses(waveletUses, LengthOf(waveletUses))
    ;
    bool waveletRecorded = false;
    const GpuTaskId waveletTask = graph.addTask<NativePacketCausticResolveWaveletProbeTask>(
        waveletDesc,
        NativePacketCausticResolveWaveletProbeTask::Payload{
            .geometry = geometry.get(),
            .input = history.get(),
            .output = resolveHalf.get(),
            .recorded = &waveletRecorded,
        }
    );
    ASSERT_TRUE(waveletTask.valid());

    const GpuTaskResourceUse secondWaveletUses[] = {
        GpuTaskResourceUse{
            .resource = geometryResource,
            .range = GpuTaskResourceRange{ .textureSubresources = geometrySubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = resolveHalfResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = historyResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint secondWaveletScheduling = waveletScheduling;
    secondWaveletScheduling.mergeWithPrevious = true;
    GpuTaskDesc secondWaveletDesc;
    secondWaveletDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_resolve_second_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Second Wavelet")
        .setQueue(graphicsQueue)
        .setScheduling(secondWaveletScheduling)
        .setDependencies(&waveletTask, 1u)
        .setResourceUses(secondWaveletUses, LengthOf(secondWaveletUses))
    ;
    bool secondWaveletRecorded = false;
    const GpuTaskId secondWaveletTask = graph.addTask<NativePacketCausticResolveWaveletProbeTask>(
        secondWaveletDesc,
        NativePacketCausticResolveWaveletProbeTask::Payload{
            .geometry = geometry.get(),
            .input = resolveHalf.get(),
            .output = history.get(),
            .recorded = &secondWaveletRecorded,
        }
    );
    ASSERT_TRUE(secondWaveletTask.valid());

    const GpuTaskResourceUse thirdWaveletUses[] = {
        GpuTaskResourceUse{
            .resource = geometryResource,
            .range = GpuTaskResourceRange{ .textureSubresources = geometrySubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = historyResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = resolveHalfResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint thirdWaveletScheduling = secondWaveletScheduling;
    thirdWaveletScheduling.mergeWithPrevious = true;
    GpuTaskDesc thirdWaveletDesc;
    thirdWaveletDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_resolve_third_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Third Wavelet")
        .setQueue(graphicsQueue)
        .setScheduling(thirdWaveletScheduling)
        .setDependencies(&secondWaveletTask, 1u)
        .setResourceUses(thirdWaveletUses, LengthOf(thirdWaveletUses))
    ;
    bool thirdWaveletRecorded = false;
    const GpuTaskId thirdWaveletTask = graph.addTask<NativePacketCausticResolveWaveletProbeTask>(
        thirdWaveletDesc,
        NativePacketCausticResolveWaveletProbeTask::Payload{
            .geometry = geometry.get(),
            .input = history.get(),
            .output = resolveHalf.get(),
            .recorded = &thirdWaveletRecorded,
        }
    );
    ASSERT_TRUE(thirdWaveletTask.valid());

    const GpuTaskResourceUse fourthWaveletUses[] = {
        GpuTaskResourceUse{
            .resource = geometryResource,
            .range = GpuTaskResourceRange{ .textureSubresources = geometrySubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = resolveHalfResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = historyResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint fourthWaveletScheduling = thirdWaveletScheduling;
    fourthWaveletScheduling.mergeWithPrevious = true;
    GpuTaskDesc fourthWaveletDesc;
    fourthWaveletDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_resolve_fourth_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Fourth Wavelet")
        .setQueue(graphicsQueue)
        .setScheduling(fourthWaveletScheduling)
        .setDependencies(&thirdWaveletTask, 1u)
        .setResourceUses(fourthWaveletUses, LengthOf(fourthWaveletUses))
    ;
    bool fourthWaveletRecorded = false;
    const GpuTaskId fourthWaveletTask = graph.addTask<NativePacketCausticResolveWaveletProbeTask>(
        fourthWaveletDesc,
        NativePacketCausticResolveWaveletProbeTask::Payload{
            .geometry = geometry.get(),
            .input = resolveHalf.get(),
            .output = history.get(),
            .recorded = &fourthWaveletRecorded,
        }
    );
    ASSERT_TRUE(fourthWaveletTask.valid());

    const GpuTaskResourceUse fifthWaveletUses[] = {
        GpuTaskResourceUse{
            .resource = geometryResource,
            .range = GpuTaskResourceRange{ .textureSubresources = geometrySubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = historyResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = resolveHalfResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint fifthWaveletScheduling = fourthWaveletScheduling;
    fifthWaveletScheduling.mergeWithPrevious = true;
    GpuTaskDesc fifthWaveletDesc;
    fifthWaveletDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_resolve_fifth_wavelet_stage"))
        .setMarkerLabel("Caustics Resolve Fifth Wavelet")
        .setQueue(graphicsQueue)
        .setScheduling(fifthWaveletScheduling)
        .setDependencies(&fourthWaveletTask, 1u)
        .setResourceUses(fifthWaveletUses, LengthOf(fifthWaveletUses))
    ;
    bool fifthWaveletRecorded = false;
    const GpuTaskId fifthWaveletTask = graph.addTask<NativePacketCausticResolveWaveletProbeTask>(
        fifthWaveletDesc,
        NativePacketCausticResolveWaveletProbeTask::Payload{
            .geometry = geometry.get(),
            .input = history.get(),
            .output = resolveHalf.get(),
            .recorded = &fifthWaveletRecorded,
        }
    );
    ASSERT_TRUE(fifthWaveletTask.valid());

    const GpuTaskResourceUse tailUses[] = {
        GpuTaskResourceUse{
            .resource = resolveHalfResource,
            .range = GpuTaskResourceRange{ .textureSubresources = pingPongSubresources },
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskSchedulingHint tailScheduling = fifthWaveletScheduling;
    tailScheduling.mergeWithPrevious = true;
    GpuTaskDesc tailDesc;
    tailDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_resolve_upsample_stage"))
        .setMarkerLabel("Caustics Resolve Upsample")
        .setQueue(graphicsQueue)
        .setScheduling(tailScheduling)
        .setDependencies(&fifthWaveletTask, 1u)
        .setResourceUses(tailUses, LengthOf(tailUses))
    ;
    bool tailRecorded = false;
    const GpuTaskId tailTask = graph.addTask<NativePacketCausticResolveTailProbeTask>(
        tailDesc,
        NativePacketCausticResolveTailProbeTask::Payload{
            .resolveHalf = resolveHalf.get(),
            .recorded = &tailRecorded,
        }
    );
    ASSERT_TRUE(tailTask.valid());

    GpuTaskSchedulingHint timingCloseScheduling = tailScheduling;
    timingCloseScheduling.mergeWithPrevious = true;
    GpuTaskDesc timingCloseDesc;
    timingCloseDesc
        .setIdentity(Name("tests/descriptor_buffer/caustic_resolve_timing_close"))
        .setMarkerLabel("Caustics Resolve Timing Close")
        .setQueue(graphicsQueue)
        .setScheduling(timingCloseScheduling)
        .setDependencies(&tailTask, 1u)
    ;
    bool timingCloseRecorded = false;
    const GpuTaskId timingCloseTask = graph.addTask<NativePacketCausticResolveTimingCloseProbeTask>(
        timingCloseDesc,
        NativePacketCausticResolveTimingCloseProbeTask::Payload{ .recorded = &timingCloseRecorded }
    );
    ASSERT_TRUE(timingCloseTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/caustic_photon_resolve_prepare_wavelet_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId photonPacket = views.compiled.packetForTask(photonTask);
    const GpuSubmissionPacketId geometryPacket = views.compiled.packetForTask(geometryTask);
    const GpuSubmissionPacketId preparePacket = views.compiled.packetForTask(prepareTask);
    const GpuSubmissionPacketId waveletPacket = views.compiled.packetForTask(waveletTask);
    const GpuSubmissionPacketId secondWaveletPacket = views.compiled.packetForTask(secondWaveletTask);
    const GpuSubmissionPacketId thirdWaveletPacket = views.compiled.packetForTask(thirdWaveletTask);
    const GpuSubmissionPacketId fourthWaveletPacket = views.compiled.packetForTask(fourthWaveletTask);
    const GpuSubmissionPacketId fifthWaveletPacket = views.compiled.packetForTask(fifthWaveletTask);
    const GpuSubmissionPacketId tailPacket = views.compiled.packetForTask(tailTask);
    const GpuSubmissionPacketId timingClosePacket = views.compiled.packetForTask(timingCloseTask);
    ASSERT_TRUE(photonPacket.valid());
    ASSERT_TRUE(geometryPacket.valid());
    ASSERT_TRUE(preparePacket.valid());
    ASSERT_TRUE(waveletPacket.valid());
    ASSERT_TRUE(secondWaveletPacket.valid());
    ASSERT_TRUE(thirdWaveletPacket.valid());
    ASSERT_TRUE(fourthWaveletPacket.valid());
    ASSERT_TRUE(fifthWaveletPacket.valid());
    ASSERT_TRUE(tailPacket.valid());
    ASSERT_TRUE(timingClosePacket.valid());
    EXPECT_EQ(geometryPacket, photonPacket);
    EXPECT_EQ(preparePacket, photonPacket);
    EXPECT_EQ(waveletPacket, photonPacket);
    EXPECT_EQ(secondWaveletPacket, photonPacket);
    EXPECT_EQ(thirdWaveletPacket, photonPacket);
    EXPECT_EQ(fourthWaveletPacket, photonPacket);
    EXPECT_EQ(fifthWaveletPacket, photonPacket);
    EXPECT_EQ(tailPacket, photonPacket);
    EXPECT_EQ(timingClosePacket, photonPacket);

    const GpuCompiledTaskView compiledPrepare = views.compiled.findTask(prepareTask);
    const GpuCompiledTaskView compiledWavelet = views.compiled.findTask(waveletTask);
    const GpuCompiledTaskView compiledSecondWavelet = views.compiled.findTask(secondWaveletTask);
    const GpuCompiledTaskView compiledThirdWavelet = views.compiled.findTask(thirdWaveletTask);
    const GpuCompiledTaskView compiledFourthWavelet = views.compiled.findTask(fourthWaveletTask);
    const GpuCompiledTaskView compiledFifthWavelet = views.compiled.findTask(fifthWaveletTask);
    const GpuCompiledTaskView compiledTail = views.compiled.findTask(tailTask);
    ASSERT_TRUE(compiledPrepare.valid());
    ASSERT_TRUE(compiledWavelet.valid());
    ASSERT_TRUE(compiledSecondWavelet.valid());
    ASSERT_TRUE(compiledThirdWavelet.valid());
    ASSERT_TRUE(compiledFourthWavelet.valid());
    ASSERT_TRUE(compiledFifthWavelet.valid());
    ASSERT_TRUE(compiledTail.valid());
    const GpuCompiledBarrier* const prepareBarriers = views.compiled.findTask(prepareTask).prologueBarriers;
    const GpuCompiledBarrier* const waveletBarriers = views.compiled.findTask(waveletTask).prologueBarriers;
    const GpuCompiledBarrier* const secondWaveletBarriers = views.compiled.findTask(secondWaveletTask).prologueBarriers;
    const GpuCompiledBarrier* const thirdWaveletBarriers = views.compiled.findTask(thirdWaveletTask).prologueBarriers;
    const GpuCompiledBarrier* const fourthWaveletBarriers = views.compiled.findTask(fourthWaveletTask).prologueBarriers;
    const GpuCompiledBarrier* const fifthWaveletBarriers = views.compiled.findTask(fifthWaveletTask).prologueBarriers;
    const GpuCompiledBarrier* const tailBarriers = views.compiled.findTask(tailTask).prologueBarriers;
    ASSERT_NE(prepareBarriers, nullptr);
    ASSERT_NE(waveletBarriers, nullptr);
    ASSERT_NE(secondWaveletBarriers, nullptr);
    ASSERT_NE(thirdWaveletBarriers, nullptr);
    ASSERT_NE(fourthWaveletBarriers, nullptr);
    ASSERT_NE(fifthWaveletBarriers, nullptr);
    ASSERT_NE(tailBarriers, nullptr);
    bool hasAccumulatorHandoff = false;
    bool hasGeometryHandoff = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledPrepare.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = prepareBarriers[barrierIndex];
        hasAccumulatorHandoff = hasAccumulatorHandoff || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == accumulatorResource
            && barrier.range.textureSubresources == accumulatorSubresources
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::ShaderResource
        );
        hasGeometryHandoff = hasGeometryHandoff || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == geometryResource
            && barrier.range.textureSubresources == geometrySubresources
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasAccumulatorHandoff);
    EXPECT_TRUE(hasGeometryHandoff);

    bool hasPrepareWaveletHandoff = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledWavelet.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = waveletBarriers[barrierIndex];
        hasPrepareWaveletHandoff = hasPrepareWaveletHandoff || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == historyResource
            && barrier.range.textureSubresources == pingPongSubresources
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasPrepareWaveletHandoff);

    bool hasWaveletSecondHandoff = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledSecondWavelet.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = secondWaveletBarriers[barrierIndex];
        hasWaveletSecondHandoff = hasWaveletSecondHandoff || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalfResource
            && barrier.range.textureSubresources == pingPongSubresources
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasWaveletSecondHandoff);

    bool hasSecondThirdHandoff = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledThirdWavelet.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = thirdWaveletBarriers[barrierIndex];
        hasSecondThirdHandoff = hasSecondThirdHandoff || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == historyResource
            && barrier.range.textureSubresources == pingPongSubresources
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasSecondThirdHandoff);

    bool hasThirdFourthHandoff = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledFourthWavelet.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = fourthWaveletBarriers[barrierIndex];
        hasThirdFourthHandoff = hasThirdFourthHandoff || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalfResource
            && barrier.range.textureSubresources == pingPongSubresources
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasThirdFourthHandoff);

    bool hasFourthFifthHandoff = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledFifthWavelet.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = fifthWaveletBarriers[barrierIndex];
        hasFourthFifthHandoff = hasFourthFifthHandoff || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == historyResource
            && barrier.range.textureSubresources == pingPongSubresources
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasFourthFifthHandoff);

    bool hasFifthUpsampleHandoff = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledTail.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = tailBarriers[barrierIndex];
        hasFifthUpsampleHandoff = hasFifthUpsampleHandoff || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == resolveHalfResource
            && barrier.range.textureSubresources == pingPongSubresources
            && barrier.before == ResourceStates::UnorderedAccess
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(hasFifthUpsampleHandoff);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    const bool packetRecorded = recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket
    );
    EXPECT_TRUE(packetRecorded) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(photonRecorded);
    EXPECT_TRUE(geometryRecorded);
    EXPECT_TRUE(prepareAccumulatorReady);
    EXPECT_TRUE(prepareGeometryReady);
    EXPECT_TRUE(prepareInputReady);
    EXPECT_TRUE(prepareOutputReady);
    EXPECT_TRUE(prepareRecorded);
    EXPECT_TRUE(waveletRecorded);
    EXPECT_TRUE(secondWaveletRecorded);
    EXPECT_TRUE(thirdWaveletRecorded);
    EXPECT_TRUE(fourthWaveletRecorded);
    EXPECT_TRUE(fifthWaveletRecorded);
    EXPECT_TRUE(tailRecorded);
    EXPECT_TRUE(timingCloseRecorded);
    if(!packetRecorded)
        return;

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.packetToken(photonPacket).valid());
    EXPECT_TRUE(transaction.packetToken(geometryPacket).valid());
    EXPECT_TRUE(transaction.packetToken(preparePacket).valid());
    EXPECT_TRUE(transaction.packetToken(waveletPacket).valid());
    EXPECT_TRUE(transaction.packetToken(secondWaveletPacket).valid());
    EXPECT_TRUE(transaction.packetToken(thirdWaveletPacket).valid());
    EXPECT_TRUE(transaction.packetToken(fourthWaveletPacket).valid());
    EXPECT_TRUE(transaction.packetToken(fifthWaveletPacket).valid());
    EXPECT_TRUE(transaction.packetToken(tailPacket).valid());
    EXPECT_TRUE(transaction.packetToken(timingClosePacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

