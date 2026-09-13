// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_compiler_statistics_probe.h"

#include <impl/ecs_render/module.h>

#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/packet_runtime.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_compiler_statistics{

struct DurationField{
    AStringView name;
    f64 Core::GpuTaskGraphCompileStatistics::*member;
};

struct CountField{
    AStringView name;
    usize Core::GpuTaskGraphCompileStatistics::*member;
};

static constexpr DurationField s_Durations[] = {
    { "declaration", &Core::GpuTaskGraphCompileStatistics::declarationSeconds },
    { "analysis", &Core::GpuTaskGraphCompileStatistics::analysisSeconds },
    { "validation", &Core::GpuTaskGraphCompileStatistics::validationSeconds },
    { "dependency_analysis", &Core::GpuTaskGraphCompileStatistics::dependencyAnalysisSeconds },
    { "hazard_analysis", &Core::GpuTaskGraphCompileStatistics::hazardAnalysisSeconds },
    { "topological_order", &Core::GpuTaskGraphCompileStatistics::topologicalOrderSeconds },
    { "queue_assignment", &Core::GpuTaskGraphCompileStatistics::queueAssignmentSeconds },
    { "planning", &Core::GpuTaskGraphCompileStatistics::planningSeconds },
    { "total", &Core::GpuTaskGraphCompileStatistics::totalSeconds },
    { "packetization", &Core::GpuTaskGraphCompileStatistics::packetizationSeconds },
    { "resource_state_planning", &Core::GpuTaskGraphCompileStatistics::resourceStatePlanningSeconds },
    { "packet_dependency_planning", &Core::GpuTaskGraphCompileStatistics::packetDependencyPlanningSeconds },
};

static constexpr CountField s_Counts[] = {
    { "task", &Core::GpuTaskGraphCompileStatistics::taskCount },
    { "resource", &Core::GpuTaskGraphCompileStatistics::resourceCount },
    { "resource_use", &Core::GpuTaskGraphCompileStatistics::resourceUseCount },
    { "resource_version", &Core::GpuTaskGraphCompileStatistics::resourceVersionCount },
    { "resource_version_edge", &Core::GpuTaskGraphCompileStatistics::resourceVersionEdgeCount },
    { "explicit_dependency", &Core::GpuTaskGraphCompileStatistics::explicitDependencyCount },
    { "inferred_dependency", &Core::GpuTaskGraphCompileStatistics::inferredDependencyCount },
    { "external_dependency", &Core::GpuTaskGraphCompileStatistics::externalDependencyCount },
    { "packet", &Core::GpuTaskGraphCompileStatistics::packetCount },
    { "packet_dependency", &Core::GpuTaskGraphCompileStatistics::packetDependencyCount },
    { "packet_external_dependency", &Core::GpuTaskGraphCompileStatistics::packetExternalDependencyCount },
    { "cross_queue_packet_dependency", &Core::GpuTaskGraphCompileStatistics::crossQueuePacketDependencyCount },
    { "prologue_state_seed", &Core::GpuTaskGraphCompileStatistics::prologueStateSeedCount },
    { "prologue_barrier", &Core::GpuTaskGraphCompileStatistics::prologueBarrierCount },
    { "epilogue_barrier", &Core::GpuTaskGraphCompileStatistics::epilogueBarrierCount },
    { "state_export_barrier", &Core::GpuTaskGraphCompileStatistics::stateExportBarrierCount },
    { "resource_set", &Core::GpuTaskGraphCompileStatistics::resourceSetCount },
    { "direct_resource_use", &Core::GpuTaskGraphCompileStatistics::directResourceUseCount },
    { "expanded_resource_set_member_use", &Core::GpuTaskGraphCompileStatistics::expandedResourceSetMemberUseCount },
};

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererCompilerStatisticsProbe::RendererCompilerStatisticsProbe(Core::GraphicsRuntime& graphics, Core::Alloc::GlobalArena& arena)
    : IRenderPass(graphics)
    , m_samples(arena)
    , m_output(arena)
{}

RendererCompilerStatisticsProbe::~RendererCompilerStatisticsProbe(){ stop(); }

bool RendererCompilerStatisticsProbe::start(Impl::RendererSystem& renderer, const NotNull<const char*> path){
    if(m_enabled || path.get()[0] == '\0')
        return false;
    m_output = path.get();
    m_samples.reserve(s_Capacity);
    m_renderer = &renderer;
    getGraphics().addRenderPassToBack(*this);
    m_registered = true;
    m_enabled = true;
    return true;
}

void RendererCompilerStatisticsProbe::stop(){
    if(m_registered){
        getGraphics().removeRenderPass(*this);
        m_registered = false;
    }
    m_renderer = nullptr;
}

bool RendererCompilerStatisticsProbe::write()const{
    if(!m_enabled)
        return true;
    if(m_registered)
        return false;
    OutputFileStream output(m_output.c_str(), s_FileOpenTruncate);
    if(!output.is_open())
        return false;
    output.precision(17);
    output << "{\"type\":\"compiler_statistics_configuration\",\"schema\":1,\"capacity\":" << s_Capacity << "}\n";
    usize invalid = 0u;
    for(usize index = 0u; index < m_samples.size(); ++index){
        const Sample& sample = m_samples[index];
        AStringView status = sample.valid ? "valid" : "invalid_snapshot";
        if(sample.valid){
            for(const auto& field : __hidden_renderer_compiler_statistics::s_Durations){
                const f64 seconds = sample.compile.*field.member;
                if(!IsFinite(seconds) || seconds < 0.0){
                    status = "invalid_duration";
                    break;
                }
            }
            for(usize previous = 0u; previous < index; ++previous){
                const Sample& prior = m_samples[previous];
                if(
                    prior.valid && prior.compile.graphGeneration == sample.compile.graphGeneration
                    && prior.compile.planGeneration == sample.compile.planGeneration
                    && prior.compile.deviceGeneration == sample.compile.deviceGeneration
                ){
                    status = "duplicate_plan";
                    break;
                }
            }
        }
        if(index != 0u && sample.sourceFrame <= m_samples[index - 1u].sourceFrame)
            status = "non_monotonic_frame";
        output << "{\"type\":\"compiler_statistics\",\"sequence\":" << index
            << ",\"source_frame\":" << sample.sourceFrame << ",\"status\":\"" << status << '\"';
        if(status == "valid"){
            output << ",\"identity\":{\"graph_generation\":" << sample.compile.graphGeneration
                << ",\"plan_generation\":" << sample.compile.planGeneration
                << ",\"device_generation\":" << sample.compile.deviceGeneration
                << ",\"recording_attempt_generation\":" << sample.recordingAttempt << "},\"seconds\":{";
            bool first = true;
            for(const auto& field : __hidden_renderer_compiler_statistics::s_Durations){
                if(!first)
                    output << ',';
                first = false;
                output << '\"' << field.name << "\":" << sample.compile.*field.member;
            }
            output << "},\"counts\":{";
            first = true;
            for(const auto& field : __hidden_renderer_compiler_statistics::s_Counts){
                if(!first)
                    output << ',';
                first = false;
                output << '\"' << field.name << "\":" << sample.compile.*field.member;
            }
            output << "},\"submission\":{\"accepted_packets\":" << sample.acceptedPackets
                << ",\"accepted_tasks\":" << sample.acceptedTasks
                << ",\"rejected_packets\":" << sample.rejectedPackets
                << ",\"rejected_tasks\":" << sample.rejectedTasks << '}';
        }
        else
            ++invalid;
        output << "}\n";
    }
    const u64 overflow = m_attempts - m_samples.size();
    const bool complete = !m_samples.empty() && overflow == 0u;
    output << "{\"type\":\"compiler_statistics_complete\",\"attempts\":" << m_attempts
        << ",\"stored\":" << m_samples.size() << ",\"overflow\":" << overflow
        << ",\"invalid\":" << invalid << ",\"complete\":" << (complete ? "true" : "false") << "}\n";
    output.flush();
    return output.good() && complete;
}

void RendererCompilerStatisticsProbe::render(Core::Framebuffer*){
    if(!m_renderer)
        return;
    ++m_attempts;
    if(m_samples.size() == s_Capacity)
        return;
    Sample sample;
    sample.sourceFrame = getGraphics().getFrameIndex();
    // Serialized tail callback: the renderer completed its record/submit calls, and the next frame has not reset it.
    // valid() proves a coherent plan/attempt snapshot, not presentation success; the offline join supplies that gate.
    const auto statistics = m_renderer->deferredTaskGraphRuntimeStatistics();
    sample.valid = statistics.valid() && statistics.compile.deviceGeneration != 0u;
    if(sample.valid){
        sample.compile = statistics.compile;
        sample.recordingAttempt = statistics.recording.recordingAttemptGeneration;
        sample.acceptedPackets = statistics.submission.acceptedPacketCount;
        sample.acceptedTasks = statistics.submission.acceptedTaskCount;
        sample.rejectedPackets = statistics.submission.rejectedPacketCount;
        sample.rejectedTasks = statistics.submission.rejectedTaskCount;
    }
    m_samples.push_back(Move(sample));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

