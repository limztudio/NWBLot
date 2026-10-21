// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_csg_context_probe.h"

#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/reflection/task_graph_reflection.h>
#include <impl/ecs_csg/components.h>

#include <core/alloc/scratch.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/telemetry/frame_graph_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReportReflectionSlicePackets(ProjectRuntimeContext& context, Impl::RendererSystem& renderer, const u32 rayCapacity, const u64 sourceFrame, const bool csg){
    NWB::Core::Telemetry::FrameGraphNodeDescs nodes(context.objectArena);
    NWB::Core::Telemetry::FrameGraphEdgeDescs edges(context.objectArena);
    NWB::Core::Telemetry::FrameGraphPendingNameEdges pendingEdges(context.objectArena);
    NWB::Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords queueStatistics(context.objectArena);
    NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetStatistics(context.objectArena);
    NWB::Core::Alloc::ScratchArena scratchArena(Name("tests/smoke/reflection_frame_graph"));
    NWB::Core::Telemetry::FrameGraphBuilder builder(scratchArena, nodes, edges, pendingEdges, queueStatistics, packetStatistics, sourceFrame);
    const bool graphAvailable = renderer.appendFrameGraph(builder);
    const auto primaryGraphics = context.graphics.getDevice().getPrimaryPhysicalQueue(NWB::Core::CommandQueue::Graphics);
    u64 planGeneration = 0u;
    bool runtimePresent = false;
    for(const auto& node : nodes){
        if(node.name == Name("ecs_render/frame") && node.runtimeStatistics.present){
            // Native packet tables are exported only for the renderer's actual source frame, not the builder's label.
            runtimePresent = graphAvailable && node.runtimeStatistics.deviceGeneration == primaryGraphics.deviceGeneration;
            planGeneration = node.runtimeStatistics.planGeneration;
            break;
        }
    }
    const u32 expectedSlices = csg ? (rayCapacity + NWB_REFLECTION_TRACE_SLICE_RAYS - 1u) / NWB_REFLECTION_TRACE_SLICE_RAYS : 1u;
    Vector<u32, Core::Alloc::ScratchArena> uniquePackets(scratchArena);
    uniquePackets.reserve(expectedSlices);
    Vector<Name, Core::Alloc::ScratchArena> sliceIdentities(scratchArena);
    sliceIdentities.reserve(expectedSlices);
    for(u32 slice = 0u; slice < expectedSlices; ++slice){
        if(!csg){
            sliceIdentities.push_back(Name("render.reflection.hardware"));
            continue;
        }
        const auto sliceIdentity = NWB::Impl::RendererTaskGraphDetail::ReflectionCsgHardwareSliceIdentity(slice);
        if(!sliceIdentity){
            NWB_LOGGER_ERROR(NWB_TEXT("Reflection CSG context probe: failed to format hardware slice identity: {}"), StringConvert(sliceIdentity.error().message()));
            return;
        }
        sliceIdentities.push_back(*sliceIdentity);
    }
    Vector<u8, Core::Alloc::ScratchArena> observedSlices(scratchArena);
    observedSlices.resize(expectedSlices, 0u);
    u32 hardwareNodes = 0u;
    u32 indexedSlices = 0u;
    u32 compiledNodes = 0u;
    u32 acceptedPackets = 0u;
    u32 singleTaskPackets = 0u;
    u32 graphicsPackets = 0u;
    for(const auto& node : nodes){
        if(node.label != "Reflection Hardware Resolve")
            continue;
        ++hardwareNodes;
        u32 sliceIndex = expectedSlices;
        for(u32 index = 0u; index < expectedSlices; ++index){
            if(node.name == sliceIdentities[index]){
                sliceIndex = index;
                break;
            }
        }
        if(sliceIndex == expectedSlices || observedSlices[sliceIndex] != 0u)
            continue;
        observedSlices[sliceIndex] = 1u;
        ++indexedSlices;
        if(!runtimePresent || !node.compiledTask.present || node.compiledTask.planGeneration != planGeneration)
            continue;
        ++compiledNodes;
        bool unique = true;
        for(const u32 packetIndex : uniquePackets){
            if(packetIndex == node.compiledTask.packetIndex){
                unique = false;
                break;
            }
        }
        if(unique)
            uniquePackets.push_back(node.compiledTask.packetIndex);
        if(!node.queueAssignment.present || !node.queueAssignment.acceptedQueue.valid() || node.queueAssignment.acceptance == NWB::Core::Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted)
            continue;
        for(const auto& packet : packetStatistics){
            if(packet.packetIndex != node.compiledTask.packetIndex || packet.packetGeneration != planGeneration || packet.queue != node.queueAssignment.acceptedQueue || packet.commandListCount == 0u || packet.recoverySubmission)
                continue;
            ++acceptedPackets;
            if(packet.taskCount == 1u)
                ++singleTaskPackets;
            if(packet.queue.index == primaryGraphics.index && packet.queue.deviceGeneration == primaryGraphics.deviceGeneration)
                ++graphicsPackets;
            break;
        }
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeSlicePackets: source_frame={} plan_generation={} device_generation={}")
        NWB_TEXT(" graphics_queue={} runtime_present={} ray_capacity={} expected_slices={}")
        NWB_TEXT(" hardware_nodes={} indexed_slices={} compiled_nodes={} unique_packets={} accepted_packets={} single_task_packets={} graphics_packets={}")
        , sourceFrame, planGeneration, primaryGraphics.deviceGeneration, primaryGraphics.index, runtimePresent ? 1u : 0u
        , rayCapacity, expectedSlices, hardwareNodes, indexedSlices, compiledNodes, uniquePackets.size()
        , acceptedPackets, singleTaskPackets, graphicsPackets
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ReflectionCsgContextProbe::ShouldCapture(void* owner, const u64 sourceFrame)noexcept{
    return sourceFrame == static_cast<ReflectionCsgContextProbe*>(owner)->m_targetSource;
}

ReflectionCsgContextProbe::ReflectionCsgContextProbe(ProjectRuntimeContext& context, Core::ECS::World& world, Impl::RendererSystem& renderer, const u32 rayCapacity)noexcept
    : m_context(context)
    , m_world(world)
    , m_renderer(renderer)
    , m_capturePaths(context.objectArena)
    , m_rayCapacity(rayCapacity)
{}

bool ReflectionCsgContextProbe::create(Core::Alloc::ScratchArena& scratchArena){
    const auto output = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
    if(!output){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionCsgContext: application framebuffer capture is required"));
        return false;
    }
    const auto scene = CreateReflectionCsgContextScene(m_context, m_world);
    if(!scene)
        return false;
    m_capturePaths.reserve(5u);
    for(u32 phase = 0u; phase < 5u; ++phase){
        const auto path = phase == 4u ? SmokeEnvironmentString(*output, m_context.objectArena) : StringFormat(m_context.objectArena, "{}.{}.bmp", *output, phase);
        m_capturePaths.emplace_back(m_context.objectArena, AStringView(path));
    }
    const FramebufferCaptureOptions options{ .shouldCapture = ShouldCapture, .predicateContext = this, .requiredWidth = 960u, .requiredHeight = 720u, .quitWhenReady = false, };
    m_capture = MakeUnique<FramebufferCapture>(m_context, Move(m_capturePaths.front()), 1u, options);
    if(!m_capture || !m_capture->start())
        return false;
    m_entities = *scene;
    return beginPhase();
}

bool ReflectionCsgContextProbe::sampleSynchronizedFrame(const Impl::ReflectionStatistics& statistics){
    // Wait before advancing the fixture/world; a later wait cannot recover skipped latest-only diagnostic rows.
    if(!m_context.graphics.waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionCsgContext: diagnostic frame synchronization failed"));
        return false;
    }
    if(m_done)
        return true;
    const u64 frame = m_context.graphics.getFrameIndex();
    if(frame > 0u && frame - 1u != m_packetSource){
        m_packetSource = frame - 1u;
        ReportReflectionSlicePackets(m_context, m_renderer, m_rayCapacity, m_packetSource, m_phase != 2u);
    }
    if(statistics.sequence != m_lastSequence || statistics.generation != m_lastGeneration){
        m_lastSequence = statistics.sequence;
        m_lastGeneration = statistics.generation;
        if(statistics.frameIndex >= 3u && statistics.graphicsFrameIndex >= m_phaseSource && statistics.acceptedToken.valid())
            ++m_completed;
    }
    m_capture->update();
    if(m_capture->captureReady()){
        const u64 source = m_capture->capturedGraphicsFrameIndex();
        if(source != m_targetSource || source < m_phaseSource || m_completed < 16u){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionCsgContext: capture missed the qualified phase source frame"));
            return false;
        }
        if(statistics.graphicsFrameIndex < source)
            return true;
        if(statistics.graphicsFrameIndex != source){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionCsgContext: captured source={} skipped by completed source={} sequence={} generation={}"), source, statistics.graphicsFrameIndex, statistics.sequence, statistics.generation);
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionCsgContext: captured phase={} source_frame={} completed_frames={}"), m_phase, source, m_completed);
        if(m_phase == 4u){
            m_done = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionCsgContext: complete captures=5"));
            m_capture->finish();
            return true;
        }
        ++m_phase;
        if(!m_capture->rearm(Move(m_capturePaths[m_phase])))
            return false;
        return beginPhase();
    }
    if(m_targetSource == Limit<u64>::s_Max && m_completed >= 16u)
        m_targetSource = frame;
    return true;
}

bool ReflectionCsgContextProbe::beginPhase(){
    const bool small = m_phase == 0u || m_phase == 3u;
    const bool csg = m_phase != 2u;
    m_world.entity(m_entities.smallReceiver).getComponent<Impl::RendererComponent>().visible = small;
    // Keep the inactive dense receiver in the current scene during small phases, away from all reflected probe rays.
    m_world.entity(m_entities.denseReceiver).getComponent<Impl::Scene::TransformComponent>().position.x = small ? 40.f : 0.f;
    m_world.entity(m_entities.smallCutter).getComponent<Impl::CsgCutterComponent>().active = small;
    m_world.entity(m_entities.denseCutter).getComponent<Impl::CsgCutterComponent>().active = !small && csg;
    m_phaseSource = m_context.graphics.getFrameIndex();
    m_completed = 0u;
    m_targetSource = Limit<u64>::s_Max;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionCsgContext: phase={} source_frame={} cut_receiver_primitives={} csg={}"), m_phase, m_phaseSource, small ? 12u : csg ? 108u : 0u, csg ? 1u : 0u);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

