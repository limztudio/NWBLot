// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "coopvec.h"
#include "gpu_descriptor_heap.h"


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

// Renderer-facing execution policy. A lane resolves to one physical backend queue for the lifetime of a device.
// AsyncCompute deliberately resolves to Graphics when a dedicated Compute queue is unavailable or disabled; callers
// should use the resolved queue carried by QueueSubmissionToken rather than assuming a second VkQueue exists.
namespace RenderLane{
    enum Enum : u8{
        Graphics = 0,
        AsyncCompute,

        kCount,
    };
};

// A completion edge produced only by an accepted queue submission. `valid()` is intentionally false for rejected or
// empty work, while an accepted synchronization-only submission may still produce a token for dependency forwarding.
struct QueueSubmissionToken{
    CommandQueue::Enum queue = CommandQueue::kCount;
    u64 value = 0;

    [[nodiscard]] constexpr bool valid()const{
        return static_cast<u32>(queue) < static_cast<u32>(CommandQueue::kCount) && value != 0u;
    }
};

// Submission-local cross-queue dependencies. Same-queue tokens collapse to normal queue order; distinct queue
// tokens become timeline waits on the consuming submission. The caller owns the token array until submit returns.
struct QueueSubmissionDesc{
    const QueueSubmissionToken* waitTokens = nullptr;
    usize waitTokenCount = 0;

    constexpr QueueSubmissionDesc& setWaitTokens(const QueueSubmissionToken* value, usize count){
        waitTokens = value;
        waitTokenCount = count;
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
    // The dedicated Compute queue has a limited subset of methods available.
    CommandQueue::Enum queueType = CommandQueue::Graphics;
    // Logical lane selection is resolved by Device::createCommandList. Keep the resolved physical queue type in this
    // structure so existing command-list validation and upload lifetime tracking remain queue-based.
    RenderLane::Enum renderLane = RenderLane::Graphics;
    bool resolveRenderLane = false;

    constexpr CommandListParameters& setQueueType(CommandQueue::Enum value){
        queueType = value;
        resolveRenderLane = false;
        return *this;
    }
    constexpr CommandListParameters& setRenderLane(RenderLane::Enum value){
        renderLane = value;
        resolveRenderLane = true;
        return *this;
    }
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List


// Represents a sequence of GPU operations submitted through a backend queue.
typedef GraphicsBackend::Handle<CommandList> CommandListHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU crash diagnostics


// Non-owning view into a tracker's stored marker string (or the static not-found sentinel). Returned by value
// so device-lost capture resolves markers WITHOUT allocating on the growable object arena. Consume promptly:
// the view points into GpuCrashMarkerTracker storage that recording could mutate (best-effort at device-lost).
typedef Pair<bool, AStringView> ResolvedMarker;

// On a device-lost the GPU driver reports the payload of the last marker the GPU executed
// (NVIDIA device-diagnostic checkpoints / AMD buffer markers).
// In cases of nested regimes, we want the marker payloads to represent the whole "stack" of regimes.
// GpuCrashMarkerTracker pushes/pops regimes to this stack.
// The payload itself is a 64bit value, so GpuCrashMarkerTracker stores the mappings of strings<->hashes.
// There should be one GpuCrashMarkerTracker per graphics API-level command list.
class GpuCrashMarkerTracker{
public:
    explicit GpuCrashMarkerTracker(GraphicsArena& arena);


public:
    usize pushEvent(const char* name);
    void popEvent();
    ResolvedMarker getEventString(usize hash);


private:
    GraphicsArena& m_arena;
    // Nested marker labels joined by "/" with an offset stack to pop the most recent segment.
    GraphicsString m_eventStack;
    GraphicsVector<usize> m_eventStackOffsets;

    Array<usize, s_MaxGpuCrashMarkerStrings> m_eventHashes;
    usize m_oldestHashIndex;
    GraphicsHashMap<usize, GraphicsString> m_eventStrings;
};

// GpuCrashTracker tracks all Device-level constructs needed when reporting a GPU crash.
// It resolves a last-executed marker payload hash back to the original nested marker string.
// There should be one GpuCrashTracker per Device.
// All command lists will register their GpuCrashMarkerTrackers with the GpuCrashTracker.
class GpuCrashTracker{
public:
    explicit GpuCrashTracker(GraphicsArena& arena);


public:
    void registerGpuCrashMarkerTracker(GpuCrashMarkerTracker& tracker);
    void unRegisterGpuCrashMarkerTracker(GpuCrashMarkerTracker& tracker);

    ResolvedMarker resolveMarker(usize markerHash);


private:
    // Guards the containers below: command lists register/unregister from worker threads (create/destroy)
    // while a device-lost capture iterates them via resolveMarker on another thread. Without this the
    // Set/Deque could be mutated mid-iteration. (Per-tracker locking is impossible — destroyed trackers
    // are copied by value into m_destroyedMarkerTrackers, so GpuCrashMarkerTracker must stay copyable.)
    Futex m_mutex;
    GraphicsSet<GpuCrashMarkerTracker*> m_markerTrackers;
    // Command lists deleted on CPU could still be executing (and crashing) on GPU,
    // so keep a small number of recently destroyed marker trackers
    GraphicsDeque<GpuCrashMarkerTracker> m_destroyedMarkerTrackers;
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
    u32 swapChainSampleCount = 1;
    u32 swapChainSampleQuality = 0;
    u32 maxFramesInFlight = s_MaxFramesInFlight;
    bool enableNvrhiValidationLayer = false;
    bool enableRayTracingExtensions = false;
    // Best-effort default lane. A dedicated compute-only family is used when present; otherwise AsyncCompute
    // resolves to Graphics without failing device creation or fabricating an alias queue.
    bool enableAsyncComputeLane = true;
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
    bool vsyncEnabled = false;
};

struct BackBufferResizeCallbacks{
    void* userData = nullptr;
    void (*beforeResize)(void*) = nullptr;
    void (*afterResize)(void*) = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

