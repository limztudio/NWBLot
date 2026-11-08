// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "framebuffer_capture.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedCausticTransition final : NoCopy{
public:
    static constexpr u64 s_TransitionPhaseFrames = 64u;
    static constexpr f32 s_PoseAnimationSpeed = 1.1f;


private:
    static constexpr u32 s_TransitionPhaseCount = 4u;


private:
    [[nodiscard]] static b8 ShouldCapture(void* context, u64 frame)noexcept;


public:
    explicit SkinnedCausticTransition(ProjectRuntimeContext& context);


public:
    [[nodiscard]] b8 prepare();
    [[nodiscard]] b8 installTransitionCutter(Core::ECS::EntityID character, Core::ECS::World& world);
    void updateTransitionState(Core::ECS::World& world, f64& animationTime);
    void updateTransitionCapture();
    void stop();
    [[nodiscard]] b8 enabled()const noexcept{ return m_transitionEnabled; }


private:
    void foldTransitionTiming(Name scope, u64& lastPublication, Array<u64, s_TransitionPhaseCount>& phaseSamples)noexcept;
    void recordTransitionRoute(u64 frame, Core::ECS::World& world);


private:
    ProjectRuntimeContext& m_context;
    Vector<::Path<Core::Alloc::GlobalArena>, Core::Alloc::GlobalArena> m_capturePaths;
    UniquePtr<FramebufferCapture> m_capture;
    Timer m_transitionWarmupStart;
    u64 m_transitionFirstFrame = 0u;
    u64 m_nextCaptureFrame = 0u;
    u64 m_lastPhotonPublication = 0u;
    u64 m_lastSkinningPublication = 0u;
    Array<u64, s_TransitionPhaseCount> m_phasePhotonSamples{};
    Array<u64, s_TransitionPhaseCount> m_phaseSkinningSamples{};
    Core::ECS::EntityID m_cutterEntity = Core::ECS::s_InvalidEntityId;
    u32 m_transitionPhase = Limit<u32>::s_Max;
    u32 m_transitionSample = 0u;
    Array<b8, s_TransitionPhaseCount> m_phaseRouteRecorded{};
    b8 m_transitionEnabled = false;
    b8 m_transitionLagged = false;
    b8 m_transitionPhotonsEnabled = false;
    b8 m_transitionWarmupStarted = false;
    b8 m_transitionStarted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

