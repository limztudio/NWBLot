// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "coopvec.h"
#include "hardware_ray_tracing_policy.h"
#include "gpu_crash_diagnostics.h"

#include <core/task/cpu/scheduler.h>

#include "gpu_descriptor_heap.h"

#include <core/filesystem/factory.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Feature{
    enum Enum : u8{
        ConservativeRasterization,
        ConstantBufferRanges,
        DeferredCommandLists,
        FastGeometryShader,
        HeapDirectlyIndexed,
        HlslExtensionUAV,
        LinearSweptSpheres,
        Meshlets,
        RayQuery,
        RayTracingAccelStruct,
        RayTracingClusters,
        RayTracingOpacityMicromap,
        RayTracingPipeline,
        ShaderExecutionReordering,
        ShaderSpecializations,
        SinglePassStereo,
        Spheres,
        VariableRateShading,
        CooperativeVectorInferencing,
        CooperativeVectorTraining,

        Count
    };
};

// One opaque binary semaphore signal from a submission-local hook. Only the owning Device decodes it.
struct QueueSubmissionNativeSignal{
    Object semaphore = Object(u64{0u});
    u64 value = 0u;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return semaphore.integer != 0u; }
};

// Prepares a submission-local binary signal atomically; exceptions unwind to the application boundary.
using QueueSubmissionPreSubmitCallback = Expected<QueueSubmissionNativeSignal>(*) (void* context, u64 identity, const GpuPhysicalQueueId& executionQueue);

// Called once after hook preparation with the accepted token (or invalid on rejection). No throw, no Device drain.
using QueueSubmissionResolvedCallback = b8(*) (void* context, u64 identity, const QueueSubmissionToken& submissionToken)noexcept;

struct QueueSubmissionPreSubmitHook{
    void* context = nullptr;
    u64 identity = 0u;
    QueueSubmissionPreSubmitCallback invoke = nullptr;
    QueueSubmissionResolvedCallback resolved = &QueueSubmissionPreSubmitHook::IgnoreResolution;


private:
    [[nodiscard]] static b8 IgnoreResolution(void*, u64, const QueueSubmissionToken&)noexcept{ return true; }


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return invoke != nullptr && resolved != nullptr; }
};

// Cross-queue deps. Same-queue tokens collapse to queue order. Caller owns the token array until submit returns.
struct QueueSubmissionDesc{
    const QueueSubmissionToken* waitTokens = nullptr;
    usize waitTokenCount = 0;
    QueueSubmissionPreSubmitHook preSubmitHook;
    // Recovery may need an exact queue submission even after earlier work consumed every wait.
    b8 forceNativeSubmission = false;


public:
    constexpr QueueSubmissionDesc& setWaitTokens(const Span<const QueueSubmissionToken> value)noexcept{
        waitTokens = value.data();
        waitTokenCount = value.size();
        return *this;
    }

    constexpr QueueSubmissionDesc& setWaitTokens(const QueueSubmissionToken* value, usize count)noexcept{
        waitTokens = value;
        waitTokenCount = count;
        return *this;
    }

    constexpr QueueSubmissionDesc& setPreSubmitHook(const QueueSubmissionPreSubmitHook value)noexcept{
        preSubmitHook = value;
        return *this;
    }
};

// Accepted submission and its emitted dependency waits, after redundant waits are eliminated.
struct QueueSubmissionReceipt{
    QueueSubmissionToken token;
    usize timelineWaitCount = 0u;
};

using WaveLaneCountRange = NWB::Core::HardwareRayTracingPolicy::WaveLaneCountRange;

struct CommandListParameters{
    // Type of the queue that this command list is to be executed on.
    // Dedicated Compute and Transfer queues expose only the command subsets their Vulkan families support.
    CommandQueue::Enum queueType = CommandQueue::Graphics;
    // Device resolves queueType to its primary queue unless an exact queue is set. Packets set it directly.
    GpuPhysicalQueueId physicalQueue;
    // Worker zero is the ordinary serial/direct lease.
    // Ready-frontier recording pairs a nonzero scheduler domain with its local worker index so pools cannot alias one arena shard.
    // Manual nonzero worker indices may leave the domain at zero when the caller deliberately owns that namespace.
    u64 recordingWorkerDomain = 0u;
    u32 recordingWorkerIndex = 0u;
    // NOTE: u32 kept last; append new fields above the domain pair. queueType stays first (reset in setQueueType).


public:
    constexpr CommandListParameters& setQueueType(CommandQueue::Enum value)noexcept{
        queueType = value;
        physicalQueue = {};
        return *this;
    }

    constexpr CommandListParameters& setPhysicalQueue(GpuPhysicalQueueId value)noexcept{
        physicalQueue = value;
        return *this;
    }

    constexpr CommandListParameters& setRecordingWorker(const u64 domain, const u32 index)noexcept{
        recordingWorkerDomain = index == 0u ? 0u : domain;
        recordingWorkerIndex = index;
        return *this;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Represents a sequence of GPU operations submitted through a backend queue.
typedef GraphicsBackend::Handle<CommandList> CommandListHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef GraphicsBackend::Handle<Device> DeviceHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AdapterInfo{
    static constexpr usize s_UuidByteCount = NWB::Core::HardwareRayTracingPolicy::s_AdapterUuidByteCount;
    static constexpr usize s_LuidByteCount = NWB::Core::HardwareRayTracingPolicy::s_AdapterLuidByteCount;


public:
    typedef Array<u8, s_UuidByteCount> UUID;
    typedef Array<u8, s_LuidByteCount> LUID;


public:
    GraphicsString name;
    u64 dedicatedVideoMemory = 0;
    UUID uuid = {};
    LUID luid = {};
    u32 vendorID = 0;
    u32 deviceID = 0;
    b8 hasUUID = false;
    b8 hasLUID = false;


public:
    explicit AdapterInfo(GraphicsArena& arena)noexcept
        : name(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Windowed swap-chain encoding. HDR10 is 10-bit Rec.2020/PQ; scene color stays linear RGBA16F until present.
namespace SwapChainOutputMode{
    static constexpr u8 s_SwapChainOutputModeSDRBase = NWB::Core::HardwareRayTracingPolicy::s_SwapChainOutputModeSDRBase;
    enum Enum : u8{
        SDR = s_SwapChainOutputModeSDRBase,
        HDR10,
    };
};

struct DeviceCreationParameters{
    GraphicsVector<GraphicsString> requiredBackendInstanceExtensions;
    GraphicsVector<GraphicsString> requiredBackendLayers;
    GraphicsVector<GraphicsString> optionalBackendInstanceExtensions;
    GraphicsVector<GraphicsString> optionalBackendLayers;
    u32 swapChainBufferCount = s_SwapChainBufferCount;
    u32 swapChainSampleCount = 1;
    u32 maxFramesInFlight = s_MaxFramesInFlight;
    i32 adapterIndex = -1;
    Format::Enum swapChainFormat = Format::RGBA8_UNORM_SRGB;
    // Low-level devices retain their conservative default; GraphicsRuntime selects Automatic unless overridden.
    HardwareRayTracingPolicy::Enum hardwareRayTracingPolicy = HardwareRayTracingPolicy::Disabled;
    b8 enableDebugRuntime = false;
    b8 headlessDevice = false;
    b8 enableGpuCrashDiagnostics = true;
    // Without HDR10 on this surface, keep the requested SDR format instead of failing creation.
    b8 enableHDR10Output = false;
    // Presentation-image readback. Unsupported surfaces keep a presentable swap chain with swapChainReadbackAvailable=false.
    b8 enableSwapChainReadback = false;
    // Native mesh shaders are optional. Windows ARM64 defaults to compute emulation: extension advertisement alone does not qualify mesh output.
#if defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
    b8 enableNativeMeshShaders = false;
#else
    b8 enableNativeMeshShaders = true;
#endif
    // Best-effort async Compute offload. Combined Graphics+Compute hardware aliases the roles unless a dedicated offload family exists.
    b8 enableAsyncComputeLane = true;
    // Optional transfer transport. Only a distinct transfer-only Vulkan family becomes CommandQueue::Transfer.
    b8 enableTransferQueue = true;
    // Opt-in same-class transports.
    // Extra queues from each active primary family plus one alternate family per class when cross-family routing is on.
    // Graph tasks must allow same-class routing before ordinary work leaves the primary transport.
    b8 enableSameClassMultiQueue = false;
    // Auxiliary same-class transport from another compatible family. Tasks opt in before the compiler routes across it.
    b8 enableCrossFamilySameClassQueueRouting = false;

    GraphicsVector<GraphicsString> requiredBackendDeviceExtensions;
    GraphicsVector<GraphicsString> optionalBackendDeviceExtensions;
    // VK_EXT_debug_utils uses signed IDs; keep the suppression list in that native form.
    GraphicsVector<i32> ignoredValidationMessageIds;

    GpuDescriptorHeapAbi bindlessHeapAbi;
    Path pipelineCacheDirectory;
    Filesystem::FilesystemFactory filesystemFactory;


public:
    explicit DeviceCreationParameters(GraphicsArena& arena)noexcept
        : requiredBackendInstanceExtensions(arena)
        , requiredBackendLayers(arena)
        , optionalBackendInstanceExtensions(arena)
        , optionalBackendLayers(arena)
        , requiredBackendDeviceExtensions(arena)
        , optionalBackendDeviceExtensions(arena)
        , ignoredValidationMessageIds(arena)
        , pipelineCacheDirectory(arena)
    {}
};

struct SwapChainRuntimeState{
    u32 backBufferWidth = s_BackBufferWidth;
    u32 backBufferHeight = s_BackBufferHeight;
    Format::Enum backBufferFormat = Format::RGBA8_UNORM_SRGB;
    SwapChainOutputMode::Enum outputMode = SwapChainOutputMode::SDR;
    b8 vsyncEnabled = false;
    b8 swapChainReadbackAvailable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

