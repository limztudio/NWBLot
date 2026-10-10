// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "framebuffer_capture.h"
#include "reflection_optical_scene.h"

#include <impl/ecs_render/reflection/statistics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Impl{
class RendererSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReportReflectionSlicePackets(ProjectRuntimeContext& context, Impl::RendererSystem& renderer,
    u32 rayCapacity, u64 sourceFrame, bool csg);

// Synchronized diagnostic fixture; captures and packet receipts retain exact source-frame evidence.
class ReflectionCsgContextProbe final{
private:
    [[nodiscard]] static bool ShouldCapture(void* owner, u64 sourceFrame)noexcept;


public:
    ReflectionCsgContextProbe(ProjectRuntimeContext& context, Core::ECS::World& world, Impl::RendererSystem& renderer, u32 rayCapacity);
    ~ReflectionCsgContextProbe()noexcept = default;


public:
    [[nodiscard]] bool create();
    [[nodiscard]] bool sampleSynchronizedFrame(const Impl::ReflectionStatistics& statistics);


private:
    [[nodiscard]] bool beginPhase();
    [[nodiscard]] bool startCapture();


private:
    ProjectRuntimeContext& m_context;
    Core::ECS::World& m_world;
    Impl::RendererSystem& m_renderer;
    const u32 m_rayCapacity;
    ReflectionCsgContextEntities m_entities;
    SmokeEnvironmentString m_output;
    UniquePtr<FramebufferCapture> m_capture;
    u64 m_phaseSource = 0u;
    u64 m_targetSource = Limit<u64>::s_Max;
    u64 m_packetSource = Limit<u64>::s_Max;
    u64 m_lastSequence = 0u;
    u64 m_lastGeneration = 0u;
    u32 m_phase = 0u;
    u32 m_completed = 0u;
    bool m_done = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

