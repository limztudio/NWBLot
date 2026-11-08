// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinning_culling_runtime_provider.h"
#include "skinning_benchmark_pose_animator.h"
#include "skinning_benchmark_scene.h"
#include "smoke_capture_teardown.h"
#include "smoke_project_helpers.h"
#include "smoke_skinned_scene_helpers.h"
#include "gpu_pass_timing_probe.h"

#include <tests/smoke/presentation_types.adhd.h>

#include <loader/project_entry.h>

#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_mesh/skinning/system.h>

#include <core/alloc/scratch.h>
#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>

#include <global/assert.h>
#include <global/environment.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinningCullingBenchmarkImpl{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_UnknownLabel = "unknown";
constexpr TStringView s_ProjectName = NWB_TEXT("SkinningCullingBenchmark");

using BenchmarkModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using BenchmarkMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using NWB::Tests::Smoke::CreateSmokeSkinnedRenderWorldOrDie;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::LoadSmokeSkeletonBindJoints;
using NWB::Tests::Smoke::ReadSmokeEnvironmentFlag;
using NWB::Tests::Smoke::TickSmokeWorld;

namespace BenchmarkView{
    enum Enum : u8{
        Front,
        Back,
        Close,
        Far,
    };
};

struct BenchmarkCase{
    BenchmarkMode::Enum mode = BenchmarkMode::NoCulling;
    BenchmarkView::Enum view = BenchmarkView::Front;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_CharacterCount = 64u;
static constexpr u32 s_BenchmarkRepeatCount = 4u;
static constexpr u32 s_WarmupFrameCount = 8u;
static constexpr u32 s_SampleFrameCount = 16u;
static constexpr u32 s_FinishDrainFrameCount = 30u;
// Keeps the full benchmark as the default while letting smoke runs exercise animated skinning uploads without a long soak.
static constexpr u32 s_FastSmokeCharacterCount = 1u;
static constexpr u32 s_FastSmokeWarmupFrameCount = 1u;
static constexpr u32 s_FastSmokeSampleFrameCount = 3u;
static constexpr u32 s_FastSmokeFinishDrainFrameCount = 1u;
static constexpr f32 s_FrontCameraDistance = 3.2f;
static constexpr f32 s_BackCameraDistance = 3.2f;
static constexpr f32 s_CloseCameraDistance = 1.65f;
static constexpr f32 s_FarCameraDistance = 5.6f;
static constexpr f32 s_CameraHeight = 1.1f;
static constexpr BenchmarkModelRef s_BenchmarkModel{"project/characters/body/model"};
static constexpr BenchmarkMaterialRef s_SkinningBenchmarkMaterial{"project/smoke/skinning_culling_benchmark/materials/solid"};
static constexpr StringView s_StaticPreviewEnv = "NWB_SKINNING_CULLING_STATIC_PREVIEW";
static constexpr StringView s_FastSmokeEnv = "NWB_SKINNING_CULLING_FAST_SMOKE";
static constexpr StringView s_GpuTimingEnv = "NWB_SKINNING_CULLING_GPU_TIMING";
using NWB::Tests::Smoke::s_SmokeNameSkeleton;

static constexpr Array<BenchmarkCase, 12> s_BenchmarkCases = {{
    { BenchmarkMode::NoCulling, BenchmarkView::Front },
    { BenchmarkMode::NoCulling, BenchmarkView::Back },
    { BenchmarkMode::NoCulling, BenchmarkView::Close },
    { BenchmarkMode::NoCulling, BenchmarkView::Far },
    { BenchmarkMode::FrustumOnly, BenchmarkView::Front },
    { BenchmarkMode::FrustumOnly, BenchmarkView::Back },
    { BenchmarkMode::FrustumOnly, BenchmarkView::Close },
    { BenchmarkMode::FrustumOnly, BenchmarkView::Far },
    { BenchmarkMode::FrustumCone, BenchmarkView::Front },
    { BenchmarkMode::FrustumCone, BenchmarkView::Back },
    { BenchmarkMode::FrustumCone, BenchmarkView::Close },
    { BenchmarkMode::FrustumCone, BenchmarkView::Far },
}};
static constexpr usize s_BenchmarkCaseCount = s_BenchmarkCases.size();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AStringView BenchmarkModeName(const BenchmarkMode::Enum mode)noexcept{
    switch(mode){
    case BenchmarkMode::NoCulling: return "no_culling";
    case BenchmarkMode::FrustumOnly: return "frustum_only";
    case BenchmarkMode::FrustumCone: return "frustum_cone";
    default: return s_UnknownLabel;
    }
}

[[nodiscard]] static AStringView BenchmarkViewName(const BenchmarkView::Enum view)noexcept{
    switch(view){
    case BenchmarkView::Front: return "front";
    case BenchmarkView::Back: return "back";
    case BenchmarkView::Close: return "close";
    case BenchmarkView::Far: return "far";
    default: return s_UnknownLabel;
    }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinningCullingBenchmarkProject final : public NWB::IProjectEntryCallbacks{
public:
    explicit SkinningCullingBenchmarkProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateSmokeSkinnedRenderWorldOrDie(s_ProjectName, {}, context))
        , m_entities(context.objectArena)
        , m_bindJoints(context.objectArena)
        , m_staticPreview(ReadSmokeEnvironmentFlag(s_StaticPreviewEnv))
        , m_fastSmoke(!m_staticPreview && ReadSmokeEnvironmentFlag(s_FastSmokeEnv))
    {}
    virtual ~SkinningCullingBenchmarkProject()noexcept override{
        destroyWorld();
    }


public:
    virtual b8 onStartup()override{
        if(!m_staticPreview)
            m_context.setTelemetryCapture(NWB::Core::Telemetry::CaptureOptions::PerfOnly());
        if(m_gpuTimingEnabled)
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());

        auto bindJoints = LoadSmokeSkeletonBindJoints(s_BenchmarkModel, s_ProjectName, s_SmokeNameSkeleton, true, m_context.objectArena, m_context.assetManager);
        if(!bindJoints){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: benchmark mesh has no skeleton joints"));
            requestQuit();
            return true;
        }
        m_bindJoints = Move(*bindJoints);
        NWB::Impl::MeshSystem& meshSystem = *MakeNotNull(m_world->getSystem<NWB::Impl::MeshSystem>());
        NWB::Impl::MeshSkinningSystem& meshSkinningSystem = *MakeNotNull(m_world->getSystem<NWB::Impl::MeshSkinningSystem>());

        m_runtimeMeshProvider.install(meshSystem, meshSkinningSystem);

        const u32 characterCount = m_staticPreview ? 1u : (m_fastSmoke ? s_FastSmokeCharacterCount : s_CharacterCount);
        if(!NWB::Tests::Smoke::SkinningBenchmarkScene::Populate(characterCount, m_staticPreview, s_BenchmarkModel, s_SkinningBenchmarkMaterial,
            s_SmokeNameSkeleton, Float4(0.0f, s_CameraHeight, -s_FrontCameraDistance, 0.0f), *m_world, m_entities, m_cameraEntity)){
            requestQuit();
            return true;
        }

        if(m_staticPreview){
            m_runtimeMeshProvider.setMode(BenchmarkMode::NoCulling);
            configureCamera(BenchmarkView::Front);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: static preview enabled; close the window manually when done"));
        }
        else
            configureCase();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: spawned {} characters with {} joints each"), characterCount, static_cast<u32>(m_bindJoints.size()));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: shutdown"));
        if(!m_staticPreview){
            if(!m_context.flushTelemetryUpload(true))
                NWB_LOGGER_WARNING(NWB_TEXT("SkinningCullingBenchmark: failed to flush telemetry upload during shutdown"));
        }
    }

    virtual b8 onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) && delta > 0.0f ? delta : 1.0f / 60.0f;
        if(m_gpuTimingEnabled)
            m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        if(m_finished){
            if(m_finishDrainFrames > 0u){
                --m_finishDrainFrames;
                return true;
            }

            requestQuit();
            return true;
        }

        if(m_staticPreview){
            m_totalTimeSeconds = m_totalTimeSeconds + static_cast<f64>(safeDelta);
            NWB::Tests::Smoke::SkinningBenchmarkPoseAnimator::Animate(*m_world, { m_entities.data(), m_entities.size() }, { m_bindJoints.data(), m_bindJoints.size() }, static_cast<f32>(m_totalTimeSeconds), m_staticPreview);
            if(!TickSmokeWorld(safeDelta, *m_world))
                return false;
            ++m_caseRenderedFrames;
            return true;
        }

        advanceCaseOrFinish();
        if(m_finished)
            return true;

        m_totalTimeSeconds = m_totalTimeSeconds + static_cast<f64>(safeDelta);
        NWB::Tests::Smoke::SkinningBenchmarkPoseAnimator::Animate(*m_world, { m_entities.data(), m_entities.size() }, { m_bindJoints.data(), m_bindJoints.size() }, static_cast<f32>(m_totalTimeSeconds), m_staticPreview);
        if(!TickSmokeWorld(safeDelta, *m_world))
            return false;
        ++m_caseRenderedFrames;
        return true;
    }


private:
    void destroyWorld(){
        if(!m_world.owner())
            return;

        m_runtimeMeshProvider.uninstall();
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    void requestQuit(){
        if(m_quitRequested)
            return;

        m_quitRequested = true;
        if(m_context.requestQuit)
            m_context.requestQuit();
    }

    void configureCamera(const BenchmarkView::Enum view)noexcept{
        auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(m_cameraEntity);
        if(!transform)
            return;

        switch(view){
        case BenchmarkView::Back:
            transform->position = Float4(0.0f, s_CameraHeight, s_BackCameraDistance, 0.0f);
            StoreFloat(QuaternionRotationRollPitchYaw(0.0f, s_PI, 0.0f), transform->rotation);
            return;
        case BenchmarkView::Close:
            transform->position = Float4(0.0f, s_CameraHeight, -s_CloseCameraDistance, 0.0f);
            transform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        case BenchmarkView::Far:
            transform->position = Float4(0.0f, s_CameraHeight, -s_FarCameraDistance, 0.0f);
            transform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        case BenchmarkView::Front:
        default:
            transform->position = Float4(0.0f, s_CameraHeight, -s_FrontCameraDistance, 0.0f);
            transform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }
    }

    [[nodiscard]] u32 repeatCount()const noexcept{
        return m_fastSmoke ? 1u : s_BenchmarkRepeatCount;
    }

    [[nodiscard]] usize caseCount()const noexcept{
        return m_fastSmoke ? 1u : s_BenchmarkCaseCount;
    }

    [[nodiscard]] u32 warmupFrameCount()const noexcept{
        return m_fastSmoke ? s_FastSmokeWarmupFrameCount : s_WarmupFrameCount;
    }

    [[nodiscard]] u32 sampleFrameCount()const noexcept{
        return m_fastSmoke ? s_FastSmokeSampleFrameCount : s_SampleFrameCount;
    }

    [[nodiscard]] u32 finishDrainFrameCount()const noexcept{
        return m_fastSmoke ? s_FastSmokeFinishDrainFrameCount : s_FinishDrainFrameCount;
    }

    void configureCase(){
        const BenchmarkCase& benchmarkCase = s_BenchmarkCases[m_caseIndex];
        m_runtimeMeshProvider.setMode(benchmarkCase.mode);
        configureCamera(benchmarkCase.view);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: begin repeat={}/{} mode={} view={}"), m_repeatIndex + 1u, repeatCount(), ConvertString(BenchmarkModeName(benchmarkCase.mode)), ConvertString(BenchmarkViewName(benchmarkCase.view)));
    }

    void finishCase(){
        const BenchmarkCase& benchmarkCase = s_BenchmarkCases[m_caseIndex];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: end repeat={}/{} mode={} view={} sample_frames={}")
            , m_repeatIndex + 1u
            , repeatCount()
            , ConvertString(BenchmarkModeName(benchmarkCase.mode))
            , ConvertString(BenchmarkViewName(benchmarkCase.view))
            , sampleFrameCount()
        );
    }

    void finishBenchmark(){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: completed repeats={} cases={} warmup_frames={} sample_frames={}"), repeatCount(), static_cast<u32>(caseCount()), warmupFrameCount(), sampleFrameCount());
    }

    void advanceCaseOrFinish(){
        if(m_caseRenderedFrames < warmupFrameCount() + sampleFrameCount())
            return;

        finishCase();
        ++m_caseIndex;
        m_caseRenderedFrames = 0u;

        if(m_caseIndex >= caseCount()){
            ++m_repeatIndex;
            if(m_repeatIndex >= repeatCount()){
                finishBenchmark();
                m_finished = true;
                m_finishDrainFrames = finishDrainFrameCount();
                return;
            }

            m_caseIndex = 0u;
        }

        configureCase();
    }




private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    BenchmarkRuntimeMeshProvider m_runtimeMeshProvider;
    Vector<NWB::Core::ECS::EntityID, NWB::Core::Alloc::GlobalArena> m_entities;
    Vector<NWB::Impl::SkeletonJointMatrix, NWB::Core::Alloc::GlobalArena> m_bindJoints;
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ s_ProjectName };
    f64 m_totalTimeSeconds = 0.0;
    usize m_caseIndex = 0u;
    NWB::Core::ECS::EntityID m_cameraEntity = NWB::Core::ECS::s_InvalidEntityId;
    u32 m_caseRenderedFrames = 0u;
    u32 m_finishDrainFrames = 0u;
    u32 m_repeatIndex = 0u;
    const b8 m_gpuTimingEnabled = ReadSmokeEnvironmentFlag(s_GpuTimingEnv);
    b8 m_finished = false;
    b8 m_quitRequested = false;
    b8 m_staticPreview = false;
    b8 m_fastSmoke = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return NWB_TEXT("NWB Skinning Culling Benchmark");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<SkinningCullingBenchmarkImpl::SkinningCullingBenchmarkProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

