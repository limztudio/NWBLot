// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <loader/runtime_constants.adhd.h>

#include <core/global.h>
#include <core/graphics/shader_archive.h>
#include <core/filesystem/factory.h>
#include <core/perf/session.h>
#include <core/telemetry/event.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;
class InputDispatcher;
interface IAccessibilityService;
interface IClipboardService;
interface ICursorService;
interface ITextInputService;

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


inline constexpr u16 s_DefaultProjectFrameClientWidth = LoaderDetail::s_DefaultProjectFrameClientWidth;
inline constexpr u16 s_DefaultProjectFrameClientHeight = LoaderDetail::s_DefaultProjectFrameClientHeight;

using ProjectFrameClientSize = LoaderDetail::ProjectFrameClientSize;

struct ProjectStartupContext{
    Core::GraphicsRuntime& graphics;
    Core::Alloc::GlobalArena& objectArena;
    Core::Filesystem::FilesystemFactory filesystemFactory;
};


struct ProjectRuntimeContext{
    using ShaderPathResolveCallback = Core::ShaderArchive::PathResolver;
    using TelemetryCaptureCallback = Function<void(const Core::Telemetry::CaptureOptions& options)>;
    using TelemetryUploadFlushCallback = Function<b8(b8 clearAfterUpload)>;
    using PerfCaptureCallback = Function<void(const Core::Perf::CaptureOptions& options)>;
    using RequestQuitCallback = Function<void()>;


public:
    Core::GraphicsRuntime& graphics;
    Core::InputDispatcher& input;
    Core::IClipboardService& clipboard;
    Core::ITextInputService& textInput;
    Core::ICursorService& cursor;
    Core::IAccessibilityService& accessibility;
    Core::Alloc::GlobalArena& objectArena;
    Core::CpuTaskScheduler& cpuTasks;
    Core::GpuTaskScheduler& gpuTasks;
    Core::CpuTaskScope& tasks;
    Core::Assets::AssetManager& assetManager;
    Core::Filesystem::IFilesystem& filesystem;
    Core::Telemetry::FrameGraphRegistry& frameGraphRegistry;
    const Core::Perf::Session& perfSession;
    ShaderPathResolveCallback shaderPathResolver;
    TelemetryCaptureCallback telemetryCapture;
    TelemetryUploadFlushCallback telemetryUploadFlush;
    PerfCaptureCallback perfCapture;
    RequestQuitCallback requestQuit;


public:
    void setTelemetryCapture(const Core::Telemetry::CaptureOptions& options){
        if(telemetryCapture)
            telemetryCapture(options);
    }
    [[nodiscard]] b8 flushTelemetryUpload(const b8 clearAfterUpload = false){
        return telemetryUploadFlush ? telemetryUploadFlush(clearAfterUpload) : true;
    }
    void setPerfCapture(const Core::Perf::CaptureOptions& options){
        if(perfCapture)
            perfCapture(options);
    }
    [[nodiscard]] Core::Perf::TimingView gpuTimingView()const noexcept{ return perfSession.gpuTimingView(); }
};


interface IProjectEntryCallbacks{
public:
    virtual ~IProjectEntryCallbacks()noexcept = default;


public:
    virtual b8 onStartup(){
        return true;
    }

    virtual void onShutdown(){
    }

    virtual b8 onUpdate([[maybe_unused]] f32 delta){
        return true;
    }
};


ProjectFrameClientSize QueryProjectFrameClientSize()noexcept;
TStringView QueryProjectWindowTitle()noexcept;
b8 ConfigureProjectRuntime(ProjectStartupContext& context)noexcept;
UniquePtr<IProjectEntryCallbacks> CreateProjectEntryCallbacks(ProjectRuntimeContext& context);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

