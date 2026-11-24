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

        kCount
    };
};

struct QueueSubmissionNativeSignal{
    Object semaphore = Object(u64{0u});
    u64 value = 0u;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return semaphore.integer != 0u; }
};

using QueueSubmissionPreSubmitCallback = Expected<QueueSubmissionNativeSignal>(*) (void* context, u64 identity, const GpuPhysicalQueueId& executionQueue);

using QueueSubmissionResolvedCallback = b8(*) (void* context, u64 identity, const QueueSubmissionToken& submissionToken)noexcept;

struct QueueSubmissionPreSubmitHook{
    void* context = nullptr;
    u64 identity = 0u;
    QueueSubmissionPreSubmitCallback invoke = nullptr;
    QueueSubmissionResolvedCallback resolved = &QueueSubmissionPreSubmitHook::IgnoreResolution;


private:
    [[nodiscard]] static b8 IgnoreResolution(void*, u64, const QueueSubmissionToken&)noexcept{ return true; }


public:
    template<typename Observer>
    [[nodiscard]] static QueueSubmissionPreSubmitHook Bind(u64 identity, Observer& observer)noexcept{
        QueueSubmissionPreSubmitHook hook;
        hook.context = &observer;
        hook.identity = identity;
        hook.invoke = [](void* context, u64 key, const GpuPhysicalQueueId& queue){
            return static_cast<Observer*>(context)->prepare(key, queue);
        };
        return hook;
    }
    [[nodiscard]] constexpr b8 valid()const noexcept{ return invoke != nullptr && resolved != nullptr; }
};

struct QueueSubmissionDesc{
    const QueueSubmissionToken* waitTokens = nullptr;
    usize waitTokenCount = 0;
    QueueSubmissionPreSubmitHook preSubmitHook;
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

struct QueueSubmissionReceipt{
    QueueSubmissionToken token;
    usize timelineWaitCount = 0u;
};

struct WaveLaneCountRange{
    u32 maxWaveLaneCount;
};

struct CommandListParameters{
    CommandQueue::Enum queueType = CommandQueue::Graphics;
    GpuPhysicalQueueId physicalQueue;
    u64 recordingWorkerDomain = 0u;
    u32 recordingWorkerIndex = 0u;

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


typedef GraphicsBackend::Handle<CommandList> CommandListHandle;


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
    b8 hasUUID = false;
    b8 hasLUID = false;


public:
    explicit AdapterInfo(GraphicsArena& arena)noexcept
        : name(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SwapChainOutputMode{
    static constexpr u8 s_SwapChainOutputModeSDRBase = 0;
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
    HardwareRayTracingPolicy::Enum hardwareRayTracingPolicy = HardwareRayTracingPolicy::Disabled;
    b8 enableDebugRuntime = false;
    b8 headlessDevice = false;
    b8 enableGpuCrashDiagnostics = true;
    b8 enableHDR10Output = false;
    b8 enableSwapChainReadback = false;
#if defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
    b8 enableNativeMeshShaders = false;
#else
    b8 enableNativeMeshShaders = true;
#endif
    b8 enableAsyncComputeLane = true;
    b8 enableTransferQueue = true;
    b8 enableSameClassMultiQueue = false;
    b8 enableCrossFamilySameClassQueueRouting = false;

    GraphicsVector<GraphicsString> requiredBackendDeviceExtensions;
    GraphicsVector<GraphicsString> optionalBackendDeviceExtensions;
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

