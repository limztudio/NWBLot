
#pragma once


#include "smoke_environment.h"

#include <global/core/common/log.h>
#include <global/core/perf/timing.h>
#include <global/basic_string.h>
#include <global/filesystem.h>
#include <global/name.h>
#include <global/simplemath.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Live per-pass GPU-timing readout, the GPU-side analog of FpsProbe. The renderer already brackets every pass with a
// Core::GpuTimingMeasure scope, so when perf capture is enabled (ProjectRuntimeContext::setPerfCapture) the per-pass
// GPU timestamps land in the Perf::Session's gpu TimingRecorder. This probe is handed that recorder's TimingView each
// frame; it folds each published per-frame per-scope total over a fixed interval and then logs one line per pass with
// the interval average / min / max in milliseconds (NWB_LOGGER_ESSENTIAL_INFO, mirroring FpsProbe's cadence + style).
//
// Notes:
//  - gpu timing is async: GpuTimingRecorder::collect publishes the PREVIOUS frame's queries at the top of render(), so
//    the view a project reads from onUpdate reflects work a frame or more old. That is expected for timestamp queries.
//  - a TimingStats published window is one frame; .seconds is the SUM of that scope's samples that frame (so a pass
//    opened multiple times per frame, e.g. per-draw, reports its total). We average that per-frame total over the
//    interval, and de-dupe by publishFrameIndex so a window is folded at most once even if onUpdate runs without a new
//    gpu publish (e.g. a skipped/occluded frame).
//  - scope names come back as Names; Name::c_str() is the readable source text in a debug build but a hash off debug,
//    so the per-pass labels are readable where GPU perf is normally measured (dbg) and hashed in opt/fin.
class GpuPassTimingProbe final{
public:
    explicit GpuPassTimingProbe(const tchar* label)
        : m_label(label)
    {}

    void recordFrame(const f32 delta, const Core::Perf::TimingView& gpuTiming){
        const f64 safeDelta = IsFinite(delta) && delta > 0.0f ? static_cast<f64>(delta) : 0.0;
        if(safeDelta <= 0.0)
            return;
        if(safeDelta > s_MaxMeasuredFrameSeconds)
            return;

        m_elapsedSeconds += safeDelta;
        if(m_elapsedSeconds < s_WarmupSeconds)
            return;
        if(!gpuTiming.valid())
            return;

        m_intervalSeconds += safeDelta;
        ++m_intervalFrames;
        accumulate(gpuTiming);

        if(m_intervalSeconds < s_ReportIntervalSeconds)
            return; // m_intervalFrames is always >= 1 here (incremented unconditionally above)

        report(gpuTiming);
        resetInterval();
    }


private:
    void accumulate(const Core::Perf::TimingView& gpuTiming){
        const usize scopeCount = Min(gpuTiming.scopeCount(), s_MaxScopes);
        for(usize i = 0u; i < scopeCount; ++i){
            const Core::Perf::TimingStats& stats = gpuTiming.statsAt(i);
            if(!stats.valid())
                continue;

            // Fold each published GPU window at most once. The watermark PERSISTS across interval resets, so a window
            // that straddles a report boundary (onUpdate ran but no new GPU publish yet, e.g. an occluded/skipped
            // frame still returns the prior window) is never double-counted into the next interval. Publish indices
            // strictly increase and are > 0 once accumulation begins (post-warmup), so the 0 default reads as "none
            // folded yet" without colliding with a real window.
            if(m_scopeLastFoldedPublish[i] == stats.publishFrameIndex)
                continue;
            m_scopeLastFoldedPublish[i] = stats.publishFrameIndex;

            ScopeAccum& accum = m_scopes[i];
            if(accum.frames == 0u){
                accum.minSeconds = stats.seconds;
                accum.maxSeconds = stats.seconds;
            }
            else{
                accum.minSeconds = Min(accum.minSeconds, stats.seconds);
                accum.maxSeconds = Max(accum.maxSeconds, stats.seconds);
            }
            accum.sumSeconds += stats.seconds;
            ++accum.frames;
        }
    }

    void report(const Core::Perf::TimingView& gpuTiming){
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("{}: gpu per-pass timing over {} frames / {}s")
            , m_label
            , m_intervalFrames
            , m_intervalSeconds
        );

        // Optional file sink for bounded profiling A/B: the smoke app is a GUI process with no stdout, and its logger routes
        // to the logserver's WINDOW, so an automated harness cannot scrape these numbers. When NWB_GPU_TIMING_FILE is set,
        // ALSO append each interval's per-pass averages there (Name::c_str() is the readable scope text in a dbg build).
        OutputFileStream timingFile;
        OpenTimingFile(timingFile);
        if(timingFile.is_open()){
            timingFile.setf(std::ios::fixed, std::ios::floatfield);
            timingFile.precision(4);
            timingFile << "=== interval: " << static_cast<unsigned>(m_intervalFrames) << " frames / " << m_intervalSeconds << "s ===\n";
        }

        const usize scopeCount = Min(gpuTiming.scopeCount(), s_MaxScopes);
        for(usize i = 0u; i < scopeCount; ++i){
            const ScopeAccum& accum = m_scopes[i];
            if(accum.frames == 0u)
                continue;

            const Name scopeName = gpuTiming.scopeNameAt(i);
            const f64 averageMs = (accum.sumSeconds / static_cast<f64>(accum.frames)) * 1000.0;
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("  {}: gpu_ms avg={} min={} max={} samples={}")
                , StringConvert(scopeName.c_str())
                , averageMs
                , accum.minSeconds * 1000.0
                , accum.maxSeconds * 1000.0
                , accum.frames
            );
            if(timingFile.is_open()){
                timingFile
                    << "  " << scopeName.c_str()
                    << ": avg=" << averageMs
                    << " min=" << accum.minSeconds * 1000.0
                    << " max=" << accum.maxSeconds * 1000.0
                    << " samples=" << static_cast<unsigned>(accum.frames)
                    << '\n'
                ;
            }
        }
    }

    static void OpenTimingFile(OutputFileStream& timingFile){
        Core::Alloc::GlobalArena arena(s_SmokeEnvironmentArena);
        SmokeEnvironmentString timingPath(arena);
        if(!ReadSmokeEnvironmentText("NWB_GPU_TIMING_FILE", timingPath))
            return;

        timingFile.open(timingPath.c_str(), s_FileOpenAppend);
    }

    void resetInterval(){
        m_intervalSeconds = 0.0;
        m_intervalFrames = 0u;
        for(ScopeAccum& accum : m_scopes)
            accum = ScopeAccum{};
    }


private:
    static constexpr f64 s_WarmupSeconds = 0.25;
    static constexpr f64 s_ReportIntervalSeconds = 0.5;
    static constexpr f64 s_MaxMeasuredFrameSeconds = 0.25;
    static constexpr usize s_MaxScopes = 64u;

    struct ScopeAccum{
        f64 sumSeconds = 0.0;
        f64 minSeconds = 0.0;
        f64 maxSeconds = 0.0;
        u32 frames = 0u;
    };

    const tchar* m_label = NWB_TEXT("Smoke");
    f64 m_elapsedSeconds = 0.0;
    f64 m_intervalSeconds = 0.0;
    u32 m_intervalFrames = 0u;
    ScopeAccum m_scopes[s_MaxScopes] = {};
    // Per-scope last-folded publish-frame watermark. NOT reset between intervals (resetInterval only clears
    // m_scopes), so a GPU window folded in one interval is never re-folded into the next at the boundary.
    u64 m_scopeLastFoldedPublish[s_MaxScopes] = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

