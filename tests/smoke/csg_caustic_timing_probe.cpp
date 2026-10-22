// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_caustic_timing_probe.h"

#include <impl/ecs_render/kernel/timing_names.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::GpuTimingScopeDefinition s_CausticGpuTimingScopes[] = {
    Impl::RendererGpuTimingScope::s_Frame,
    Impl::RendererGpuTimingScope::s_CausticPhotons,
    Impl::RendererGpuTimingScope::s_CausticResolve,
    Impl::RendererGpuTimingScope::s_OpaqueRegular,
    Impl::RendererGpuTimingScope::s_DeferredComposite,
    Impl::RendererGpuTimingScope::s_DeferredPresent,
};

static constexpr Name s_CausticCpuTimingScopes[] = {
    Name("graphics.frame"),
    Name("graphics.prepare_resources"),
    Name("graphics.render_passes"),
    Name("graphics.begin_frame"),
    Name("graphics.present"),
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgCausticTimingProbe::CsgCausticTimingProbe(ProjectRuntimeContext& context)noexcept
    : IRenderPass(context.graphics)
    , m_context(context)
    , m_cpu(NWB_TEXT("csg_caustic"), context.objectArena)
    , m_records(context.objectArena)
    , m_native(context.objectArena)
    , m_intervals(context.objectArena)
    , m_output(context.objectArena)
    , m_startPath(context.objectArena)
{}

bool CsgCausticTimingProbe::initialize(const bool diagnostic, const bool csg, const bool hardware, const AStringView outputPath, const AStringView cpuOutputPath, const AStringView startPath){
    if(!m_context.requestQuit || outputPath.empty() || startPath.empty() || diagnostic != !cpuOutputPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: explicit outputs, focus gate and quit callback are required"));
        return false;
    }
    static_assert(LengthOf(s_CausticGpuTimingScopes) == s_GpuScopeCount);
    static_assert(LengthOf(s_CausticCpuTimingScopes) == s_CpuScopeCount);
    m_diagnostic = diagnostic;
    m_output.assign(outputPath.data(), outputPath.size());
    m_startPath = startPath;
    m_records.resize(s_MaxRecords);
    m_records.clear();
    m_native.resize(s_MaxNativeEndpoints);
    m_native.clear();
    m_intervals.resize(s_MaxIntervals);
    m_intervals.clear();
    if(!m_cpu.initialize(diagnostic, true, cpuOutputPath))
        return false;
    auto capture = Core::Perf::CaptureOptions::GpuTimingOnly();
    capture.cpuTiming = diagnostic;
    m_context.setPerfCapture(capture);
    for(const Core::GpuTimingScopeDefinition& scope : s_CausticGpuTimingScopes){
        if(!m_context.graphics.gpuTiming().prepareScopeQueries(scope.identity, m_context.graphics.getDevice(), 32u))
            return false;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticTiming: configured mode={} csg={} hardware={} preparation_presentations=64 warmup_seconds=30 minimum_seconds=30 minimum_presentations=100 minimum_positive_intervals=11 capture=0")
        , diagnostic ? NWB_TEXT("diagnostic") : NWB_TEXT("performance"), csg ? 1u : 0u, hardware ? 1u : 0u
    );
    return true;
}

bool CsgCausticTimingProbe::update(){
    if(m_complete)
        return true;
    const u64 presentations = m_context.graphics.getSuccessfulPresentationCount();
    const u64 frame = m_context.perfSession.frameIndex();
    const Timer now = TimerNow();
    if(!m_prepared){
        const auto started = FileExists(m_startPath);
        if(!started){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: focus gate status failed"));
            return false;
        }
        if(!*started)
            return true;
        if(presentations < s_PreparationPresentations)
            return true;
        const Core::Perf::TimingView& view = m_context.perfSession.gpuTimingView();
        if(!view.valid())
            return true;
        // Steady warm-up starts only after every measured GPU pass has completed real work.
        for(const Core::GpuTimingScopeDefinition& scope : s_CausticGpuTimingScopes){
            bool ready = false;
            for(usize index = 0u; index < view.scopeCount(); ++index){
                if(view.scopeNameAt(index) == scope.identity && view.statsAt(index).valid() && view.statsAt(index).seconds > 0.0)
                    ready = true;
            }
            if(!ready)
                return true;
        }
        m_prepared = true;
        m_preparationBegin = now;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticTiming: preparation presentations={} source={} gpu_ready_scopes=6"), presentations, frame);
    }
    const auto status = m_presentation.observe(presentations, now);
    if(status == PresentationFpsStatus::Invalid){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: native counter or clock regressed"));
        return false;
    }
    if(m_presentation.measurementStarted() && !m_measuring){
        m_measuring = true;
        m_firstSource = frame;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticTiming: begin presentations={} source={} warmup_seconds={}"), presentations, frame, DurationInSeconds<f64>(now, m_preparationBegin));
    }
    if(!m_measuring)
        return true;
    const auto options = m_context.perfSession.captureOptions();
    if(!options.gpuTimingActive() || options.cpuTimingActive() != m_diagnostic || options.memoryActive()){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: capture mode changed inside measurement"));
        return false;
    }
    if(m_native.size() == s_MaxNativeEndpoints || (!m_native.empty() && (frame != m_native.back().source + 1u || presentations > m_native.back().presentations + 1u))){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: native source endpoints are not contiguous or capacity was exceeded"));
        return false;
    }
    m_native.push_back({ .timestamp = now, .source = frame, .presentations = presentations });
    if(!capture(m_context.perfSession.gpuTimingView(), frame, false) || (m_diagnostic && !capture(m_context.perfSession.cpuTimingView(), frame, true)))
        return false;
    if(m_diagnostic && !m_cpu.observe(m_context.perfSession, m_presentation, presentations, false))
        return false;
    const auto common = selectCommonWindow();
    if(!common)
        return false;
    if(!*common)
        return true;
    if((m_diagnostic && !m_cpu.observe(m_context.perfSession, m_presentation, presentations, true)) || !write(frame))
        return false;
    m_complete = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticTiming: common first={} last={} first_source={} end_source={} collection_end={}")
        , m_commonPresentation.firstPresentationCount, m_commonPresentation.lastPresentationCount, m_native[m_commonBegin].source, m_native[m_commonEnd].source, frame
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticTiming: complete mode={} presentations={} seconds={} positive_intervals={}")
        , m_diagnostic ? NWB_TEXT("diagnostic") : NWB_TEXT("performance"), m_commonPresentation.presentations(), m_commonPresentation.wallSeconds, m_positiveIntervals
    );
    m_context.requestQuit();
    return true;
}

void CsgCausticTimingProbe::finish(){
    if(!m_complete)
        NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: shutdown before measurement completion"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CsgCausticTimingProbe::capture(const Core::Perf::TimingView& view, const u64 sourceFrame, const bool cpu){
    if(!view.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: timing view is invalid"));
        return false;
    }
    const usize scopeBegin = cpu ? s_GpuScopeCount : 0u;
    const usize scopeEnd = cpu ? s_ScopeCount : s_GpuScopeCount;
    for(usize scope = scopeBegin; scope < scopeEnd; ++scope){
        const Name& name = cpu ? s_CausticCpuTimingScopes[scope - s_GpuScopeCount] : s_CausticGpuTimingScopes[scope].identity;
        for(usize index = 0u; index < view.scopeCount(); ++index){
            if(view.scopeNameAt(index) != name)
                continue;
            const Core::Perf::TimingStats& stats = view.statsAt(index);
            if(!stats.valid() || stats.publishFrameIndex == m_lastPublication[scope])
                continue;
            const u32 generation = view.scopeAt(index).generation;
            if((m_generations[scope] != 0u && generation != m_generations[scope]) || stats.publishFrameIndex < m_lastPublication[scope]){
                NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: timing identity or publication order changed"));
                return false;
            }
            m_generations[scope] = generation;
            m_lastPublication[scope] = stats.publishFrameIndex;
            if(stats.firstSampleFrameIndex < m_firstSource)
                continue;
            // This fixed fixture emits exactly one accepted range for each selected scope and source frame.
            if(stats.lastSampleFrameIndex >= sourceFrame || stats.firstSampleFrameIndex > stats.lastSampleFrameIndex
                || stats.sampleCount != stats.lastSampleFrameIndex - stats.firstSampleFrameIndex + 1u || m_records.size() == s_MaxRecords
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: source coverage is incomplete, duplicated or outside buffered native endpoints"));
                return false;
            }
            const u32 bit = 1u << static_cast<u32>(scope);
            const usize first = static_cast<usize>(stats.firstSampleFrameIndex - m_firstSource);
            const usize end = static_cast<usize>(stats.lastSampleFrameIndex - m_firstSource + 1u);
            for(usize frame = first; frame < end; ++frame){
                NativeEndpoint& endpoint = m_native[frame];
                if((endpoint.completeMask & bit) != 0u){
                    NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: same-scope publication source ranges overlap"));
                    return false;
                }
                endpoint.completeMask |= bit;
                if(stats.minSeconds > 0.0)
                    endpoint.positiveMask |= bit;
            }
            m_native[first].beginMask |= bit;
            m_native[end].endMask |= bit;
            m_records.push_back({ stats, sourceFrame, static_cast<u32>(scope) });
        }
    }
    return true;
}

Expected<bool> CsgCausticTimingProbe::selectCommonWindow(){
    const u32 mask = (1u << static_cast<u32>(m_diagnostic ? s_ScopeCount : s_GpuScopeCount)) - 1u;
    if(m_commonBegin == Limit<usize>::s_Max){
        for(usize index = 0u; index + 1u < m_native.size(); ++index){
            if(m_native[index].beginMask == mask && m_native[index].completeMask == mask){
                m_commonBegin = index;
                m_commonEnd = index;
                break;
            }
        }
    }
    if(m_commonBegin == Limit<usize>::s_Max)
        return false;
    while(m_commonEnd + 1u < m_native.size() && m_native[m_commonEnd].completeMask == mask)
        ++m_commonEnd;
    usize end = m_commonEnd;
    while(end > m_commonBegin && m_native[end].endMask != mask)
        --end;
    const NativeEndpoint& first = m_native[m_commonBegin];
    const NativeEndpoint& last = m_native[end];
    const f64 seconds = DurationInSeconds<f64>(last.timestamp, first.timestamp);
    if(seconds < s_MinimumMeasurementSeconds || last.presentations < first.presentations + s_MinimumPresentations)
        return false;
    m_intervals.clear();
    usize begin = m_commonBegin;
    u32 positiveMask = 0u;
    m_positiveIntervals = 0u;
    u32 positiveScopeIntervals[s_ScopeCount] = {};
    for(usize frame = begin + 1u; frame <= end; ++frame){
        positiveMask |= m_native[frame - 1u].positiveMask;
        if(DurationInSeconds<f64>(m_native[frame].timestamp, m_native[begin].timestamp) < s_ReportIntervalSeconds && frame != end)
            continue;
        if(m_intervals.size() == s_MaxIntervals){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: native interval capacity exceeded"));
            return MakeUnexpected(Failure{});
        }
        const PresentationFpsSample interval{
            .firstPresentationCount = m_native[begin].presentations,
            .lastPresentationCount = m_native[frame].presentations,
            .wallSeconds = DurationInSeconds<f64>(m_native[frame].timestamp, m_native[begin].timestamp),
        };
        if(interval.presentations() != 0u){
            ++m_positiveIntervals;
            for(usize scope = 0u; scope < (m_diagnostic ? s_ScopeCount : s_GpuScopeCount); ++scope){
                if((positiveMask & (1u << static_cast<u32>(scope))) != 0u)
                    ++positiveScopeIntervals[scope];
            }
        }
        m_intervals.push_back({ interval, m_native[begin].source, m_native[frame].source });
        begin = frame;
        positiveMask = 0u;
    }
    if(m_positiveIntervals < s_MinimumPositiveIntervals)
        return false;
    for(usize scope = 0u; scope < (m_diagnostic ? s_ScopeCount : s_GpuScopeCount); ++scope){
        if(positiveScopeIntervals[scope] < s_MinimumPositiveIntervals)
            return false;
    }
    m_commonPresentation = { .firstPresentationCount = first.presentations, .lastPresentationCount = last.presentations, .wallSeconds = seconds };
    m_commonEnd = end;
    return true;
}

bool CsgCausticTimingProbe::write(const u64 collectionEndSource){
    OutputFileStream output(m_output.c_str(), s_FileOpenTruncate);
    if(!output.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: output open failed"));
        return false;
    }
    output.precision(17);
    const auto& total = m_commonPresentation;
    const u64 firstSource = m_native[m_commonBegin].source;
    const u64 endSource = m_native[m_commonEnd].source;
    output << "NWB_CSG_CAUSTIC_TIMING 2\nmode " << (m_diagnostic ? "diagnostic" : "performance") << '\n';
    output << "window " << total.firstPresentationCount << ' ' << total.lastPresentationCount << ' ' << firstSource << ' ' << endSource << ' ' << total.wallSeconds << '\n';
    const auto& collection = m_presentation.total();
    output << "collection " << collection.firstPresentationCount << ' ' << collection.lastPresentationCount << ' ' << m_firstSource << ' ' << collectionEndSource << ' ' << collection.wallSeconds << '\n';
    for(usize scope = 0u; scope < s_GpuScopeCount; ++scope)
        output << "scope " << scope << ' ' << s_CausticGpuTimingScopes[scope].identity.resolvedText() << '\n';
    for(const Interval& interval : m_intervals){
        output << "interval " << interval.presentation.firstPresentationCount << ' ' << interval.presentation.lastPresentationCount << ' ' << interval.firstSource << ' ' << interval.endSource << ' ' << interval.presentation.wallSeconds << '\n';
    }
    usize recordCount = 0u;
    for(const Record& row : m_records){
        const auto& stats = row.stats;
        if(row.scope >= s_GpuScopeCount || stats.firstSampleFrameIndex < firstSource || stats.lastSampleFrameIndex >= endSource)
            continue;
        ++recordCount;
        output << "gpu " << row.scope << ' ' << row.observation << ' ' << stats.publishFrameIndex << ' ' << stats.firstSampleFrameIndex << ' '
            << stats.lastSampleFrameIndex << ' ' << stats.sampleCount << ' ' << stats.seconds << ' ' << stats.minSeconds << ' ' << stats.maxSeconds << ' ' << stats.lastSeconds << '\n'
        ;
    }
    output << "complete " << m_intervals.size() << ' ' << recordCount << '\n';
    output.close();
    if(!output.good()){
        NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: output write or close failed"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

