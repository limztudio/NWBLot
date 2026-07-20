#pragma once


#include <core/global.h>
#include <core/perf/session.h>
#include <core/telemetry/event.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics;
class InputDispatcher;

namespace ECS{
    class World;
};

namespace Alloc{
    class GlobalArena;
    class ThreadPool;
    class JobSystem;
};

namespace Assets{
    class AssetManager;
};

namespace Telemetry{
    class FrameGraphRegistry;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_DefaultProjectFrameClientWidth = 1280u;
inline constexpr u16 s_DefaultProjectFrameClientHeight = 900u;

struct ProjectFrameClientSize{
    u16 width = s_DefaultProjectFrameClientWidth;
    u16 height = s_DefaultProjectFrameClientHeight;
};

struct ProjectRuntimeContext{
    using ShaderPathResolveCallback = Function<bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)>;
    using TelemetryCaptureCallback = Function<void(const Core::Telemetry::CaptureOptions& options)>;
    using TelemetryUploadFlushCallback = Function<bool(bool clearAfterUpload)>;
    using PerfCaptureCallback = Function<void(const Core::Perf::CaptureOptions& options)>;
    using RequestQuitCallback = Function<void()>;

    Core::Graphics& graphics;
    Core::InputDispatcher& input;
    Core::Alloc::GlobalArena& objectArena;
    Core::Alloc::ThreadPool& threadPool;
    Core::Alloc::JobSystem& jobSystem;
    Core::Assets::AssetManager& assetManager;
    Core::Telemetry::FrameGraphRegistry& frameGraphRegistry;
    // Read-only handle to the captured perf data (per-pass cpu/gpu timing views, memory). Owned by the Frame; bound
    // here so a project can read per-pass GPU times (gpuTimingView()) for a live readout.
    const Core::Perf::Session& perfSession;
    ShaderPathResolveCallback shaderPathResolver;
    TelemetryCaptureCallback telemetryCapture;
    TelemetryUploadFlushCallback telemetryUploadFlush;
    // Enable/disable perf capture (flips the GPU-timing double gate) without standing up telemetry upload. A project
    // calls setPerfCapture(Perf::CaptureOptions::GpuTimingOnly()) to start collecting per-pass GPU timestamps.
    PerfCaptureCallback perfCapture;
    RequestQuitCallback requestQuit;

    void setTelemetryCapture(const Core::Telemetry::CaptureOptions& options);
    [[nodiscard]] bool flushTelemetryUpload(bool clearAfterUpload = false);
    void setPerfCapture(const Core::Perf::CaptureOptions& options);
    [[nodiscard]] Core::Perf::TimingView gpuTimingView()const{ return perfSession.gpuTimingView(); }
};


class IProjectEntryCallbacks{
public:
    virtual ~IProjectEntryCallbacks() = default;


public:
    virtual bool onStartup(){
        return true;
    }
    virtual void onShutdown(){
    }

    virtual bool onUpdate(f32 delta){
        static_cast<void>(delta);
        return true;
    }
};


ProjectFrameClientSize QueryProjectFrameClientSize();
const tchar* QueryProjectWindowTitle();
UniquePtr<IProjectEntryCallbacks> CreateProjectEntryCallbacks(ProjectRuntimeContext& context);

bool CreateInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld);
void DestroyInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

