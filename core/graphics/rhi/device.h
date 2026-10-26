// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "coopvec.h"
#include "hardware_ray_tracing_policy.h"

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

        kCount
    };
};

// One opaque binary semaphore signal from a submission-local hook. Only the owning Device decodes it.
struct QueueSubmissionNativeSignal{
    Object semaphore = Object(u64{0u});
    u64 value = 0u;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return semaphore.integer != 0u; }
};

// Prepares a submission-local binary signal atomically; exceptions unwind to the application boundary.
using QueueSubmissionPreSubmitCallback = Expected<QueueSubmissionNativeSignal>(*) (void* context, u64 identity, const GpuPhysicalQueueId& executionQueue);

// Called once after hook preparation with the accepted token (or invalid on rejection). No throw, no Device drain.
using QueueSubmissionResolvedCallback = bool(*) (void* context, u64 identity, const QueueSubmissionToken& submissionToken)noexcept;

struct QueueSubmissionPreSubmitHook{
    void* context = nullptr;
    u64 identity = 0u;
    QueueSubmissionPreSubmitCallback invoke = nullptr;
    QueueSubmissionResolvedCallback resolved = &QueueSubmissionPreSubmitHook::IgnoreResolution;


private:
    [[nodiscard]] static bool IgnoreResolution(void*, u64, const QueueSubmissionToken&)noexcept{ return true; }


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return invoke != nullptr && resolved != nullptr; }
};

// Cross-queue deps. Same-queue tokens collapse to queue order. Caller owns the token array until submit returns.
struct QueueSubmissionDesc{
    const QueueSubmissionToken* waitTokens = nullptr;
    usize waitTokenCount = 0;
    QueueSubmissionPreSubmitHook preSubmitHook;
    // Recovery may need an exact queue submission even after earlier work consumed every wait.
    bool forceNativeSubmission = false;


public:
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

struct VariableRateShadingFeatureInfo{
    u32 shadingRateImageTileSize;
};

struct WaveLaneCountRange{
    u32 minWaveLaneCount;
    u32 maxWaveLaneCount;
};

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

    constexpr CommandListParameters& setRecordingWorkerIndex(const u32 value)noexcept{
        recordingWorkerDomain = 0u;
        recordingWorkerIndex = value;
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


// View into device-lifetime marker history (or the not-found sentinel). GpuCrashTracker must outlive the view.
typedef Pair<bool, AStringView> ResolvedMarker;

class GpuCrashTracker;

// Device-lost reports the last executed marker payload (NV checkpoints / AMD markers); nested regimes stack them.
// One GpuCrashMarkerTracker per API-level command list; 64-bit payload via device-lifetime history.
class GpuCrashMarkerTracker : NoCopy{
public:
    GpuCrashMarkerTracker(GpuCrashTracker& tracker, GraphicsArena& arena)noexcept;


public:
    usize pushEvent(AStringView name);
    void popEvent()noexcept;
    // Clears only active nesting. Published device history remains available for in-flight crash reports.
    void resetEventStack()noexcept;


private:
    GpuCrashTracker& m_tracker;
    // Nested marker labels joined by "/" with an offset stack to pop the most recent segment.
    GraphicsString m_eventStack;
    GraphicsVector<usize> m_eventStackOffsets;
};

// GpuCrashTracker tracks all Device-level constructs needed when reporting a GPU crash.
// It resolves a last-executed marker payload hash back to the original nested marker string.
// There is one GpuCrashTracker per Device.
// Its concurrent map is append-only: exact paths reuse IDs, while published strings never move or disappear.
class GpuCrashTracker : NoCopy{
    friend class GpuCrashMarkerTracker;


public:
    explicit GpuCrashTracker(GraphicsArena& arena)noexcept;


public:
    ResolvedMarker resolveMarker(usize markerHash)noexcept;


private:
    [[nodiscard]] usize internEvent(AStringView eventString);


private:
    GraphicsArena& m_arena;
    ParallelHashMap<usize, GraphicsString, GraphicsArena> m_eventStrings;
};

namespace GpuCrashDumpKind{
    enum Enum : u8{
        None,
        Aftermath,
        RadeonGpuDetective,
    };
};

// Captured GPU crash report: last-executed marker stack and fault info, as text for the crash reporter.
struct GpuCrashReport{
    AString<Alloc::PersistentArena> context;
    AString<Alloc::PersistentArena> details;

    const u8* binaryDump = nullptr;
    usize binaryDumpSize = 0u;
    // Optional binary crash dump (e.g. '.nv-gpudmp'). View; valid only during DispatchGpuCrash.
    GpuCrashDumpKind::Enum binaryDumpKind = GpuCrashDumpKind::None;


public:
    explicit GpuCrashReport(Alloc::PersistentArena& arena)noexcept
        : context(arena)
        , details(arena)
    {}
};

// Process-global sink for backend GPU-crash capture on device-lost. The app registers it (e.g. forward to crash reporter).
typedef void(*GpuCrashSink)(void* userData, const GpuCrashReport& report);

void RegisterGpuCrashSink(GpuCrashSink sink, void* userData)noexcept;
void DispatchGpuCrash(const GpuCrashReport& report);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef GraphicsBackend::Handle<Device> DeviceHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AdapterInfo{
    static constexpr usize s_UuidByteCount = 16u;
    static constexpr usize s_LuidByteCount = 8u;


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
    bool hasUUID = false;
    bool hasLUID = false;


public:
    explicit AdapterInfo(GraphicsArena& arena)noexcept
        : name(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InstanceParameters{
    GraphicsString backendLibraryName;
    GraphicsVector<GraphicsString> requiredBackendInstanceExtensions;
    GraphicsVector<GraphicsString> requiredBackendLayers;
    GraphicsVector<GraphicsString> optionalBackendInstanceExtensions;
    GraphicsVector<GraphicsString> optionalBackendLayers;
    bool enableDebugRuntime = false;
    bool enableWarningsAsErrors = false;
    bool headlessDevice = false;
    bool enableGpuCrashDiagnostics = true;
    bool logBufferLifetime = false;
    bool enablePerMonitorDPI = false;


public:
    explicit InstanceParameters(GraphicsArena& arena)noexcept
        : backendLibraryName(arena)
        , requiredBackendInstanceExtensions(arena)
        , requiredBackendLayers(arena)
        , optionalBackendInstanceExtensions(arena)
        , optionalBackendLayers(arena)
    {}
};

// Windowed swap-chain encoding. HDR10 is 10-bit Rec.2020/PQ; scene color stays linear RGBA16F until present.
namespace SwapChainOutputMode{
    static constexpr u8 s_SwapChainOutputModeSDRBase = 0;
    enum Enum : u8{
        SDR = s_SwapChainOutputModeSDRBase,
        HDR10,
    };
};

struct DeviceCreationParameters : public InstanceParameters{
    i32 windowPosX = s_WindowPositionAuto;
    i32 windowPosY = s_WindowPositionAuto;
    u32 refreshRate = 0;
    u32 swapChainBufferCount = s_SwapChainBufferCount;
    u32 swapChainSampleCount = 1;
    u32 swapChainSampleQuality = 0;
    u32 maxFramesInFlight = s_MaxFramesInFlight;
    i32 adapterIndex = -1;
    Format::Enum swapChainFormat = Format::RGBA8_UNORM_SRGB;
    // Low-level devices retain their conservative default; GraphicsRuntime selects Automatic unless overridden.
    HardwareRayTracingPolicy::Enum hardwareRayTracingPolicy = HardwareRayTracingPolicy::Disabled;
    bool startMaximized = false;
    bool startFullscreen = false;
    bool startBorderless = false;
    bool allowModeSwitch = false;
    // Without HDR10 on this surface, keep the requested SDR format instead of failing creation.
    bool enableHDR10Output = false;
    // Presentation-image readback. Unsupported surfaces keep a presentable swap chain with swapChainReadbackAvailable=false.
    bool enableSwapChainReadback = false;
    // Native mesh shaders are optional. Windows ARM64 defaults to compute emulation: extension advertisement alone does not qualify mesh output.
#if defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
    bool enableNativeMeshShaders = false;
#else
    bool enableNativeMeshShaders = true;
#endif
    // Best-effort async Compute offload. Combined Graphics+Compute hardware aliases the roles unless a dedicated offload family exists.
    bool enableAsyncComputeLane = true;
    // Optional transfer transport. Only a distinct transfer-only Vulkan family becomes CommandQueue::Transfer.
    bool enableTransferQueue = true;
    // Opt-in same-class transports.
    // Extra queues from each active primary family plus one alternate family per class when cross-family routing is on.
    // Graph tasks must allow same-class routing before ordinary work leaves the primary transport.
    bool enableSameClassMultiQueue = false;
    // Auxiliary same-class transport from another compatible family. Tasks opt in before the compiler routes across it.
    bool enableCrossFamilySameClassQueueRouting = false;
    bool supportExplicitDisplayScaling = false;
    bool resizeWindowWithDisplayScale = false;

    GraphicsVector<GraphicsString> requiredBackendDeviceExtensions;
    GraphicsVector<GraphicsString> optionalBackendDeviceExtensions;
    // VK_EXT_debug_utils uses signed IDs; keep the suppression list in that native form.
    GraphicsVector<i32> ignoredValidationMessageIds;

    GpuDescriptorHeapAbi bindlessHeapAbi;
    Path pipelineCacheDirectory;
    Filesystem::FilesystemFactory filesystemFactory;


public:
    explicit DeviceCreationParameters(GraphicsArena& arena)noexcept
        : InstanceParameters(arena)
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
    bool vsyncEnabled = false;
    bool swapChainReadbackAvailable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

