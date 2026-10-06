// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <core/filesystem/factory.h>
#include <core/perf/session.h>
#include <core/telemetry/event.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;
class InputDispatcher;
interface IClipboardService;
interface ITextInputService;

namespace ECS{
    class World;
};

class CpuTaskScheduler;
class GpuTaskScheduler;
class CpuTaskScope;

namespace Alloc{
    class GlobalArena;
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

struct ProjectStartupContext{
    Core::GraphicsRuntime& graphics;
    Core::Alloc::GlobalArena& objectArena;
    // The factory is copied into graphics configuration and must own any captured service lifetimes.
    Core::Filesystem::FilesystemFactory filesystemFactory;
};


struct ProjectRuntimeContext{
    using ShaderPathResolveCallback = Function<bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)>;
    using TelemetryCaptureCallback = Function<void(const Core::Telemetry::CaptureOptions& options)>;
    using TelemetryUploadFlushCallback = Function<bool(bool clearAfterUpload)>;
    using PerfCaptureCallback = Function<void(const Core::Perf::CaptureOptions& options)>;
    using RequestQuitCallback = Function<void()>;

    Core::GraphicsRuntime& graphics;
    Core::InputDispatcher& input;
    Core::IClipboardService& clipboard;
    Core::ITextInputService& textInput;
    Core::Alloc::GlobalArena& objectArena;
    Core::CpuTaskScheduler& cpuTasks;
    Core::GpuTaskScheduler& gpuTasks;
    Core::CpuTaskScope& tasks;
    Core::Assets::AssetManager& assetManager;
    Core::Filesystem::IFilesystem& filesystem;
    Core::Telemetry::FrameGraphRegistry& frameGraphRegistry;
    // Read-only timing and memory capture owned by the Frame.
    const Core::Perf::Session& perfSession;
    ShaderPathResolveCallback shaderPathResolver;
    TelemetryCaptureCallback telemetryCapture;
    TelemetryUploadFlushCallback telemetryUploadFlush;
    // Controls both GPU-timing gates without enabling telemetry upload.
    PerfCaptureCallback perfCapture;
    RequestQuitCallback requestQuit;

    void setTelemetryCapture(const Core::Telemetry::CaptureOptions& options);
    [[nodiscard]] bool flushTelemetryUpload(bool clearAfterUpload = false);
    void setPerfCapture(const Core::Perf::CaptureOptions& options);
    [[nodiscard]] Core::Perf::TimingView gpuTimingView()const noexcept{ return perfSession.gpuTimingView(); }
};


interface IProjectEntryCallbacks{
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
TStringView QueryProjectWindowTitle();
bool ConfigureProjectRuntime(ProjectStartupContext& context);
UniquePtr<IProjectEntryCallbacks> CreateProjectEntryCallbacks(ProjectRuntimeContext& context);

bool CreateInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& outWorld);
void DestroyInitialProjectWorld(ProjectRuntimeContext& context, UniquePtr<Core::ECS::World>& world);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

