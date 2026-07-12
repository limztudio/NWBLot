
#pragma once


#include <global/core/global.h>

#include <global/core/common/module.h>
#include <global/core/input/module.h>
#include <global/core/graphics/module.h>
#include <global/core/perf/session.h>
#include <global/core/telemetry/codec.h>
#include <global/core/telemetry/frame_graph_registry.h>
#include <global/core/telemetry/session.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Frame{
private:
    using FrameString = TString<Alloc::GlobalArena>;


private:
    static constexpr u32 s_ReservedCoresForMainThread = 1;


private:
    static void ApplyPointerScale(void* userData, f32 scaleX, f32 scaleY);
    static u32 queryGraphicsWorkerThreadCount();
    static u32 queryProjectWorkerThreadCount();


public:
    Frame(void* inst, u16 width, u16 height);
    ~Frame();


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

    [[nodiscard]] inline Graphics& graphics(){ return m_graphics; }
    [[nodiscard]] inline const Graphics& graphics()const{ return m_graphics; }

    [[nodiscard]] inline InputDispatcher& input(){ return m_input; }
    [[nodiscard]] inline const InputDispatcher& input()const{ return m_input; }

    [[nodiscard]] inline Alloc::GlobalArena& projectObjectArena(){ return m_projectObjectArena; }
    [[nodiscard]] inline const Alloc::GlobalArena& projectObjectArena()const{ return m_projectObjectArena; }

    [[nodiscard]] inline Alloc::ThreadPool& projectThreadPool(){ return m_projectThreadPool; }
    [[nodiscard]] inline const Alloc::ThreadPool& projectThreadPool()const{ return m_projectThreadPool; }

    [[nodiscard]] inline Alloc::JobSystem& projectJobSystem(){ return m_projectJobSystem; }
    [[nodiscard]] inline const Alloc::JobSystem& projectJobSystem()const{ return m_projectJobSystem; }

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

    [[nodiscard]] const tchar* windowTitleOrDefault()const;
    [[nodiscard]] inline bool quitRequested()const{ return m_quitRequested; }
    [[nodiscard]] const tchar* syncGraphicsWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus);


private:
    void setupPlatform(void* inst);
    void cleanupPlatform();
    bool updateFrame(f32 delta);


private:
    Common::FrameData m_data;

    Alloc::GlobalArena m_graphicsObjectArena;
    FrameString m_appliedWindowTitle;
    GraphicsAllocator m_graphicsAllocator;
    Alloc::ThreadPool m_graphicsThreadPool;
    Alloc::JobSystem m_graphicsJobSystem;
    InputDispatcher m_input;

    Alloc::GlobalArena m_projectObjectArena;
    Perf::Session m_perfSession;
    Telemetry::CaptureSession m_telemetrySession;
    Telemetry::FrameGraphRegistry m_frameGraphRegistry;
    Telemetry::TelemetryBytes m_telemetryUploadBytes;
    Perf::MemoryScopeId m_graphicsObjectArenaMemoryScope;
    Perf::MemoryScopeId m_projectObjectArenaMemoryScope;
    Alloc::ThreadPool m_projectThreadPool;
    Alloc::JobSystem m_projectJobSystem;

    Graphics m_graphics;

    ProjectUpdateCallback m_projectUpdateCallback = nullptr;
    void* m_projectUpdateUserData = nullptr;
    TelemetryUploadCallback m_telemetryUploadCallback = nullptr;
    void* m_telemetryUploadUserData = nullptr;
    bool m_quitRequested = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

