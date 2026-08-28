// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Phase 11 optional-command-IR CPU probe for the copy-buffer record shape. This is intentionally a standalone
// headless executable rather than a renderer benchmark: it compares ordinary native packet recording with the
// explicit capture seam, then measures POD decoding, graph-aware preflight, Core::CommandList lowering, and the
// experimental direct-Vulkan CopyBuffer lowerer separately. The default renderer path never creates this capture
// object, so this tool reports opt-in tooling cost only; texture-copy and clear opcode shapes require their own
// explicitly labelled corpora.


#include <global/global.h>

#include <core/common/application_entry.h>
#include <core/common/module.h>
#include <core/alloc/general.h>
#include <core/alloc/job.h>
#include <core/alloc/thread.h>
#include <core/graphics/api.h>
#include <core/graphics/capture/command_ir.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
#include <core/graphics/vulkan/backend.h>
#include <core/perf/timing.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>
#include <tests/common/capturing_logger.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CommandIrProfile{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


inline constexpr int s_SkipExitCode = 77;
inline constexpr u32 s_DefaultRecordCount = 4096u;
inline constexpr u32 s_DefaultWarmupCount = 3u;
inline constexpr u32 s_DefaultSampleCount = 11u;
inline constexpr u32 s_MaxRecordCount = 65536u;
inline constexpr u32 s_MaxSampleCount = 64u;
inline constexpr int s_F64DecimalRoundTripPrecision = 17;
inline constexpr u64 s_ChecksumHashSeed = 1469598103934665603ull;


struct Arguments{
    i32 adapterIndex = 0;
    u32 recordCount = s_DefaultRecordCount;
    u32 warmupCount = s_DefaultWarmupCount;
    u32 sampleCount = s_DefaultSampleCount;
    bool gpuValidation = true;
};

struct TimingSamples{
    f64 values[s_MaxSampleCount] = {};
    u32 count = 0u;

    [[nodiscard]] bool append(const f64 value)noexcept{
        if(count >= LengthOf(values))
            return false;
        values[count++] = value;
        return true;
    }
};

struct TimingSummary{
    f64 minimum = 0.0;
    f64 median = 0.0;
    f64 maximum = 0.0;
};

struct Result{
    const char* status = "failed";
    const char* reason = "unknown";
    const char* workload = "copy_buffer";
    i32 requestedAdapterIndex = 0;
    u32 selectedAdapterVendorID = 0u;
    u32 selectedAdapterDeviceID = 0u;
    AdapterInfo::UUID selectedAdapterUUID = {};
    bool selectedAdapterHasUUID = false;
    u32 graphicsFamily = Limit<u32>::s_Max;
    u32 recordCount = 0u;
    u32 warmupCount = 0u;
    u32 sampleCount = 0u;
    u64 streamBytes = 0u;
    u64 payloadBytes = 0u;
    u64 decodedRecords = 0u;
    u64 preflightRecords = 0u;
    u64 replayedRecords = 0u;
    u64 directVulkanReplayedRecords = 0u;
    u64 captureAllocationDelta = 0u;
    u64 captureReallocationDelta = 0u;
    u64 expectedHash = 0u;
    u64 observedHash = 0u;
    u64 directVulkanObservedHash = 0u;
    bool streamValid = false;
    bool checksumVerified = false;
    bool directVulkanChecksumVerified = false;
    TimingSamples nativeRecord;
    TimingSamples captureRecord;
    TimingSamples readerDecode;
    TimingSamples preflight;
    TimingSamples replay;
    TimingSamples directVulkanReplay;
    u32 loggerErrors = 0u;
};


[[nodiscard]] static bool ParseUnsigned(const char* const value, u32& outValue){
    if(!value || !*value)
        return false;

    u64 parsed = 0u;
    if(!ParseU64(AStringView(value), parsed) || parsed > static_cast<u64>(Limit<u32>::s_Max))
        return false;

    outValue = static_cast<u32>(parsed);
    return true;
}

[[nodiscard]] static bool ParseAdapterIndex(const char* const value, i32& outValue){
    if(!value || !*value)
        return false;

    i64 parsed = 0;
    if(!ParseI64(AStringView(value), parsed) || parsed < 0 || parsed > static_cast<i64>(Limit<i32>::s_Max))
        return false;

    outValue = static_cast<i32>(parsed);
    return true;
}

[[nodiscard]] static bool ParseArguments(const int argc, char** argv, Arguments& outArguments){
    for(int index = 1; index < argc; ++index){
        const char* const argument = argv[index];
        if(NWB_STRCMP(argument, "--adapter-index") == 0){
            if(++index >= argc || !ParseAdapterIndex(argv[index], outArguments.adapterIndex))
                return false;
        }
        else if(NWB_STRCMP(argument, "--records") == 0){
            if(++index >= argc || !ParseUnsigned(argv[index], outArguments.recordCount))
                return false;
        }
        else if(NWB_STRCMP(argument, "--warmup") == 0){
            if(++index >= argc || !ParseUnsigned(argv[index], outArguments.warmupCount))
                return false;
        }
        else if(NWB_STRCMP(argument, "--samples") == 0){
            if(++index >= argc || !ParseUnsigned(argv[index], outArguments.sampleCount))
                return false;
        }
        else if(NWB_STRCMP(argument, "--gpu-validation") == 0)
            outArguments.gpuValidation = true;
        else if(NWB_STRCMP(argument, "--no-gpu-validation") == 0)
            outArguments.gpuValidation = false;
        else if(NWB_STRCMP(argument, "--help") == 0 || NWB_STRCMP(argument, "-h") == 0){
            NWB_COUT
                << "Usage: command_ir_profile [--adapter-index N] [--records N] [--warmup N] [--samples N] "
                << "[--gpu-validation|--no-gpu-validation]\n"
            ;
            return false;
        }
        else
            return false;
    }

    return outArguments.recordCount != 0u
        && outArguments.recordCount <= s_MaxRecordCount
        && outArguments.warmupCount != 0u
        && outArguments.sampleCount != 0u
        && outArguments.sampleCount <= s_MaxSampleCount
    ;
}

[[nodiscard]] static TimingSummary Summarize(const TimingSamples& samples){
    TimingSummary summary;
    if(samples.count == 0u)
        return summary;

    f64 ordered[s_MaxSampleCount] = {};
    for(u32 index = 0u; index < samples.count; ++index)
        ordered[index] = samples.values[index];
    for(u32 index = 1u; index < samples.count; ++index){
        const f64 value = ordered[index];
        u32 insertion = index;
        while(insertion > 0u && ordered[insertion - 1u] > value){
            ordered[insertion] = ordered[insertion - 1u];
            --insertion;
        }
        ordered[insertion] = value;
    }

    summary.minimum = ordered[0u];
    summary.maximum = ordered[samples.count - 1u];
    const u32 middle = samples.count / 2u;
    summary.median = (samples.count & 1u) != 0u
        ? ordered[middle]
        : (ordered[middle - 1u] + ordered[middle]) * 0.5
    ;
    return summary;
}

[[nodiscard]] static bool CaptureSelectedAdapterIdentity(
    Graphics& graphics,
    GraphicsAllocator& allocator,
    Result& outResult
){
    AdapterInfo selectedAdapter(allocator.getObjectArena());
    if(!graphics.getSelectedAdapterInfo(selectedAdapter) || !selectedAdapter.hasUUID)
        return false;

    outResult.selectedAdapterVendorID = selectedAdapter.vendorID;
    outResult.selectedAdapterDeviceID = selectedAdapter.deviceID;
    outResult.selectedAdapterUUID = selectedAdapter.uuid;
    outResult.selectedAdapterHasUUID = true;
    return true;
}

[[nodiscard]] static bool RecordPacket(
    const GpuNativePacketRecorder& recorder,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    GpuRecordedGraph& recordedGraph,
    GpuCommandIrCapture* const capture
){
    recordedGraph.reset(compiledGraph);
    return recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = packet, .packetCount = 1u },
        recordedGraph,
        nullptr,
        capture
    );
}

[[nodiscard]] static bool DiscardRecordedPacket(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    GpuRecordedGraph& recordedGraph
){
    if(!compiledGraph.validPacket(packet) || !recordedGraph.validFor(graph, compiledGraph))
        return false;
    if(!graph.discardUnacceptedPacket(compiledGraph, packet, recordedGraph.recordingAttemptGeneration()))
        return false;

    // Discard resolves the graph's exact recording attempt; reset then releases the unsubmitted native artifact.
    recordedGraph.reset(compiledGraph);
    return true;
}

[[nodiscard]] static bool DecodeRecords(const BinaryByteView bytes, const u64 expectedCount, u64& outCount){
    outCount = 0u;
    GpuCommandIrStreamReader reader(bytes);
    if(reader.validation().failed())
        return false;

    GpuCommandIrBuiltinTaskRecord record;
    for(;;){
        switch(reader.next(record)){
        case GpuCommandIrStreamReadStatus::Record:
            ++outCount;
            break;
        case GpuCommandIrStreamReadStatus::End:
            return reader.validation().valid() && outCount == expectedCount;
        case GpuCommandIrStreamReadStatus::Error:
            return false;
        default:
            return false;
        }
    }
}

[[nodiscard]] static bool PreflightRecords(
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const u64 expectedCount,
    u64& outCount
){
    outCount = 0u;
    const GpuCommandIrReplayResult replay = PreflightGpuCommandIrPacket(bytes, graph, compiledGraph, packet);
    if(!replay.valid() || !replay.streamValidation.valid())
        return false;
    outCount = replay.recordIndex;
    return outCount == expectedCount;
}

[[nodiscard]] static CommandListHandle CreatePacketCommandList(
    Device& device,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet
){
    if(!compiledGraph.validPacket(packet))
        return {};
    const GpuPhysicalQueueId packetQueue = compiledGraph.packet(packet).queue;
    if(!compiledGraph.queueInfo(packetQueue))
        return {};

    CommandListParameters parameters;
    parameters.setPhysicalQueue(packetQueue);
    return device.createCommandList(parameters);
}

[[nodiscard]] static bool ReplayRecords(
    Device& device,
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const u64 expectedCount,
    u64& outCount,
    f64* const outNanoseconds
){
    outCount = 0u;
    if(outNanoseconds)
        *outNanoseconds = 0.0;

    CommandListHandle commandList = CreatePacketCommandList(device, compiledGraph, packet);
    if(!commandList)
        return false;
    commandList->open();
    if(!commandList->isRecording())
        return false;

    const Timer begin = TimerNow();
    const GpuCommandIrReplayResult replay = ReplayGpuCommandIrPacket(bytes, graph, compiledGraph, packet, *commandList);
    const Timer end = TimerNow();
    commandList->close();
    if(!replay.valid() || !replay.streamValidation.valid())
        return false;

    outCount = replay.recordIndex;
    if(outNanoseconds)
        *outNanoseconds = DurationInNS<f64>(end, begin);
    return outCount == expectedCount;
}

[[nodiscard]] static bool PrepareDirectVulkanReplayState(
    CommandList& commandList,
    Buffer* const source,
    Buffer* const destination
){
    // The graph recorder normally lowers these exact packet transitions before a task body. Keep that required
    // setup outside the direct-lowering timing window: this probe isolates reader/preflight plus raw vkCmdCopyBuffer
    // emission, not graph barrier lowering.
    if(!commandList.isRecording() || commandList.isRenderPassActive() || !source || !destination)
        return false;
    commandList.setBufferState(source, ResourceStates::CopySource);
    commandList.setBufferState(destination, ResourceStates::CopyDest);
    commandList.commitBarriers();
    return commandList.isRecording();
}

[[nodiscard]] static bool ReplayDirectVulkanRecords(
    Device& device,
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const u64 expectedCount,
    Buffer* const source,
    Buffer* const destination,
    u64& outCount,
    f64* const outNanoseconds
){
    outCount = 0u;
    if(outNanoseconds)
        *outNanoseconds = 0.0;

    CommandListHandle commandList = CreatePacketCommandList(device, compiledGraph, packet);
    if(!commandList)
        return false;
    commandList->open();
    if(!PrepareDirectVulkanReplayState(*commandList, source, destination))
        return false;

    const Timer begin = TimerNow();
    const GpuCommandIrReplayResult replay = ReplayGpuCommandIrPacketDirectVulkan(
        bytes,
        graph,
        compiledGraph,
        packet,
        *commandList
    );
    const Timer end = TimerNow();
    commandList->close();
    if(!replay.valid() || !replay.streamValidation.valid())
        return false;

    outCount = replay.recordIndex;
    if(outNanoseconds)
        *outNanoseconds = DurationInNS<f64>(end, begin);
    return outCount == expectedCount;
}

[[nodiscard]] static bool VerifyReplay(
    Device& device,
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const u64 expectedCount,
    Buffer* const destination,
    const usize byteCount,
    Result& outResult
){
    CommandListHandle commandList = CreatePacketCommandList(device, compiledGraph, packet);
    if(!commandList)
        return false;
    commandList->open();
    if(!commandList->isRecording())
        return false;

    const GpuCommandIrReplayResult replay = ReplayGpuCommandIrPacket(bytes, graph, compiledGraph, packet, *commandList);
    commandList->close();
    if(!replay.valid() || !replay.streamValidation.valid() || replay.recordIndex != expectedCount)
        return false;

    CommandList* const commandLists[] = { commandList.get() };
    const GpuPhysicalQueueId packetQueue = compiledGraph.packet(packet).queue;
    bool submitted = false;
    if(
        device.executeCommandLists(commandLists, LengthOf(commandLists), packetQueue, &submitted) == 0u
        || !submitted
    )
        return false;
    if(!device.waitForIdle())
        return false;

    const auto* const bytesRead = static_cast<const u8*>(device.mapBuffer(destination, CpuAccessMode::Read));
    if(!bytesRead)
        return false;
    outResult.observedHash = UpdateFnv64(s_ChecksumHashSeed, bytesRead, byteCount);
    device.unmapBuffer(destination);
    outResult.checksumVerified = outResult.observedHash == outResult.expectedHash;
    return outResult.checksumVerified;
}

[[nodiscard]] static bool VerifyDirectVulkanReplay(
    Device& device,
    const BinaryByteView bytes,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const u64 expectedCount,
    Buffer* const source,
    Buffer* const destination,
    const usize byteCount,
    Result& outResult
){
    CommandListHandle commandList = CreatePacketCommandList(device, compiledGraph, packet);
    if(!commandList)
        return false;
    commandList->open();
    if(!PrepareDirectVulkanReplayState(*commandList, source, destination))
        return false;

    const GpuCommandIrReplayResult replay = ReplayGpuCommandIrPacketDirectVulkan(
        bytes,
        graph,
        compiledGraph,
        packet,
        *commandList
    );
    commandList->close();
    if(!replay.valid() || !replay.streamValidation.valid() || replay.recordIndex != expectedCount)
        return false;

    CommandList* const commandLists[] = { commandList.get() };
    const GpuPhysicalQueueId packetQueue = compiledGraph.packet(packet).queue;
    bool submitted = false;
    if(
        device.executeCommandLists(commandLists, LengthOf(commandLists), packetQueue, &submitted) == 0u
        || !submitted
    )
        return false;
    if(!device.waitForIdle())
        return false;

    const auto* const bytesRead = static_cast<const u8*>(device.mapBuffer(destination, CpuAccessMode::Read));
    if(!bytesRead)
        return false;
    outResult.directVulkanObservedHash = UpdateFnv64(s_ChecksumHashSeed, bytesRead, byteCount);
    device.unmapBuffer(destination);
    outResult.directVulkanChecksumVerified = outResult.directVulkanObservedHash == outResult.expectedHash;
    return outResult.directVulkanChecksumVerified;
}

[[nodiscard]] static bool RunProfile(
    Graphics& graphics,
    Alloc::GlobalArena& arena,
    const Arguments& arguments,
    Result& outResult
){
    auto& device = graphics.getDevice();
    static constexpr u32 s_SourceWords[] = {
        0x0badf00du,
        0xcafebabeu,
        0x7143a9d2u,
        0xdecafbadU,
    };
    const BufferDesc sourceDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    const BufferDesc destinationDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    BufferHandle source = device.createBuffer(sourceDesc);
    BufferHandle destination = device.createBuffer(destinationDesc);
    if(!source || !destination)
        return false;

    auto* const sourceWords = static_cast<u32*>(device.mapBuffer(source.get(), CpuAccessMode::Write));
    if(!sourceWords)
        return false;
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        sourceWords[wordIndex] = s_SourceWords[wordIndex];
    device.unmapBuffer(source.get());
    outResult.expectedHash = UpdateFnv64(s_ChecksumHashSeed, reinterpret_cast<const u8*>(s_SourceWords), sizeof(s_SourceWords));

    GpuTaskGraph graph(arena);
    const GpuGraphResourceId sourceResource = graph.importBuffer(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/ab/command_ir/source"))
            .setMarkerLabel("Command IR Profile Source")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/ab/command_ir/destination"))
            .setMarkerLabel("Command IR Profile Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    if(!sourceResource.valid() || !destinationResource.valid())
        return false;

    GraphicsVector<GpuCopyBufferTaskRegion> regions(arena);
    regions.resize(arguments.recordCount);
    for(GpuCopyBufferTaskRegion& region : regions){
        region.source = sourceResource;
        region.destination = destinationResource;
        region.dataSizeBytes = sizeof(s_SourceWords);
    }
    GpuTaskDesc copyDesc;
    copyDesc
        .setIdentity(Name("tests/ab/command_ir/copy"))
        .setMarkerLabel("Command IR Profile Copy")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const GpuTaskId copyTask = graph.addCopyBufferTask(
        copyDesc,
        GpuCopyBufferTaskDesc{
            .regions = regions.data(),
            .regionCount = regions.size(),
        }
    );
    if(!copyTask.valid())
        return false;

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    if(!device.getQueue(CommandQueue::Graphics) || graphicsFamily == Limit<u32>::s_Max)
        return false;
    const GpuPhysicalQueueInfo queue{
        .id = GpuPhysicalQueueId{
            device.getPhysicalQueueIndex(CommandQueue::Graphics),
            device.getDeviceGeneration(),
        },
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = graphicsFamily,
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(arena);
    GpuTaskGraphQueueAssignments assignments(arena);
    GpuCompiledGraph compiledGraph(arena);
    Alloc::ScratchArena scratchArena(Name("tests/ab/command_ir/scratch"));
    const GpuTaskGraphCompiler compiler;
    if(!compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena))
        return false;
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(copyTask);
    if(!packet.valid() || compiledGraph.packet(packet).queue != queue.id)
        return false;

    const GpuNativePacketRecorder recorder(device);
    GpuRecordedGraph nativeRecordedGraph(arena);
    GpuRecordedGraph captureRecordedGraph(arena);
    Alloc::GlobalArena captureArena(Name("tests/ab/command_ir/capture_arena"));
    GpuCommandIrCapture capture(captureArena);
    const auto warmup = [&]{
        if(!RecordPacket(recorder, graph, compiledGraph, packet, nativeRecordedGraph, nullptr))
            return false;
        if(!DiscardRecordedPacket(graph, compiledGraph, packet, nativeRecordedGraph))
            return false;
        capture.reset();
        if(!RecordPacket(recorder, graph, compiledGraph, packet, captureRecordedGraph, &capture))
            return false;
        if(!DiscardRecordedPacket(graph, compiledGraph, packet, captureRecordedGraph))
            return false;
        if(capture.recordCount() != arguments.recordCount)
            return false;
        u64 decoded = 0u;
        u64 preflight = 0u;
        u64 replayed = 0u;
        u64 directVulkanReplayed = 0u;
        return DecodeRecords(capture.commandBytes(), arguments.recordCount, decoded)
            && PreflightRecords(capture.commandBytes(), graph, compiledGraph, packet, arguments.recordCount, preflight)
            && ReplayRecords(
                device,
                capture.commandBytes(),
                graph,
                compiledGraph,
                packet,
                arguments.recordCount,
                replayed,
                nullptr
            )
            && ReplayDirectVulkanRecords(
                device,
                capture.commandBytes(),
                graph,
                compiledGraph,
                packet,
                arguments.recordCount,
                source.get(),
                destination.get(),
                directVulkanReplayed,
                nullptr
            )
        ;
    };
    for(u32 warmupIndex = 0u; warmupIndex < arguments.warmupCount; ++warmupIndex){
        if(!warmup())
            return false;
    }

    const ArenaMemoryStats captureBefore = captureArena.memoryStats();
    const auto recordNativeSample = [&]{
        const Timer nativeBegin = TimerNow();
        if(!RecordPacket(recorder, graph, compiledGraph, packet, nativeRecordedGraph, nullptr))
            return false;
        const bool appended = outResult.nativeRecord.append(
            DurationInNS<f64>(TimerNow(), nativeBegin) / static_cast<f64>(arguments.recordCount)
        );
        if(!DiscardRecordedPacket(graph, compiledGraph, packet, nativeRecordedGraph))
            return false;
        return appended;
    };
    const auto recordCaptureSample = [&]{
        const Timer captureBegin = TimerNow();
        // Reset is the per-capture artifact lifecycle counterpart to GpuRecordedGraph::reset in the direct arm.
        // Keep it inside the paired timing so the reported increment is a full steady-state capture cost.
        capture.reset();
        if(!RecordPacket(recorder, graph, compiledGraph, packet, captureRecordedGraph, &capture))
            return false;
        const bool appended = outResult.captureRecord.append(
            DurationInNS<f64>(TimerNow(), captureBegin) / static_cast<f64>(arguments.recordCount)
        );
        if(!DiscardRecordedPacket(graph, compiledGraph, packet, captureRecordedGraph))
            return false;
        if(capture.recordCount() != arguments.recordCount)
            return false;
        return appended;
    };
    for(u32 sampleIndex = 0u; sampleIndex < arguments.sampleCount; ++sampleIndex){
        // Alternate arm order so a fixed cache/pool order cannot systematically favor direct or capture recording.
        if(
            ((sampleIndex & 1u) == 0u && (!recordNativeSample() || !recordCaptureSample()))
            || ((sampleIndex & 1u) != 0u && (!recordCaptureSample() || !recordNativeSample()))
        )
            return false;

        const Timer decodeBegin = TimerNow();
        if(!DecodeRecords(capture.commandBytes(), arguments.recordCount, outResult.decodedRecords))
            return false;
        if(!outResult.readerDecode.append(
            DurationInNS<f64>(TimerNow(), decodeBegin) / static_cast<f64>(arguments.recordCount)
        ))
            return false;

        const Timer preflightBegin = TimerNow();
        if(!PreflightRecords(
            capture.commandBytes(),
            graph,
            compiledGraph,
            packet,
            arguments.recordCount,
            outResult.preflightRecords
        ))
            return false;
        if(!outResult.preflight.append(
            DurationInNS<f64>(TimerNow(), preflightBegin) / static_cast<f64>(arguments.recordCount)
        ))
            return false;

        f64 replayNanoseconds = 0.0;
        if(!ReplayRecords(
            device,
            capture.commandBytes(),
            graph,
            compiledGraph,
            packet,
            arguments.recordCount,
            outResult.replayedRecords,
            &replayNanoseconds
        ))
            return false;
        if(!outResult.replay.append(replayNanoseconds / static_cast<f64>(arguments.recordCount)))
            return false;

        f64 directVulkanReplayNanoseconds = 0.0;
        if(!ReplayDirectVulkanRecords(
            device,
            capture.commandBytes(),
            graph,
            compiledGraph,
            packet,
            arguments.recordCount,
            source.get(),
            destination.get(),
            outResult.directVulkanReplayedRecords,
            &directVulkanReplayNanoseconds
        ))
            return false;
        if(!outResult.directVulkanReplay.append(
            directVulkanReplayNanoseconds / static_cast<f64>(arguments.recordCount)
        ))
            return false;
    }
    const ArenaMemoryStats captureAfter = captureArena.memoryStats();
    outResult.captureAllocationDelta = captureAfter.allocationCount - captureBefore.allocationCount;
    outResult.captureReallocationDelta = captureAfter.reallocationCount - captureBefore.reallocationCount;
    outResult.streamBytes = capture.commandBytes().size();
    outResult.payloadBytes = outResult.streamBytes >= sizeof(GpuCommandIrStreamHeader)
        ? outResult.streamBytes - sizeof(GpuCommandIrStreamHeader)
        : 0u
    ;
    outResult.streamValid = ValidateGpuCommandIrStream(capture.commandBytes()).valid();
    const u64 expectedPayloadBytes = static_cast<u64>(arguments.recordCount)
        * static_cast<u64>(sizeof(GpuCommandIrCopyBufferRecord))
    ;
    if(
        !outResult.streamValid
        || outResult.payloadBytes != expectedPayloadBytes
        || outResult.streamBytes != sizeof(GpuCommandIrStreamHeader) + expectedPayloadBytes
        || outResult.captureAllocationDelta != 0u
        || outResult.captureReallocationDelta != 0u
        || outResult.decodedRecords != arguments.recordCount
        || outResult.preflightRecords != arguments.recordCount
        || outResult.replayedRecords != arguments.recordCount
        || outResult.directVulkanReplayedRecords != arguments.recordCount
    )
        return false;

    return VerifyReplay(
        device,
        capture.commandBytes(),
        graph,
        compiledGraph,
        packet,
        arguments.recordCount,
        destination.get(),
        sizeof(s_SourceWords),
        outResult
    ) && VerifyDirectVulkanReplay(
        device,
        capture.commandBytes(),
        graph,
        compiledGraph,
        packet,
        arguments.recordCount,
        source.get(),
        destination.get(),
        sizeof(s_SourceWords),
        outResult
    );
}

static void EmitTimingSamples(const TimingSamples& samples){
    const TimingSummary summary = Summarize(samples);
    // The runner recomputes aggregate values from these samples, so retain enough precision for a lossless f64
    // round trip (including an even-sized median that averages two samples).
    NWB_COUT.precision(s_F64DecimalRoundTripPrecision);
    NWB_COUT << "{\"samples_ns_per_command\":[";
    for(u32 index = 0u; index < samples.count; ++index){
        if(index != 0u)
            NWB_COUT << ',';
        NWB_COUT << samples.values[index];
    }
    NWB_COUT
        << "],\"min_ns_per_command\":" << summary.minimum
        << ",\"median_ns_per_command\":" << summary.median
        << ",\"max_ns_per_command\":" << summary.maximum
        << '}'
    ;
}

static void EmitResult(const Result& result){
    NWB_COUT
        << "NWB_COMMAND_IR_PROFILE_RESULT {"
        << "\"status\":\"" << result.status << "\","
        << "\"reason\":\"" << result.reason << "\","
        << "\"workload\":\"" << result.workload << "\","
        << "\"requested_adapter_index\":" << result.requestedAdapterIndex << ','
        << "\"selected_adapter_vendor_id\":" << result.selectedAdapterVendorID << ','
        << "\"selected_adapter_device_id\":" << result.selectedAdapterDeviceID << ','
        << "\"selected_adapter_uuid\":"
    ;
    if(!result.selectedAdapterHasUUID)
        NWB_COUT << "null";
    else{
        static constexpr char s_HexDigits[] = "0123456789abcdef";
        NWB_COUT << '\"';
        for(const u8 byte : result.selectedAdapterUUID)
            NWB_COUT << s_HexDigits[byte >> 4u] << s_HexDigits[byte & 0x0fu];
        NWB_COUT << '\"';
    }
    NWB_COUT
        << ','
        << "\"graphics_family\":" << result.graphicsFamily << ','
        << "\"records\":" << result.recordCount << ','
        << "\"warmup\":" << result.warmupCount << ','
        << "\"samples\":" << result.sampleCount << ','
        << "\"stream_bytes\":" << result.streamBytes << ','
        << "\"payload_bytes\":" << result.payloadBytes << ','
        << "\"decoded_records\":" << result.decodedRecords << ','
        << "\"preflight_records\":" << result.preflightRecords << ','
        << "\"replayed_records\":" << result.replayedRecords << ','
        << "\"direct_vulkan_replayed_records\":" << result.directVulkanReplayedRecords << ','
        << "\"capture_allocation_delta\":" << result.captureAllocationDelta << ','
        << "\"capture_reallocation_delta\":" << result.captureReallocationDelta << ','
        << "\"stream_valid\":" << (result.streamValid ? "true" : "false") << ','
        << "\"expected_hash\":" << result.expectedHash << ','
        << "\"observed_hash\":" << result.observedHash << ','
        << "\"checksum_verified\":" << (result.checksumVerified ? "true" : "false") << ','
        << "\"direct_vulkan_observed_hash\":" << result.directVulkanObservedHash << ','
        << "\"direct_vulkan_checksum_verified\":"
        << (result.directVulkanChecksumVerified ? "true" : "false") << ','
        << "\"native_record\":"
    ;
    EmitTimingSamples(result.nativeRecord);
    NWB_COUT << ",\"capture_record\":";
    EmitTimingSamples(result.captureRecord);
    NWB_COUT << ",\"reader_decode\":";
    EmitTimingSamples(result.readerDecode);
    NWB_COUT << ",\"preflight\":";
    EmitTimingSamples(result.preflight);
    NWB_COUT << ",\"replay\":";
    EmitTimingSamples(result.replay);
    NWB_COUT << ",\"direct_vulkan_replay\":";
    EmitTimingSamples(result.directVulkanReplay);
    NWB_COUT
        << ",\"logger_errors\":" << result.loggerErrors
        << "}" << '\n'
    ;
}

[[nodiscard]] static int Run(const int argc, char** argv){
    Arguments arguments;
    if(!ParseArguments(argc, argv, arguments))
        return 2;

    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    static constexpr Name s_ArenaName{"tests/ab/command_ir/profile_arena"};
    Alloc::GlobalArena arena(s_ArenaName);
    GraphicsAllocator allocator(arena);
    Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
    Alloc::JobSystem jobSystem(threadPool);
    Perf::TimingRecorder gpuTiming(arena);
    Graphics graphics(allocator, threadPool, jobSystem, gpuTiming);

    Result result;
    result.requestedAdapterIndex = arguments.adapterIndex;
    result.recordCount = arguments.recordCount;
    result.warmupCount = arguments.warmupCount;
    result.sampleCount = arguments.sampleCount;
    const auto finish = [&](int exitCode){
        graphics.destroy();
        result.loggerErrors = logger.errorCount();
        if(exitCode == 0 && result.loggerErrors != 0u){
            result.status = "failed";
            result.reason = "logger_reported_error";
            exitCode = 1;
        }
        if(result.loggerErrors != 0u)
            logger.emitErrorsToStderr();
        EmitResult(result);
        return exitCode;
    };

    if(arguments.gpuValidation && !graphics.setDebugRuntimeEnabled(true)){
        result.status = "skipped";
        result.reason = "gpu_validation_unavailable";
        return finish(s_SkipExitCode);
    }
    // This intentionally uses the normal Graphics fallback topology.  Capture/replay must not affect physical
    // queue assignment, and the packet itself still declares Transfer capability just like built-in copy tasks.
    if(
        !graphics.setAdapterIndex(arguments.adapterIndex)
        || !graphics.setTransferQueueEnabled(false)
        || !graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ){
        result.reason = "profile_configuration_rejected";
        return finish(1);
    }
    if(!graphics.createHeadlessDevice()){
        result.status = "skipped";
        result.reason = "headless_vulkan_or_descriptor_buffer_unavailable";
        return finish(s_SkipExitCode);
    }
    if(!CaptureSelectedAdapterIdentity(graphics, allocator, result)){
        result.reason = "selected_adapter_identity_unavailable";
        return finish(1);
    }
    result.graphicsFamily = graphics.getDevice().getQueueFamilyIndex(CommandQueue::Graphics);
    if(result.graphicsFamily == Limit<u32>::s_Max){
        result.status = "skipped";
        result.reason = "graphics_queue_unavailable";
        return finish(s_SkipExitCode);
    }

    if(!RunProfile(graphics, arena, arguments, result) || logger.errorCount() != 0u){
        result.reason = logger.errorCount() != 0u ? "logger_reported_error" : "command_ir_profile_failed";
        return finish(1);
    }
    result.status = "ok";
    result.reason = "completed";
    return finish(0);
}

#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
[[nodiscard]] static int EntryPoint(const isize argc, wchar** argv, void*){
    return Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, Run);
}
#else
[[nodiscard]] static int EntryPoint(const isize argc, char** argv, void*){
    return Run(static_cast<int>(argc), argv);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_APPLICATION_ENTRY_POINT(::NWB::CommandIrProfile::EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

