// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Target-hardware probe for the Phase 10 Transfer queue decision.  This is intentionally a small standalone
// executable rather than a renderer benchmark: it drives the public setup-upload APIs directly, records their
// physical route/readiness contract, and leaves GPU bandwidth attribution to an external profiler.  The automatic
// path makes uploaded resources concurrently shared with Graphics and inserts a Graphics timeline bridge, so it
// must be reported as *zero ownership transfers*, not as an exclusive-family handoff.


#include <global/global.h>

#include <core/common/application_entry.h>
#include <core/common/module.h>
#include <core/alloc/general.h>
#include <core/alloc/job.h>
#include <core/alloc/thread.h>
#include <core/graphics/api.h>
#include <core/graphics/module.h>
#include <core/graphics/vulkan/backend.h>
#include <core/perf/timing.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>
#include <tests/common/capturing_logger.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TransferUploadProfile{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


inline constexpr int s_SkipExitCode = 77;
inline constexpr usize s_MiB = 1024u * 1024u;
inline constexpr u32 s_TextureWidth = 1024u;
inline constexpr u64 s_ChecksumHashSeed = 1469598103934665603ull;


enum class Route : u8{
    Graphics,
    Automatic,
};

enum class ResourceKind : u8{
    Buffer,
    Texture,
};

struct Arguments{
    Route route = Route::Automatic;
    ResourceKind resourceKind = ResourceKind::Buffer;
    i32 adapterIndex = -1;
    usize uploadMiB = 16u;
    u32 iterations = 12u;
    u32 inFlightIterations = 2u;
    usize contentionMiB = 32u;
    u32 contentionCopies = 16u;
    bool gpuValidation = false;
};

struct Result{
    const char* status = "failed";
    const char* reason = "unknown";
    Route requestedRoute = Route::Automatic;
    ResourceKind resourceKind = ResourceKind::Buffer;
    CommandQueue::Enum producerQueue = CommandQueue::kCount;
    bool transferQueueEnabled = true;
    i32 requestedAdapterIndex = -1;
    u32 selectedAdapterVendorID = 0u;
    u32 selectedAdapterDeviceID = 0u;
    AdapterInfo::UUID selectedAdapterUUID = {};
    bool selectedAdapterHasUUID = false;
    u32 graphicsFamily = Limit<u32>::s_Max;
    u32 transferFamily = Limit<u32>::s_Max;
    u32 producerFamily = Limit<u32>::s_Max;
    u32 queueSharing = 0u;
    usize uploadBytes = 0u;
    u32 iterations = 0u;
    u32 inFlightWindow = 0u;
    usize contentionBytes = 0u;
    u32 contentionCopies = 0u;
    u32 graphicsReadinessCopies = 0u;
    u32 observedGraphicsReadinessBridgeSubmissions = 0u;
    u32 expectedExclusiveOwnershipTransfers = 0u;
    f64 submitSeconds = 0.0;
    f64 completionSeconds = 0.0;
    u64 expectedHash = 0u;
    u64 observedHash = 0u;
    bool checksumVerified = false;
    bool graphicsReadinessVerified = false;
    u32 loggerErrors = 0u;
};


[[nodiscard]] static const char* RouteName(const Route route){
    switch(route){
    case Route::Graphics:
        return "graphics";
    case Route::Automatic:
        return "automatic";
    }
    return "invalid";
}

[[nodiscard]] static const char* ResourceKindName(const ResourceKind kind){
    switch(kind){
    case ResourceKind::Buffer:
        return "buffer";
    case ResourceKind::Texture:
        return "texture";
    }
    return "invalid";
}

[[nodiscard]] static const char* QueueName(const CommandQueue::Enum queue){
    switch(queue){
    case CommandQueue::Graphics:
        return "graphics";
    case CommandQueue::Compute:
        return "compute";
    case CommandQueue::Transfer:
        return "transfer";
    default:
        return "invalid";
    }
}

[[nodiscard]] static bool ParseUnsigned(const char* value, usize& outValue){
    if(!value || !*value)
        return false;

    u64 parsed = 0u;
    if(!ParseU64(AStringView(value), parsed) || parsed > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    outValue = static_cast<usize>(parsed);
    return true;
}

[[nodiscard]] static bool ParseUnsigned(const char* value, u32& outValue){
    usize parsed = 0u;
    if(!ParseUnsigned(value, parsed) || parsed > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    outValue = static_cast<u32>(parsed);
    return true;
}

[[nodiscard]] static bool ParseAdapterIndex(const char* value, i32& outValue){
    if(!value || !*value)
        return false;

    i64 parsed = 0;
    if(!ParseI64(AStringView(value), parsed) || parsed < -1 || parsed > static_cast<i64>(Limit<i32>::s_Max))
        return false;

    outValue = static_cast<i32>(parsed);
    return true;
}

[[nodiscard]] static bool ParseArguments(const int argc, char** argv, Arguments& outArguments){
    for(int index = 1; index < argc; ++index){
        const char* const argument = argv[index];
        if(NWB_STRCMP(argument, "--route") == 0){
            if(++index >= argc)
                return false;
            const char* const value = argv[index];
            if(NWB_STRCMP(value, "graphics") == 0)
                outArguments.route = Route::Graphics;
            else if(NWB_STRCMP(value, "automatic") == 0)
                outArguments.route = Route::Automatic;
            else
                return false;
        }
        else if(NWB_STRCMP(argument, "--resource") == 0){
            if(++index >= argc)
                return false;
            const char* const value = argv[index];
            if(NWB_STRCMP(value, "buffer") == 0)
                outArguments.resourceKind = ResourceKind::Buffer;
            else if(NWB_STRCMP(value, "texture") == 0)
                outArguments.resourceKind = ResourceKind::Texture;
            else
                return false;
        }
        else if(NWB_STRCMP(argument, "--adapter-index") == 0){
            if(++index >= argc || !ParseAdapterIndex(argv[index], outArguments.adapterIndex))
                return false;
        }
        else if(NWB_STRCMP(argument, "--upload-mib") == 0){
            if(++index >= argc || !ParseUnsigned(argv[index], outArguments.uploadMiB))
                return false;
        }
        else if(NWB_STRCMP(argument, "--iterations") == 0){
            if(++index >= argc || !ParseUnsigned(argv[index], outArguments.iterations))
                return false;
        }
        else if(NWB_STRCMP(argument, "--in-flight") == 0){
            if(++index >= argc || !ParseUnsigned(argv[index], outArguments.inFlightIterations))
                return false;
        }
        else if(NWB_STRCMP(argument, "--contention-mib") == 0){
            if(++index >= argc || !ParseUnsigned(argv[index], outArguments.contentionMiB))
                return false;
        }
        else if(NWB_STRCMP(argument, "--contention-copies") == 0){
            if(++index >= argc || !ParseUnsigned(argv[index], outArguments.contentionCopies))
                return false;
        }
        else if(NWB_STRCMP(argument, "--gpu-validation") == 0)
            outArguments.gpuValidation = true;
        else if(NWB_STRCMP(argument, "--no-gpu-validation") == 0)
            outArguments.gpuValidation = false;
        else if(NWB_STRCMP(argument, "--help") == 0 || NWB_STRCMP(argument, "-h") == 0){
            NWB_COUT
                << "Usage: transfer_upload_profile [--route graphics|automatic] [--resource buffer|texture] "
                << "[--adapter-index N] [--upload-mib N] [--iterations N] [--in-flight N] "
                << "[--contention-mib N] [--contention-copies N] "
                << "[--gpu-validation|--no-gpu-validation]\n";
            return false;
        }
        else
            return false;
    }

    if(outArguments.uploadMiB == 0u || outArguments.iterations == 0u || outArguments.inFlightIterations == 0u)
        return false;
    if(outArguments.contentionMiB != 0u && outArguments.contentionCopies == 0u)
        return false;
    if(outArguments.uploadMiB > Limit<usize>::s_Max / s_MiB || outArguments.contentionMiB > Limit<usize>::s_Max / s_MiB)
        return false;

    const u64 uploadBytes = static_cast<u64>(outArguments.uploadMiB) * static_cast<u64>(s_MiB);
    const u64 contentionBytes = static_cast<u64>(outArguments.contentionMiB) * static_cast<u64>(s_MiB);
    if(uploadBytes > Limit<u64>::s_Max / static_cast<u64>(outArguments.iterations) / 2u)
        return false;
    if(
        contentionBytes != 0u
        && contentionBytes > Limit<u64>::s_Max / static_cast<u64>(outArguments.contentionCopies) / static_cast<u64>(outArguments.iterations) / 2u
    )
        return false;

    return true;
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

static void FillUploadData(Vector<u8, Alloc::GlobalArena>& outBytes){
    for(usize index = 0u; index < outBytes.size(); ++index)
        outBytes[index] = static_cast<u8>((index * 131u + index / 17u + 29u) & 0xffu);
}

[[nodiscard]] static CommandQueue::Enum RequestedQueue(const Route route){
    return route == Route::Graphics ? CommandQueue::Graphics : CommandQueue::kCount;
}

[[nodiscard]] static bool SubmitGraphicsContention(
    GraphicsBackend::Device& device,
    const usize byteSize,
    const u32 copyCount,
    Vector<BufferHandle, Alloc::GlobalArena>& retainedBuffers,
    QueueSubmissionToken& outToken
){
    outToken = {};

    if(byteSize == 0u || copyCount == 0u){
        CommandListHandle marker = device.createCommandList();
        if(!marker)
            return false;

        marker->open();
        if(!marker->hasCommandBuffer())
            return false;
        marker->close();

        CommandList* const markerLists[] = { marker.get() };
        outToken = device.executeCommandLists(markerLists, LengthOf(markerLists), CommandQueue::Graphics, QueueSubmissionDesc{});
        return outToken.valid();
    }

    const BufferDesc bufferDesc = BufferDesc()
        .setByteSize(static_cast<u64>(byteSize))
        .setInitialState(ResourceStates::Common)
    ;
    BufferHandle source = device.createBuffer(bufferDesc);
    BufferHandle destination = device.createBuffer(bufferDesc);
    if(!source || !destination)
        return false;

    CommandListHandle commandList = device.createCommandList();
    if(!commandList)
        return false;

    commandList->open();
    if(!commandList->hasCommandBuffer())
        return false;
    for(u32 copyIndex = 0u; copyIndex < copyCount; ++copyIndex)
        commandList->copyBuffer(destination.get(), 0u, source.get(), 0u, static_cast<u64>(byteSize));
    commandList->close();

    CommandList* const commandLists[] = { commandList.get() };
    outToken = device.executeCommandLists(commandLists, LengthOf(commandLists), CommandQueue::Graphics, QueueSubmissionDesc{});
    if(!outToken.valid())
        return false;

    retainedBuffers.push_back(Move(source));
    retainedBuffers.push_back(Move(destination));
    return true;
}

[[nodiscard]] static bool SubmitGraphicsBufferReadinessCopy(
    GraphicsBackend::Device& device,
    Buffer* const source,
    const usize byteSize,
    BufferHandle& outReadback,
    QueueSubmissionToken& outToken
){
    outReadback.reset();
    outToken = {};

    const BufferDesc readbackDesc = BufferDesc()
        .setByteSize(static_cast<u64>(byteSize))
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    outReadback = device.createBuffer(readbackDesc);
    if(!outReadback)
        return false;

    CommandListHandle commandList = device.createCommandList();
    if(!commandList)
        return false;

    commandList->open();
    if(!commandList->hasCommandBuffer())
        return false;
    commandList->copyBuffer(outReadback.get(), 0u, source, 0u, static_cast<u64>(byteSize));
    commandList->close();

    CommandList* const commandLists[] = { commandList.get() };
    outToken = device.executeCommandLists(commandLists, LengthOf(commandLists), CommandQueue::Graphics, QueueSubmissionDesc{});
    return outToken.valid();
}

[[nodiscard]] static bool VerifyBufferReadback(
    GraphicsBackend::Device& device,
    Buffer* const readback,
    const usize byteSize,
    u64& outHash
){
    const auto* const bytes = static_cast<const u8*>(device.mapBuffer(readback, CpuAccessMode::Read));
    if(!bytes)
        return false;
    outHash = UpdateFnv64(s_ChecksumHashSeed, bytes, byteSize);
    device.unmapBuffer(readback);
    return true;
}

[[nodiscard]] static bool SubmitGraphicsTextureReadinessCopy(
    GraphicsBackend::Device& device,
    Texture* const source,
    const TextureDesc& textureDesc,
    StagingTextureHandle& outReadback,
    QueueSubmissionToken& outToken
){
    outReadback.reset();
    outToken = {};

    outReadback = device.createStagingTexture(textureDesc, CpuAccessMode::Read);
    if(!outReadback)
        return false;

    CommandListHandle commandList = device.createCommandList();
    if(!commandList)
        return false;

    commandList->open();
    if(!commandList->hasCommandBuffer())
        return false;
    commandList->copyTexture(outReadback.get(), TextureSlice{}, source, TextureSlice{});
    commandList->close();

    CommandList* const commandLists[] = { commandList.get() };
    outToken = device.executeCommandLists(commandLists, LengthOf(commandLists), CommandQueue::Graphics, QueueSubmissionDesc{});
    return outToken.valid();
}

[[nodiscard]] static bool VerifyTextureReadback(
    GraphicsBackend::Device& device,
    StagingTexture* const readback,
    const TextureDesc& textureDesc,
    u64& outHash
){
    usize rowPitch = 0u;
    const auto* const bytes = static_cast<const u8*>(device.mapStagingTexture(readback, TextureSlice{}, CpuAccessMode::Read, &rowPitch));
    if(!bytes || rowPitch < static_cast<usize>(textureDesc.width) * sizeof(u32))
        return false;

    outHash = s_ChecksumHashSeed;
    const usize rowBytes = static_cast<usize>(textureDesc.width) * sizeof(u32);
    for(u32 row = 0u; row < textureDesc.height; ++row)
        outHash = UpdateFnv64(outHash, bytes + static_cast<usize>(row) * rowPitch, rowBytes);
    device.unmapStagingTexture(readback);
    return true;
}

// The harness owns all Graphics submissions.  The consumer token therefore exposes exactly how many Graphics queue
// submissions were inserted by setup between the pre-upload contention marker and the no-wait consumer copy.  A
// Graphics producer accounts for one setup upload submission; a non-Graphics producer must account for one and only
// one timeline-readiness bridge.  This is an observed queue-timeline count, not a route-derived constant.
[[nodiscard]] static bool RecordGraphicsReadinessBridge(
    const QueueSubmissionToken& graphicsBeforeSetup,
    const QueueSubmissionToken& uploadToken,
    const QueueSubmissionToken& graphicsConsumerToken,
    Result& outResult
){
    if(
        graphicsBeforeSetup.queue != CommandQueue::Graphics
        || graphicsConsumerToken.queue != CommandQueue::Graphics
        || graphicsConsumerToken.value <= graphicsBeforeSetup.value
    )
        return false;

    const u64 setupRelatedGraphicsSubmissions = graphicsConsumerToken.value - graphicsBeforeSetup.value - 1u;
    if(setupRelatedGraphicsSubmissions != 1u)
        return false;

    if(uploadToken.queue != CommandQueue::Graphics)
        ++outResult.observedGraphicsReadinessBridgeSubmissions;
    return true;
}

[[nodiscard]] static bool RunBufferProfile(
    Graphics& graphics,
    Alloc::GlobalArena& arena,
    const Arguments& arguments,
    Result& outResult
){
    auto& device = graphics.getDevice();
    const usize uploadBytes = arguments.uploadMiB * s_MiB;
    const usize contentionBytes = arguments.contentionMiB * s_MiB;
    Vector<u8, Alloc::GlobalArena> input(arena);
    input.resize(uploadBytes);
    FillUploadData(input);
    outResult.expectedHash = UpdateFnv64(s_ChecksumHashSeed, input.data(), input.size());

    const u32 inFlightWindow = arguments.inFlightIterations > arguments.iterations
        ? arguments.iterations
        : arguments.inFlightIterations
    ;
    Vector<BufferHandle, Alloc::GlobalArena> uploadedBuffers(arena);
    Vector<BufferHandle, Alloc::GlobalArena> contentionBuffers(arena);
    Vector<BufferHandle, Alloc::GlobalArena> readinessReadbacks(arena);
    uploadedBuffers.reserve(inFlightWindow);
    contentionBuffers.reserve(static_cast<usize>(inFlightWindow) * 2u);
    readinessReadbacks.reserve(inFlightWindow);

    QueueSubmissionToken firstToken;
    const Timer begin = TimerNow();
    for(u32 iteration = 0u; iteration < arguments.iterations; ++iteration){
        const Timer submitBegin = TimerNow();
        QueueSubmissionToken graphicsBeforeSetup;
        if(!SubmitGraphicsContention(
            device,
            contentionBytes,
            arguments.contentionCopies,
            contentionBuffers,
            graphicsBeforeSetup
        ))
            return false;

        QueueSubmissionToken uploadToken;
        Graphics::BufferSetupDesc setupDesc;
        setupDesc.bufferDesc = BufferDesc()
            .setByteSize(static_cast<u64>(uploadBytes))
            .setInitialState(ResourceStates::Common)
        ;
        setupDesc.data = input.data();
        setupDesc.dataSize = input.size();
        setupDesc.queue = RequestedQueue(arguments.route);
        setupDesc.acceptedToken = &uploadToken;
        BufferHandle uploaded = graphics.setupBuffer(setupDesc);
        if(!uploaded || !uploadToken.valid())
            return false;

        if(iteration == 0u){
            firstToken = uploadToken;
            outResult.queueSharing = static_cast<u32>(uploaded->getDescription().queueSharing);
        }
        else if(uploadToken.queue != firstToken.queue)
            return false;

        BufferHandle readinessReadback;
        QueueSubmissionToken graphicsConsumerToken;
        if(!SubmitGraphicsBufferReadinessCopy(
            device,
            uploaded.get(),
            uploadBytes,
            readinessReadback,
            graphicsConsumerToken
        ))
            return false;
        if(!RecordGraphicsReadinessBridge(graphicsBeforeSetup, uploadToken, graphicsConsumerToken, outResult))
            return false;

        uploadedBuffers.push_back(Move(uploaded));
        readinessReadbacks.push_back(Move(readinessReadback));
        ++outResult.graphicsReadinessCopies;
        outResult.submitSeconds += DurationInSeconds<f64>(TimerNow(), submitBegin);

        if((iteration + 1u) % inFlightWindow != 0u && iteration + 1u != arguments.iterations)
            continue;

        if(!device.waitForIdle())
            return false;
        for(const BufferHandle& readback : readinessReadbacks){
            if(!VerifyBufferReadback(device, readback.get(), uploadBytes, outResult.observedHash))
                return false;
            if(outResult.observedHash != outResult.expectedHash)
                return false;
        }
        readinessReadbacks.clear();
        uploadedBuffers.clear();
        contentionBuffers.clear();
    }
    outResult.completionSeconds = DurationInSeconds<f64>(TimerNow(), begin);

    outResult.checksumVerified = true;
    outResult.graphicsReadinessVerified = true;
    outResult.producerQueue = firstToken.queue;
    outResult.uploadBytes = uploadBytes;
    outResult.inFlightWindow = inFlightWindow;
    outResult.contentionBytes = contentionBytes;
    outResult.contentionCopies = arguments.contentionCopies;
    return outResult.checksumVerified;
}

[[nodiscard]] static bool RunTextureProfile(
    Graphics& graphics,
    Alloc::GlobalArena& arena,
    const Arguments& arguments,
    Result& outResult
){
    auto& device = graphics.getDevice();
    const usize uploadBytes = arguments.uploadMiB * s_MiB;
    const usize contentionBytes = arguments.contentionMiB * s_MiB;
    const usize rowBytes = static_cast<usize>(s_TextureWidth) * sizeof(u32);
    if(uploadBytes % rowBytes != 0u)
        return false;
    const usize textureHeight = uploadBytes / rowBytes;
    if(textureHeight == 0u || textureHeight > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    TextureDesc textureDesc;
    textureDesc
        .setWidth(s_TextureWidth)
        .setHeight(static_cast<u32>(textureHeight))
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::ShaderResource)
    ;

    Vector<u8, Alloc::GlobalArena> input(arena);
    input.resize(uploadBytes);
    FillUploadData(input);
    outResult.expectedHash = UpdateFnv64(s_ChecksumHashSeed, input.data(), input.size());

    const u32 inFlightWindow = arguments.inFlightIterations > arguments.iterations
        ? arguments.iterations
        : arguments.inFlightIterations
    ;
    Vector<TextureHandle, Alloc::GlobalArena> uploadedTextures(arena);
    Vector<BufferHandle, Alloc::GlobalArena> contentionBuffers(arena);
    Vector<StagingTextureHandle, Alloc::GlobalArena> readinessReadbacks(arena);
    uploadedTextures.reserve(inFlightWindow);
    contentionBuffers.reserve(static_cast<usize>(inFlightWindow) * 2u);
    readinessReadbacks.reserve(inFlightWindow);

    QueueSubmissionToken firstToken;
    const Timer begin = TimerNow();
    for(u32 iteration = 0u; iteration < arguments.iterations; ++iteration){
        const Timer submitBegin = TimerNow();
        QueueSubmissionToken graphicsBeforeSetup;
        if(!SubmitGraphicsContention(
            device,
            contentionBytes,
            arguments.contentionCopies,
            contentionBuffers,
            graphicsBeforeSetup
        ))
            return false;

        QueueSubmissionToken uploadToken;
        Graphics::TextureSetupDesc setupDesc;
        setupDesc.textureDesc = textureDesc;
        setupDesc.data = input.data();
        setupDesc.uploadDataSize = input.size();
        setupDesc.queue = RequestedQueue(arguments.route);
        setupDesc.acceptedToken = &uploadToken;
        TextureHandle uploaded = graphics.setupTexture(setupDesc);
        if(!uploaded || !uploadToken.valid())
            return false;

        if(iteration == 0u){
            firstToken = uploadToken;
            outResult.queueSharing = static_cast<u32>(uploaded->getDescription().queueSharing);
        }
        else if(uploadToken.queue != firstToken.queue)
            return false;

        StagingTextureHandle readinessReadback;
        QueueSubmissionToken graphicsConsumerToken;
        if(!SubmitGraphicsTextureReadinessCopy(
            device,
            uploaded.get(),
            textureDesc,
            readinessReadback,
            graphicsConsumerToken
        ))
            return false;
        if(!RecordGraphicsReadinessBridge(graphicsBeforeSetup, uploadToken, graphicsConsumerToken, outResult))
            return false;

        uploadedTextures.push_back(Move(uploaded));
        readinessReadbacks.push_back(Move(readinessReadback));
        ++outResult.graphicsReadinessCopies;
        outResult.submitSeconds += DurationInSeconds<f64>(TimerNow(), submitBegin);

        if((iteration + 1u) % inFlightWindow != 0u && iteration + 1u != arguments.iterations)
            continue;

        if(!device.waitForIdle())
            return false;
        for(const StagingTextureHandle& readback : readinessReadbacks){
            if(!VerifyTextureReadback(device, readback.get(), textureDesc, outResult.observedHash))
                return false;
            if(outResult.observedHash != outResult.expectedHash)
                return false;
        }
        readinessReadbacks.clear();
        uploadedTextures.clear();
        contentionBuffers.clear();
    }
    outResult.completionSeconds = DurationInSeconds<f64>(TimerNow(), begin);

    outResult.checksumVerified = true;
    outResult.graphicsReadinessVerified = true;
    outResult.producerQueue = firstToken.queue;
    outResult.uploadBytes = uploadBytes;
    outResult.inFlightWindow = inFlightWindow;
    outResult.contentionBytes = contentionBytes;
    outResult.contentionCopies = arguments.contentionCopies;
    return outResult.checksumVerified;
}

static void EmitResult(const Result& result){
    const u64 totalLogicalUploadBytes = static_cast<u64>(result.uploadBytes) * static_cast<u64>(result.iterations);
    const u64 modeledUploadReadWriteBytes = totalLogicalUploadBytes * 2u;
    const u64 modeledGraphicsReadinessReadWriteBytes = static_cast<u64>(result.uploadBytes)
        * static_cast<u64>(result.graphicsReadinessCopies)
        * 2u
    ;
    const u64 modeledGraphicsContentionReadWriteBytes = static_cast<u64>(result.contentionBytes)
        * static_cast<u64>(result.contentionCopies)
        * static_cast<u64>(result.iterations)
        * 2u
    ;
    const f64 totalUploadMiB = static_cast<f64>(totalLogicalUploadBytes) / static_cast<f64>(s_MiB);
    const f64 completionMiBPerSecond = result.completionSeconds > 0.0 ? totalUploadMiB / result.completionSeconds : 0.0;
    NWB_COUT
        << "NWB_TRANSFER_UPLOAD_PROFILE_RESULT {"
        << "\"status\":\"" << result.status << "\","
        << "\"reason\":\"" << result.reason << "\","
        << "\"requested_route\":\"" << RouteName(result.requestedRoute) << "\","
        << "\"resource\":\"" << ResourceKindName(result.resourceKind) << "\","
        << "\"transfer_queue_enabled\":" << (result.transferQueueEnabled ? "true" : "false") << ","
        << "\"requested_adapter_index\":" << result.requestedAdapterIndex << ","
        << "\"selected_adapter_vendor_id\":" << result.selectedAdapterVendorID << ","
        << "\"selected_adapter_device_id\":" << result.selectedAdapterDeviceID << ","
        << "\"selected_adapter_uuid\":"
    ;
    if(!result.selectedAdapterHasUUID)
        NWB_COUT << "null";
    else{
        static constexpr char s_HexDigits[] = "0123456789abcdef";
        NWB_COUT << '"';
        for(const u8 byte : result.selectedAdapterUUID)
            NWB_COUT << s_HexDigits[byte >> 4u] << s_HexDigits[byte & 0x0fu];
        NWB_COUT << '"';
    }
    NWB_COUT
        << ","
        << "\"producer_queue\":\"" << QueueName(result.producerQueue) << "\","
        << "\"graphics_family\":" << result.graphicsFamily << ","
        << "\"transfer_family\":" << result.transferFamily << ","
        << "\"producer_family\":" << result.producerFamily << ","
        << "\"queue_sharing_mask\":" << result.queueSharing << ","
        << "\"logical_upload_bytes\":" << result.uploadBytes << ","
        << "\"iterations\":" << result.iterations << ","
        << "\"in_flight_window\":" << result.inFlightWindow << ","
        << "\"total_logical_upload_bytes\":" << totalLogicalUploadBytes << ","
        << "\"modeled_upload_read_write_bytes\":" << modeledUploadReadWriteBytes << ","
        << "\"async_ingress_copy_bytes\":0,"
        << "\"graphics_readiness_copy_bytes\":" << result.uploadBytes << ","
        << "\"graphics_readiness_copies\":" << result.graphicsReadinessCopies << ","
        << "\"modeled_graphics_readiness_read_write_bytes\":" << modeledGraphicsReadinessReadWriteBytes << ","
        << "\"graphics_contention_copy_bytes\":" << result.contentionBytes << ","
        << "\"graphics_contention_copies\":" << result.contentionCopies << ","
        << "\"modeled_graphics_contention_read_write_bytes\":" << modeledGraphicsContentionReadWriteBytes << ","
        << "\"observed_graphics_readiness_bridge_submissions\":" << result.observedGraphicsReadinessBridgeSubmissions << ","
        << "\"expected_exclusive_ownership_transfers\":" << result.expectedExclusiveOwnershipTransfers << ","
        << "\"host_submit_seconds\":" << result.submitSeconds << ","
        << "\"completion_seconds\":" << result.completionSeconds << ","
        << "\"completion_mib_per_second\":" << completionMiBPerSecond << ","
        << "\"expected_hash\":" << result.expectedHash << ","
        << "\"observed_hash\":" << result.observedHash << ","
        << "\"checksum_verified\":" << (result.checksumVerified ? "true" : "false") << ","
        << "\"graphics_readiness_verified\":" << (result.graphicsReadinessVerified ? "true" : "false") << ","
        << "\"logger_errors\":" << result.loggerErrors
        << "}" << '\n'
    ;
}

[[nodiscard]] static int Run(const int argc, char** argv){
    Arguments arguments;
    if(!ParseArguments(argc, argv, arguments))
        return 2;

    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    static constexpr Name s_ArenaName{"tests/ab/transfer_queue/profile_arena"};
    Alloc::GlobalArena arena(s_ArenaName);
    GraphicsAllocator allocator(arena);
    Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
    Alloc::JobSystem jobSystem(threadPool);
    Perf::TimingRecorder gpuTiming(arena);
    Graphics graphics(allocator, threadPool, jobSystem, gpuTiming);

    Result result;
    result.requestedRoute = arguments.route;
    result.resourceKind = arguments.resourceKind;
    result.iterations = arguments.iterations;
    result.requestedAdapterIndex = arguments.adapterIndex;
    // Keep a dedicated Transfer queue enabled in both arms.  The explicit Graphics request is the control; changing
    // logical-device topology would confound a transport-route comparison.
    result.transferQueueEnabled = true;

    const auto finish = [&](int exitCode){
        if(!graphics.destroy()){
            result.status = "failed";
            result.reason = "graphics_destroy_failed";
            exitCode = 1;
        }
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
    if(!graphics.setAdapterIndex(arguments.adapterIndex)
        || !graphics.setTransferQueueEnabled(result.transferQueueEnabled)
        || !graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())){
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

    auto& device = graphics.getDevice();
    result.graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    if(device.getQueue(CommandQueue::Transfer))
        result.transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);

    if(arguments.route == Route::Automatic && (!device.getQueue(CommandQueue::Transfer) || result.transferFamily == result.graphicsFamily)){
        result.status = "skipped";
        result.reason = "dedicated_transfer_family_unavailable";
        return finish(s_SkipExitCode);
    }

    const bool succeeded = arguments.resourceKind == ResourceKind::Buffer
        ? RunBufferProfile(graphics, arena, arguments, result)
        : RunTextureProfile(graphics, arena, arguments, result)
    ;
    if(!succeeded || logger.errorCount() != 0u){
        result.status = "failed";
        result.reason = succeeded ? "logger_reported_error" : "upload_or_readback_failed";
        if(result.producerQueue != CommandQueue::kCount)
            result.producerFamily = device.getQueueFamilyIndex(result.producerQueue);
        return finish(1);
    }

    result.producerFamily = device.getQueueFamilyIndex(result.producerQueue);
    const u32 expectedReadinessBridges = result.producerQueue == CommandQueue::Graphics ? 0u : result.iterations;
    if(result.observedGraphicsReadinessBridgeSubmissions != expectedReadinessBridges || !result.graphicsReadinessVerified){
        result.status = "failed";
        result.reason = "readiness_bridge_verification_failed";
        return finish(1);
    }
    // Setup uploads use concurrent sharing and timeline readiness.  The zero value is the declared contract, not a
    // backend-wide barrier counter; graph ownership transfers remain separate external-profiler/telemetry evidence.
    result.expectedExclusiveOwnershipTransfers = 0u;
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


NWB_DEFINE_APPLICATION_ENTRY_POINT(::NWB::TransferUploadProfile::EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

