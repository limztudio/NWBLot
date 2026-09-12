// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_gather_benchmark_probe.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererGatherBenchmarkProbe::RendererGatherBenchmarkProbe(Core::Alloc::GlobalArena& arena)
    : m_cpu(arena)
    , m_gpu(arena)
{
    m_cpu.reserve(s_SampleFrames);
    m_gpu.reserve((s_WarmupFrames + s_SampleFrames + s_DrainFrames) * s_GpuScopeCount);
}

bool RendererGatherBenchmarkProbe::poll(const Core::Perf::SessionReport& report, const bool memoryEnabled){
    const auto& frame = report.cpuTiming.stats(Name(s_CpuNames[0u]));
    if(!frame.valid() || (m_hasPublication && frame.publishFrameIndex == m_lastPublish))
        return true;
    if((m_hasPublication && frame.publishFrameIndex < m_lastPublish) || frame.sampleCount != 1u || frame.firstSampleFrameIndex != frame.lastSampleFrameIndex)
        return false;
    const auto& failedPreparation = report.cpuTiming.stats(Name("graphics.prepare_resources_failed"));
    if(failedPreparation.valid() && failedPreparation.publishFrameIndex == frame.publishFrameIndex)
        return false;
    const auto& preamble = report.cpuTiming.stats(Name("graphics.frame_preamble"));
    if((!preamble.valid() || preamble.publishFrameIndex != frame.publishFrameIndex) && m_successfulFrames == 0u)
        return true; // Initial hidden-window maintenance is not a rendered frame or part of warm-up.
    CpuSample sample;
    sample.sourceFrame = frame.firstSampleFrameIndex;
    sample.publishFrame = frame.publishFrameIndex;
    for(u32 scope = 0u; scope < s_CpuScopeCount; ++scope){
        const auto& timing = report.cpuTiming.stats(Name(s_CpuNames[scope]));
        if(
            !timing.valid() || timing.sampleCount != 1u || timing.publishFrameIndex != frame.publishFrameIndex
            || timing.firstSampleFrameIndex != sample.sourceFrame || timing.lastSampleFrameIndex != sample.sourceFrame
        )
            return false;
        sample.milliseconds[scope] = timing.seconds * 1000.0;
    }
    if(memoryEnabled){
        for(u32 scope = 0u; scope < s_ArenaScopeCount; ++scope){
            const auto& snapshot = report.memory.snapshot(Name(s_ArenaNames[scope]), Core::Perf::MemorySource::Arena);
            if(snapshot.valid() && snapshot.frameIndex != sample.publishFrame)
                return false;
            sample.memory[scope] = snapshot;
        }
    }
    for(u32 scope = 0u; scope < s_GpuScopeCount; ++scope){
        const auto& timing = report.gpuTiming.stats(Name(s_GpuNames[scope]));
        if(!timing.valid() || (m_hasGpuPublication[scope] && timing.publishFrameIndex == m_gpuPublish[scope]))
            continue;
        if(m_hasGpuPublication[scope] && timing.publishFrameIndex < m_gpuPublish[scope])
            return false;
        m_hasGpuPublication[scope] = true;
        m_gpuPublish[scope] = timing.publishFrameIndex;
        m_gpu.push_back(GpuSample{ timing, scope });
    }
    m_lastPublish = frame.publishFrameIndex;
    m_hasPublication = true;
    ++m_successfulFrames;
    if(m_successfulFrames == s_WarmupFrames)
        m_memoryBaseline = sample.memory;
    if(m_successfulFrames > s_WarmupFrames && m_cpu.size() < s_SampleFrames)
        m_cpu.push_back(Move(sample));
    return true;
}

bool RendererGatherBenchmarkProbe::write(const NotNull<const char*> path, const NotNull<const char*> workload, const bool memoryEnabled, const u32 renderers,
    const u32 runtimeRenderers, const u32 transparentRenderers, const u32 runtimeOwners)const{
    const bool complete = finished() && m_cpu.size() == s_SampleFrames;
    OutputFileStream output(path.get(), s_FileOpenTruncate);
    if(!output.is_open())
        return false;
    output.setf(s_FileFormatFixed, s_FileFormatFloatField);
    output.precision(9);
    output << "{\"type\":\"configuration\",\"schema\":1,\"workload\":\"" << workload.get()
        << "\",\"mode\":\"" << (memoryEnabled ? "memory" : "timing")
        << "\",\"width\":960,\"height\":720,\"warmup\":" << s_WarmupFrames
        << ",\"samples\":" << s_SampleFrames << ",\"drain\":" << s_DrainFrames
        << ",\"successful_frames\":" << m_successfulFrames << ",\"renderers\":" << renderers
        << ",\"runtime_renderers\":" << runtimeRenderers << ",\"transparent_renderers\":" << transparentRenderers
        << ",\"runtime_owners\":" << runtimeOwners
        << ",\"reflection_mode\":\"hardware\",\"hardware_budget\":4096,\"optical_queries\":16"
        << ",\"temporal\":false,\"spatial\":false,\"feedback\":false,\"diagnostics\":false"
        << ",\"fixed_delta_seconds\":0.016666667,\"sampling_seed\":0,\"refraction\":true}\n";
    if(memoryEnabled){
        output << "{\"type\":\"memory_baseline\",\"arenas\":";
        writeMemory(output, m_memoryBaseline);
        output << "}\n";
    }
    for(const CpuSample& sample : m_cpu){
        output << "{\"type\":\"cpu\",\"source_frame\":" << sample.sourceFrame
            << ",\"publish_frame\":" << sample.publishFrame << ",\"scopes_ms\":{";
        for(u32 scope = 0u; scope < s_CpuScopeCount; ++scope){
            if(scope != 0u)
                output << ',';
            output << '"' << s_CpuNames[scope] << "\":" << sample.milliseconds[scope];
        }
        output << '}';
        if(memoryEnabled){
            output << ",\"arenas\":";
            writeMemory(output, sample.memory);
        }
        output << "}\n";
    }
    for(const GpuSample& sample : m_gpu){
        output << "{\"type\":\"gpu\",\"scope\":\"" << s_GpuNames[sample.scope]
            << "\",\"publish_frame\":" << sample.timing.publishFrameIndex
            << ",\"first_source_frame\":" << sample.timing.firstSampleFrameIndex
            << ",\"last_source_frame\":" << sample.timing.lastSampleFrameIndex
            << ",\"total_ms\":" << sample.timing.seconds * 1000.0
            << ",\"samples\":" << sample.timing.sampleCount << "}\n";
    }
    if(complete)
        output << "{\"type\":\"complete\"}\n";
    output.flush();
    return output.good() && complete;
}

void RendererGatherBenchmarkProbe::writeMemory(OutputFileStream& output, const Array<Core::Perf::MemorySnapshot, s_ArenaScopeCount>& snapshots){
    output << '{';
    for(u32 scope = 0u; scope < s_ArenaScopeCount; ++scope){
        if(scope != 0u)
            output << ',';
        const auto& snapshot = snapshots[scope];
        output << '"' << s_ArenaNames[scope] << "\":{\"present\":" << (snapshot.valid() ? "true" : "false")
            << ",\"frame\":" << snapshot.frameIndex << ",\"reserved\":" << snapshot.reservedBytes
            << ",\"used\":" << snapshot.usedBytes << ",\"historical_arena_peak\":" << snapshot.peakUsedBytes
            << ",\"allocations\":" << snapshot.allocationCount << ",\"reallocations\":" << snapshot.reallocationCount
            << ",\"deallocations\":" << snapshot.deallocationCount << '}';
    }
    output << '}';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

