// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "coopvec.h"

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
        // Deprecated unsupported compatibility slot. Keep this ordinal stable for external Feature users.
        SamplerFeedback,
        ShaderExecutionReordering,
        ShaderSpecializations,
        SinglePassStereo,
        Spheres,
        VariableRateShading,
        // Deprecated unsupported compatibility slot. Keep this ordinal stable for external Feature users.
        VirtualResources,
        WaveLaneCountMinMax,
        CooperativeVectorInferencing,
        CooperativeVectorTraining,

        kCount
    };
};

// One opaque binary semaphore signal from a submission-local hook. Only the owning native Device may decode it;
// presentation takes one binary signal while timeline dependencies stay token-based.
struct QueueSubmissionNativeSignal{
    Object semaphore = Object(u64{0u});
    u64 value = 0u;

    [[nodiscard]] constexpr bool valid()const noexcept{ return semaphore.integer != 0u; }
};

// Runs just before one validated submission reaches its queue. Returns a binary signal attached directly to that
// submission, never to a queue-global pending list. False is an expected atomic rejection; an exception unwinds to
// the application boundary, so preparation must also leave owner state unchanged.
// retained past resolution or their owner's lifecycle.
using QueueSubmissionPreSubmitCallback = bool(*) (
    void* context,
    u64 identity,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
);

// Called exactly once after successful hook preparation, with the accepted physical-queue timeline token or an
// invalid token when native submission was rejected. Callbacks must resolve one-shot state without throwing or
// synchronously draining Device.
using QueueSubmissionResolvedCallback = bool(*) (
    void* context,
    u64 identity,
    const QueueSubmissionToken& submissionToken
)noexcept;

struct QueueSubmissionPreSubmitHook{
private:
    [[nodiscard]] static bool IgnoreResolution(void*, u64, const QueueSubmissionToken&)noexcept{ return true; }


public:
    void* context = nullptr;
    u64 identity = 0u;
    QueueSubmissionPreSubmitCallback invoke = nullptr;
    QueueSubmissionResolvedCallback resolved = &QueueSubmissionPreSubmitHook::IgnoreResolution;

    [[nodiscard]] constexpr bool valid()const noexcept{ return invoke != nullptr && resolved != nullptr; }
};

// Submission-local cross-queue dependencies. Same-queue tokens collapse to normal queue order; distinct queue
// tokens become timeline waits on the consuming submission. The caller owns the token array until submit returns.
struct QueueSubmissionDesc{
    const QueueSubmissionToken* waitTokens = nullptr;
    usize waitTokenCount = 0;
    QueueSubmissionPreSubmitHook preSubmitHook;
    // Error-recovery paths may require an exact queue timeline submission even after earlier work consumed every
    // pending wait. Normal empty submissions retain their no-op behavior unless this is explicit.
    bool forceNativeSubmission = false;

    constexpr QueueSubmissionDesc& setWaitTokens(const QueueSubmissionToken* value, usize count){
        waitTokens = value;
        waitTokenCount = count;
        return *this;
    }
    constexpr QueueSubmissionDesc& setPreSubmitHook(const QueueSubmissionPreSubmitHook value){
        preSubmitHook = value;
        return *this;
    }
};

struct VariableRateShadingFeatureInfo{
    u32 shadingRateImageTileSize;
};

struct WaveLaneCountMinMaxFeatureInfo{
    u32 minWaveLaneCount;
    u32 maxWaveLaneCount;
};

struct CommandListParameters{
    // Type of the queue that this command list is to be executed on.
    // Dedicated Compute and Transfer queues expose only the command subsets their Vulkan families support.
    CommandQueue::Enum queueType = CommandQueue::Graphics;
    // Device resolves queueType to its primary physical queue unless an exact queue is supplied. Explicit graph
    // packets set the exact queue directly, so command pools, uploads, and ownership handoffs never collapse
    // same-class queues.
    GpuPhysicalQueueId physicalQueue;
    // Worker zero is the ordinary serial/direct lease. Ready-frontier graph recording combines a stable nonzero
    // CpuTaskScheduler domain with its local nonzero worker index so different pools cannot alias one native arena shard.
    // Manual nonzero worker indices may leave the domain at zero when the caller deliberately owns that namespace.
    u64 recordingWorkerDomain = 0u;
    u32 recordingWorkerIndex = 0u;

    constexpr CommandListParameters& setQueueType(CommandQueue::Enum value){
        queueType = value;
        physicalQueue = {};
        return *this;
    }
    constexpr CommandListParameters& setPhysicalQueue(GpuPhysicalQueueId value){
        physicalQueue = value;
        return *this;
    }
    constexpr CommandListParameters& setRecordingWorkerIndex(const u32 value){
        recordingWorkerDomain = 0u;
        recordingWorkerIndex = value;
        return *this;
    }
    constexpr CommandListParameters& setRecordingWorker(const u64 domain, const u32 index){
        recordingWorkerDomain = index == 0u ? 0u : domain;
        recordingWorkerIndex = index;
        return *this;
    }
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List


// Represents a sequence of GPU operations submitted through a backend queue.
typedef GraphicsBackend::Handle<CommandList> CommandListHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU crash diagnostics


// Non-owning view into immutable device-lifetime marker history (or the static not-found sentinel). Recording may
// publish more history concurrently without invalidating the view. The owning GpuCrashTracker must outlive it.
typedef Pair<bool, AStringView> ResolvedMarker;

class GpuCrashTracker;

// On a device-lost the GPU driver reports the payload of the last marker the GPU executed
// (NVIDIA device-diagnostic checkpoints / AMD buffer markers).
// In cases of nested regimes, we want the marker payloads to represent the whole "stack" of regimes.
// GpuCrashMarkerTracker pushes/pops regimes to this stack and interns completed paths in its device tracker.
// The payload itself is a 64bit value resolved through immutable device-lifetime history.
// There should be one GpuCrashMarkerTracker per graphics API-level command list.
class GpuCrashMarkerTracker : NoCopy{
public:
    GpuCrashMarkerTracker(GpuCrashTracker& tracker, GraphicsArena& arena);


public:
    usize pushEvent(const char* name);
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
// There should be one GpuCrashTracker per Device.
// Its concurrent map is append-only: exact paths reuse IDs, while published strings never move or disappear.
class GpuCrashTracker : NoCopy{
    friend class GpuCrashMarkerTracker;


public:
    explicit GpuCrashTracker(GraphicsArena& arena);


public:
    ResolvedMarker resolveMarker(usize markerHash);


private:
    [[nodiscard]] usize internEvent(const GraphicsString& eventString);


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

// A captured GPU crash report (vendor-neutral): the last-executed GPU marker stack and
// device fault information, formatted as text ready to ship to the crash reporter.
struct GpuCrashReport{
    AString<Alloc::PersistentArena> context;
    AString<Alloc::PersistentArena> details;

    // Optional vendor-neutral binary GPU crash dump (e.g. an NVIDIA Aftermath '.nv-gpudmp'),
    // captured alongside the text 'details'. Non-owning view into the capturer's buffer; valid
    // only for the duration of the synchronous DispatchGpuCrash call.
    GpuCrashDumpKind::Enum binaryDumpKind = GpuCrashDumpKind::None;
    const u8* binaryDump = nullptr;
    usize binaryDumpSize = 0u;

    explicit GpuCrashReport(Alloc::PersistentArena& arena)
        : context(arena)
        , details(arena)
    {}
};

// Process-global sink invoked when the graphics backend captures a GPU crash on device-lost.
// The application registers a sink (e.g. forwarding to the crash reporter) so the graphics
// layer stays crash-subsystem-agnostic.
typedef void(*GpuCrashSink)(void* userData, const GpuCrashReport& report);

void RegisterGpuCrashSink(GpuCrashSink sink, void* userData);
void DispatchGpuCrash(const GpuCrashReport& report);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device


typedef GraphicsBackend::Handle<Device> DeviceHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adapter info


struct AdapterInfo{
    static constexpr usize s_UuidByteCount = 16u;
    static constexpr usize s_LuidByteCount = 8u;

    typedef Array<u8, s_UuidByteCount> UUID;
    typedef Array<u8, s_LuidByteCount> LUID;

    GraphicsString name;
    u32 vendorID = 0;
    u32 deviceID = 0;
    u64 dedicatedVideoMemory = 0;

    UUID uuid = {};
    bool hasUUID = false;
    LUID luid = {};
    bool hasLUID = false;

    explicit AdapterInfo(GraphicsArena& arena)
        : name(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instance and device creation parameters


struct InstanceParameters{
    bool enableDebugRuntime = false;
    bool enableWarningsAsErrors = false;
    bool headlessDevice = false;
    bool enableGpuCrashDiagnostics = true;
    bool logBufferLifetime = false;
    bool enablePerMonitorDPI = false;

    GraphicsString backendLibraryName;
    GraphicsVector<GraphicsString> requiredBackendInstanceExtensions;
    GraphicsVector<GraphicsString> requiredBackendLayers;
    GraphicsVector<GraphicsString> optionalBackendInstanceExtensions;
    GraphicsVector<GraphicsString> optionalBackendLayers;

    explicit InstanceParameters(GraphicsArena& arena)
        : backendLibraryName(arena)
        , requiredBackendInstanceExtensions(arena)
        , requiredBackendLayers(arena)
        , optionalBackendInstanceExtensions(arena)
        , optionalBackendLayers(arena)
    {}
};

// The requested/effective presentation encoding for a windowed swap chain. HDR10 uses a
// 10-bit Rec.2020/PQ surface; renderer code keeps scene color in linear RGBA16F until the
// final presentation pass performs that encoding.
namespace SwapChainOutputMode{
    enum Enum : u8{
        SDR = 0,
        HDR10,
    };
};

struct DeviceCreationParameters : public InstanceParameters{
    bool startMaximized = false;
    bool startFullscreen = false;
    bool startBorderless = false;
    bool allowModeSwitch = false;
    i32 windowPosX = s_WindowPositionAuto;
    i32 windowPosY = s_WindowPositionAuto;
    u32 refreshRate = 0;
    u32 swapChainBufferCount = s_SwapChainBufferCount;
    Format::Enum swapChainFormat = Format::RGBA8_UNORM_SRGB;
    // Opt-in preference: if the current surface cannot expose HDR10, creation continues with the
    // requested SDR format instead of rejecting the device or window.
    bool enableHDR10Output = false;
    // Opt-in presentation-image readback. Unsupported surfaces keep a normal presentable swap chain and publish
    // swapChainReadbackAvailable=false instead of failing device creation.
    bool enableSwapChainReadback = false;
    u32 swapChainSampleCount = 1;
    u32 swapChainSampleQuality = 0;
    u32 maxFramesInFlight = s_MaxFramesInFlight;
    bool enableNvrhiValidationLayer = false;
    bool enableRayTracingExtensions = false;
    // Native mesh shaders are optional. Windows ARM64 defaults to the compute-emulation path because extension
    // advertisement alone does not qualify native mesh-output correctness; callers may explicitly opt in.
#if defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
    bool enableNativeMeshShaders = false;
#else
    bool enableNativeMeshShaders = true;
#endif
    // Best-effort asynchronous Compute offload. Universal Graphics+Compute hardware aliases the required roles
    // unless a dedicated offload family exists. Split Graphics-only/Compute-only hardware always creates the
    // functionally required Compute transport, even when this optional preference is disabled.
    bool enableAsyncComputeLane = true;
    // Best-effort optional transfer transport. Only a distinct transfer-only Vulkan family is exposed as a
    // CommandQueue::Transfer; task-graph copy work otherwise falls back to the existing Compute/Graphics queues.
    bool enableTransferQueue = true;
    // Opt-in same-class transports. The backend registers every safe additional queue from each active primary
    // family and, with cross-family routing enabled, one deterministic alternate family for each supported class.
    // Graph tasks must explicitly allow same-class routing before any ordinary work leaves the primary transport.
    bool enableSameClassMultiQueue = false;
    // Permits an auxiliary same-class transport to come from a different compatible Vulkan family. Tasks must
    // separately opt in before the compiler can route across that ownership boundary.
    bool enableCrossFamilySameClassQueueRouting = false;
    i32 adapterIndex = -1;
    bool supportExplicitDisplayScaling = false;
    bool resizeWindowWithDisplayScale = false;

    GraphicsVector<GraphicsString> requiredBackendDeviceExtensions;
    GraphicsVector<GraphicsString> optionalBackendDeviceExtensions;
    // VK_EXT_debug_utils identifies validation messages with a signed message ID. Keep the
    // suppression list in that native representation instead of the retired debug-report
    // callback location token.
    GraphicsVector<i32> ignoredValidationMessageIds;

    GpuDescriptorHeapAbi bindlessHeapAbi;
    Path pipelineCacheDirectory;
    Filesystem::FilesystemFactory filesystemFactory;

    explicit DeviceCreationParameters(GraphicsArena& arena)
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

