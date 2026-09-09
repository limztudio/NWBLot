// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/task/cpu/scheduler.h>
#include <core/task/gpu/scheduler.h>

#include <core/common/module.h>
#include <core/input/module.h>
#include <core/graphics/runtime/runtime.h>
#include <core/perf/session.h>
#include <core/telemetry/codec.h>
#include <core/telemetry/frame_graph_registry.h>
#include <core/telemetry/session.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Frame{
private:
    using FrameString = TString<Alloc::GlobalArena>;


private:
    static void ApplyPointerScale(void* userData, f32 scaleX, f32 scaleY);


public:
    Frame(void* inst, u16 width, u16 height, const CpuTaskSchedulerConfig& cpuTaskConfig = {});
    ~Frame()noexcept(false);


public:
    bool init();
    bool showFrame();
    bool mainLoop();
    void requestQuit();

public:
    template<typename T>
    inline T& data(){ return static_cast<T&>(m_data); }

public:
    using ProjectUpdateCallback = bool(*)(void* userData, f32 delta);
    using TelemetryUploadCallback = bool(*)(void* userData, const void* bytes, usize byteCount);

public:
    bool startup();
    void cleanup();
    bool update(f32 delta);
    bool render();

public:
    inline void setProjectUpdateCallback(ProjectUpdateCallback callback, void* userData){
        m_projectUpdateCallback = callback;
        m_projectUpdateUserData = userData;
    }

    [[nodiscard]] inline GraphicsRuntime& graphics(){ return m_graphics; }
    [[nodiscard]] inline const GraphicsRuntime& graphics()const{ return m_graphics; }

    [[nodiscard]] inline InputDispatcher& input(){ return m_input; }
    [[nodiscard]] inline const InputDispatcher& input()const{ return m_input; }

    [[nodiscard]] inline Alloc::GlobalArena& projectObjectArena(){ return m_projectObjectArena; }
    [[nodiscard]] inline const Alloc::GlobalArena& projectObjectArena()const{ return m_projectObjectArena; }

    [[nodiscard]] inline CpuTaskScheduler& cpuTasks(){ return m_cpuTasks; }
    [[nodiscard]] inline GpuTaskScheduler& gpuTasks(){ return m_gpuTasks; }

    void setTelemetryCapture(const Telemetry::CaptureOptions& options);
    void setTelemetryUploadCallback(TelemetryUploadCallback callback, void* userData);
    [[nodiscard]] bool flushTelemetryUpload(bool clearAfterUpload = false);

    // Enable/disable perf capture independently of telemetry. Flips BOTH halves of the GPU-timing double gate
    // (the perf-session sink AND the graphics query recorder), so per-pass GPU timestamps are actually collected
    // without standing up a telemetry upload session. Public so a project can opt into a live readout via the
    // ProjectRuntimeContext perfCapture callback (the only other caller is setTelemetryCapture).
    void setPerfCapture(const Perf::CaptureOptions& options);
    // Read-only access to the captured timing data (per-pass cpu/gpu views, memory, frame index). The Session owns
    // the per-scope stats GpuTimingRecorder feeds; this is how a project reads per-pass GPU times for display.
    [[nodiscard]] inline const Perf::Session& perfSession()const{ return m_perfSession; }

    [[nodiscard]] inline Telemetry::FrameGraphRegistry& frameGraphRegistry(){ return m_frameGraphRegistry; }
    [[nodiscard]] inline const Telemetry::FrameGraphRegistry& frameGraphRegistry()const{ return m_frameGraphRegistry; }

    [[nodiscard]] inline FrameString& appliedWindowTitle(){ return m_appliedWindowTitle; }
    [[nodiscard]] inline const FrameString& appliedWindowTitle()const{ return m_appliedWindowTitle; }

    [[nodiscard]] NotNull<const tchar*> windowTitleOrDefault()const;
    [[nodiscard]] inline bool quitRequested()const{ return m_quitRequested; }
    [[nodiscard]] const tchar* syncGraphicsWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus);


private:
    void setupPlatform(void* inst);
    void cleanupPlatform()noexcept;
    bool updateFrame(f32 delta);


private:
    CpuTaskScheduler m_cpuTasks;
    GpuTaskScheduler m_gpuTasks;
    Common::FrameData m_data;

    Alloc::GlobalArena m_graphicsObjectArena;
    FrameString m_appliedWindowTitle;
    GraphicsAllocator m_graphicsAllocator;
    InputDispatcher m_input;

    Alloc::GlobalArena m_projectObjectArena;
    Perf::Session m_perfSession;
    Telemetry::CaptureSession m_telemetrySession;
    Telemetry::FrameGraphRegistry m_frameGraphRegistry;
    Telemetry::TelemetryBytes m_telemetryUploadBytes;

    GraphicsRuntime m_graphics;

    ProjectUpdateCallback m_projectUpdateCallback = nullptr;
    void* m_projectUpdateUserData = nullptr;
    TelemetryUploadCallback m_telemetryUploadCallback = nullptr;
    void* m_telemetryUploadUserData = nullptr;
    bool m_quitRequested = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

