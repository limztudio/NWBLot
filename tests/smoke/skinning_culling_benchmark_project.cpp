// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <core/perf/report.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/timing_names.h>
#include <impl/ecs_skeleton/runtime_helpers.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_mesh/skinning/timing_names.h>

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_culling_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using BenchmarkModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using BenchmarkMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;

namespace RendererTiming = NWB::Impl::RendererGpuTimingScope;
namespace SkinnedTiming = NWB::Impl::MeshSkinningGpuTimingScope;

namespace BenchmarkMode{
    enum Enum : u8{
        NoCulling,
        FrustumOnly,
        FrustumCone,
    };
};

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

struct BenchmarkAccumulation{
    f64 cpuFrameSeconds = 0.0;
    f64 skinningSeconds = 0.0;
    f64 boundsSeconds = 0.0;
    f64 renderFrameSeconds = 0.0;
    f64 deferredClearSeconds = 0.0;
    f64 deferredLightingSeconds = 0.0;
    f64 deferredCompositeSeconds = 0.0;
    f64 materialUploadSeconds = 0.0;
    f64 meshDispatchSeconds = 0.0;
    f64 rasterSeconds = 0.0;
    u32 frameCount = 0u;
    u32 skinningSamples = 0u;
    u32 boundsSamples = 0u;
    u32 renderFrameSamples = 0u;
    u32 deferredClearSamples = 0u;
    u32 deferredLightingSamples = 0u;
    u32 deferredCompositeSamples = 0u;
    u32 materialUploadSamples = 0u;
    u32 meshDispatchSamples = 0u;
    u32 rasterSamples = 0u;
};

struct BenchmarkTimingSummary{
    f64 cpuFrameSeconds = 0.0;
    f64 skinningSeconds = 0.0;
    f64 boundsSeconds = 0.0;
    f64 renderFrameSeconds = 0.0;
    f64 deferredClearSeconds = 0.0;
    f64 deferredLightingSeconds = 0.0;
    f64 deferredCompositeSeconds = 0.0;
    f64 materialUploadSeconds = 0.0;
    f64 meshDispatchSeconds = 0.0;
    f64 rasterSeconds = 0.0;
    u32 frameCount = 0u;
    u32 skinningSamples = 0u;
    u32 boundsSamples = 0u;
    u32 renderFrameSamples = 0u;
    u32 deferredClearSamples = 0u;
    u32 deferredLightingSamples = 0u;
    u32 deferredCompositeSamples = 0u;
    u32 materialUploadSamples = 0u;
    u32 meshDispatchSamples = 0u;
    u32 rasterSamples = 0u;

    [[nodiscard]] bool hasRenderTiming()const{
        return frameCount != 0u && meshDispatchSamples != 0u;
    }

    [[nodiscard]] bool hasBoundsTiming()const{
        return frameCount != 0u && boundsSamples != 0u;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_CharacterCount = 64u;
static constexpr u32 s_BenchmarkRepeatCount = 4u;
static constexpr u32 s_WarmupFrameCount = 8u;
static constexpr u32 s_SampleFrameCount = 16u;
static constexpr u32 s_FinishDrainFrameCount = 30u;
static constexpr f64 s_CullingRegressionToleranceRatio = 0.02;
static constexpr f64 s_CullingRegressionToleranceMilliseconds = 0.05;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;
static constexpr f32 s_FrontCameraDistance = 3.2f;
static constexpr f32 s_BackCameraDistance = 3.2f;
static constexpr f32 s_CloseCameraDistance = 1.65f;
static constexpr f32 s_FarCameraDistance = 5.6f;
static constexpr f32 s_CameraHeight = 1.1f;
static constexpr u32 s_AnimatedJointModulo = 16u;
static constexpr u32 s_StaticPreviewAnimatedJointModulo = 2u;
static constexpr AStringView s_BenchmarkModelPath = "project/characters/body/model";
static constexpr AStringView s_SkinningBenchmarkMaterialPath = "project/smoke/skinning_culling_benchmark/materials/solid";
static constexpr const char* s_StaticPreviewEnv = "NWB_SKINNING_CULLING_STATIC_PREVIEW";
static constexpr Name s_ModelSkeletonObject("skeleton");

static constexpr BenchmarkCase s_BenchmarkCases[] = {
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
};
static constexpr usize s_BenchmarkCaseCount = sizeof(s_BenchmarkCases) / sizeof(s_BenchmarkCases[0]);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static const char* BenchmarkModeName(const BenchmarkMode::Enum mode){
    switch(mode){
    case BenchmarkMode::NoCulling: return "no_culling";
    case BenchmarkMode::FrustumOnly: return "frustum_only";
    case BenchmarkMode::FrustumCone: return "frustum_cone";
    default: return "unknown";
    }
}

[[nodiscard]] static const char* BenchmarkViewName(const BenchmarkView::Enum view){
    switch(view){
    case BenchmarkView::Front: return "front";
    case BenchmarkView::Back: return "back";
    case BenchmarkView::Close: return "close";
    case BenchmarkView::Far: return "far";
    default: return "unknown";
    }
}

[[nodiscard]] static f64 AverageSeconds(const f64 seconds, const u32 count){
    return count != 0u ? seconds / static_cast<f64>(count) : 0.0;
}

[[nodiscard]] static f64 AverageMilliseconds(const f64 seconds, const u32 count){
    return AverageSeconds(seconds, count) * 1000.0;
}

[[nodiscard]] static f64 RenderGpuMilliseconds(const BenchmarkTimingSummary& summary){
    return AverageMilliseconds(summary.meshDispatchSeconds + summary.rasterSeconds, summary.frameCount);
}

[[nodiscard]] static f64 BoundsGpuMilliseconds(const BenchmarkTimingSummary& summary){
    return AverageMilliseconds(summary.boundsSeconds, summary.frameCount);
}

[[nodiscard]] static f64 CullingGpuMilliseconds(const BenchmarkTimingSummary& summary){
    return BoundsGpuMilliseconds(summary) + RenderGpuMilliseconds(summary);
}

[[nodiscard]] static u32 RandomJointSeed(const u32 jointIndex, const u32 characterIndex){
    u32 seed = jointIndex * 747796405u + characterIndex * 2891336453u + 277803737u;
    seed = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return (seed >> 22u) ^ seed;
}

[[nodiscard]] static bool ShouldAnimateJoint(const u32 jointIndex, const u32 characterIndex, const bool staticPreview){
    const u32 modulo = staticPreview ? s_StaticPreviewAnimatedJointModulo : s_AnimatedJointModulo;
    return jointIndex != 0u && (RandomJointSeed(jointIndex, characterIndex) % modulo) == 0u;
}

[[nodiscard]] static bool EnvironmentFlagEnabled(const char* name){
#if defined(_MSC_VER)
    char* value = nullptr;
    size_t valueSize = 0;
    if(::_dupenv_s(&value, &valueSize, name) != 0 || !value)
        return false;

    const bool enabled = value[0] != '\0' && value[0] != '0';
    ::free(value);
    return enabled;
#else
    const char* value = NWB_GETENV(name);
    return value && value[0] != '\0' && value[0] != '0';
#endif
}

[[nodiscard]] static bool StaticPreviewEnabled(){
    return EnvironmentFlagEnabled(s_StaticPreviewEnv);
}

static void AccumulateGpuStats(const NWB::Core::Perf::TimingStats& stats, f64& seconds, u32& samples){
    if(!stats.valid())
        return;

    seconds += static_cast<f64>(stats.seconds);
    samples += stats.sampleCount;
}

static void AccumulateGpuScope(
    const NWB::Core::Perf::TimingView& gpuTiming,
    const Name& scopeName,
    f64& seconds,
    u32& samples
){
    AccumulateGpuStats(gpuTiming.stats(scopeName), seconds, samples);
}

static void AccumulateBenchmarkSummary(BenchmarkTimingSummary& summary, const BenchmarkAccumulation& accum){
    summary.cpuFrameSeconds += accum.cpuFrameSeconds;
    summary.skinningSeconds += accum.skinningSeconds;
    summary.boundsSeconds += accum.boundsSeconds;
    summary.renderFrameSeconds += accum.renderFrameSeconds;
    summary.deferredClearSeconds += accum.deferredClearSeconds;
    summary.deferredLightingSeconds += accum.deferredLightingSeconds;
    summary.deferredCompositeSeconds += accum.deferredCompositeSeconds;
    summary.materialUploadSeconds += accum.materialUploadSeconds;
    summary.meshDispatchSeconds += accum.meshDispatchSeconds;
    summary.rasterSeconds += accum.rasterSeconds;
    summary.frameCount += accum.frameCount;
    summary.skinningSamples += accum.skinningSamples;
    summary.boundsSamples += accum.boundsSamples;
    summary.renderFrameSamples += accum.renderFrameSamples;
    summary.deferredClearSamples += accum.deferredClearSamples;
    summary.deferredLightingSamples += accum.deferredLightingSamples;
    summary.deferredCompositeSamples += accum.deferredCompositeSamples;
    summary.materialUploadSamples += accum.materialUploadSamples;
    summary.meshDispatchSamples += accum.meshDispatchSamples;
    summary.rasterSamples += accum.rasterSamples;
}

[[nodiscard]] static NWB::Impl::SkeletonJointMatrix BuildAnimatedJointMatrix(
    const NWB::Impl::SkeletonJointMatrix& bindJoint,
    const u32 jointIndex,
    const u32 characterIndex,
    const f32 timeSeconds,
    const bool staticPreview
){
    if(!ShouldAnimateJoint(jointIndex, characterIndex, staticPreview))
        return bindJoint;

    const u32 seed = RandomJointSeed(jointIndex, characterIndex);
    const f32 seedPhase = static_cast<f32>(seed & 255u) * 0.0245436926f;
    const f32 phase = staticPreview ? seedPhase : static_cast<f32>(characterIndex) * 1.04719758f;
    const f32 primarySpeed = staticPreview ? 1.15f : 0.34f;
    const f32 secondarySpeed = staticPreview ? 0.83f : 0.21f;
    const f32 baseAngle = staticPreview ? 0.20f : 0.024f;
    const f32 angleJitter = staticPreview ? 0.025f : 0.0025f;
    const f32 rollBase = staticPreview ? 0.045f : 0.006f;
    const f32 rollJitter = staticPreview ? 0.008f : 0.0015f;
    const SIMDVector waves = VectorSin(VectorSet(
        timeSeconds * primarySpeed + phase,
        timeSeconds * secondarySpeed + phase + 0.5f,
        0.0f,
        0.0f
    ));
    const f32 amount = VectorGetX(waves);
    const f32 secondary = VectorGetY(waves);
    const f32 angle = amount * (baseAngle + static_cast<f32>((seed >> 16u) % 5u) * angleJitter);

    SIMDMatrix matrix = MatrixRotationRollPitchYaw(
        angle * (0.18f + static_cast<f32>((seed >> 3u) & 3u) * 0.035f),
        angle * (0.58f + static_cast<f32>((seed >> 6u) & 3u) * 0.04f),
        secondary * (rollBase + static_cast<f32>((seed >> 9u) & 3u) * rollJitter)
    );
    matrix = NWB::Impl::SkeletonRuntime::MultiplyJointMatrices(LoadFloat(bindJoint), matrix);
    if(!NWB::Impl::SkeletonRuntime::IsInvertibleAffineJointMatrix(matrix))
        return bindJoint;

    NWB::Impl::SkeletonJointMatrix stored{};
    StoreFloat(matrix, &stored);
    return stored;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BenchmarkRuntimeMeshProvider final : public NWB::Impl::IRuntimeMeshProvider{
public:
    BenchmarkRuntimeMeshProvider() = default;
    ~BenchmarkRuntimeMeshProvider(){
        uninstall();
    }


public:
    bool install(NWB::Impl::MeshSystem& meshSystem, NWB::Impl::MeshSkinningSystem& meshSkinningSystem){
        if(m_registered)
            return true;

        m_meshSystem = &meshSystem;
        m_meshSkinningSystem = &meshSkinningSystem;
        m_meshSystem->unregisterRuntimeMeshProvider(*m_meshSkinningSystem);
        m_meshSystem->registerRuntimeMeshProvider(*this);
        m_registered = true;
        return true;
    }

    void uninstall(){
        if(!m_registered || !m_meshSystem || !m_meshSkinningSystem)
            return;

        m_meshSystem->unregisterRuntimeMeshProvider(*this);
        m_meshSystem->registerRuntimeMeshProvider(*m_meshSkinningSystem);
        m_meshSystem = nullptr;
        m_meshSkinningSystem = nullptr;
        m_registered = false;
    }

    void setMode(const BenchmarkMode::Enum mode){
        m_mode = mode;
    }

    virtual bool resolveRuntimeMesh(const NWB::Core::ECS::EntityID entity, NWB::Impl::RuntimeMeshDesc& outMesh)override{
        if(!m_meshSkinningSystem || !m_meshSkinningSystem->resolveRuntimeMesh(entity, outMesh))
            return false;

        switch(m_mode){
        case BenchmarkMode::NoCulling:
            outMesh.dynamicMeshletBoundsFresh = false;
            outMesh.dynamicMeshletConesFresh = false;
            return true;
        case BenchmarkMode::FrustumOnly:
            outMesh.dynamicMeshletBoundsFresh = true;
            outMesh.dynamicMeshletConesFresh = false;
            return true;
        case BenchmarkMode::FrustumCone:
        default:
            outMesh.dynamicMeshletBoundsFresh = true;
            outMesh.dynamicMeshletConesFresh = true;
            return true;
        }
    }

    virtual bool containsRuntimeMesh(const Name& meshKey, const u64 version)override{
        return m_meshSkinningSystem && m_meshSkinningSystem->containsRuntimeMesh(meshKey, version);
    }


private:
    NWB::Impl::MeshSystem* m_meshSystem = nullptr;
    NWB::Impl::MeshSkinningSystem* m_meshSkinningSystem = nullptr;
    BenchmarkMode::Enum m_mode = BenchmarkMode::FrustumCone;
    bool m_registered = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinningCullingBenchmarkProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("SkinningCullingBenchmark: ECS world allocation failed"));
            throw RuntimeException("SkinningCullingBenchmark initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("SkinningCullingBenchmark: shader path resolver callback is null"));
            throw RuntimeException("SkinningCullingBenchmark initialization failed");
        }

        auto& meshSystem = world->addSystem<NWB::Impl::MeshSystem>(*world);
        auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(
            *world,
            context.graphics,
            context.assetManager,
            context.shaderPathResolver
        );
        auto& modelSystem = world->addSystem<NWB::Impl::ModelSystem>(
            *world,
            context.assetManager,
            NWB::Impl::CreateModelObjectRendererHooks()
        );
        static_cast<void>(modelSystem);
        auto& meshSkinningSystem = world->addSystem<NWB::Impl::MeshSkinningSystem>(
            *world,
            context.graphics,
            context.assetManager,
            meshSystem,
            context.shaderPathResolver
        );
        context.setGpuTimingEnabled(true);

        context.graphics.addRenderPassToBack(meshSkinningSystem);
        context.graphics.addRenderPassToBack(rendererSystem);
        return MakeNotNullUnique(Move(world));
    }

    void destroyWorld(){
        if(!m_world.owner())
            return;

        m_runtimeMeshProvider.uninstall();

        auto* meshSkinningSystem = m_world->getSystem<NWB::Impl::MeshSkinningSystem>();
        if(meshSkinningSystem)
            m_context.graphics.removeRenderPass(*meshSkinningSystem);

        auto* rendererSystem = m_world->getSystem<NWB::Impl::RendererSystem>();
        if(rendererSystem)
            m_context.graphics.removeRenderPass(*rendererSystem);

        m_context.graphics.waitAllJobs();
        if(auto* device = m_context.graphics.getDevice())
            device->waitForIdle();

        m_world->clear();
        m_world.owner().reset();
    }

    void requestQuit(){
        if(m_quitRequested)
            return;

        m_quitRequested = true;
        if(m_context.requestQuit)
            m_context.requestQuit();
    }

    [[nodiscard]] bool loadSkeletonBindJoints(){
        UniquePtr<NWB::Core::Assets::IAsset> loadedModelAsset;
        if(!m_context.assetManager.loadSync(NWB::Impl::Model::AssetTypeName(), Name(s_BenchmarkModelPath), loadedModelAsset)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: failed to load benchmark model"));
            return false;
        }
        if(!loadedModelAsset || loadedModelAsset->assetType() != NWB::Impl::Model::AssetTypeName()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: benchmark model loaded with an unexpected asset type"));
            return false;
        }

        const auto* model = static_cast<const NWB::Impl::Model*>(loadedModelAsset.get());
        const NWB::Impl::ModelSkeletonObject* skeletonObject = nullptr;
        for(const NWB::Impl::ModelSkeletonObject& object : model->skeletonObjects()){
            if(object.name == s_ModelSkeletonObject){
                skeletonObject = &object;
                break;
            }
        }
        if(!skeletonObject && !model->skeletonObjects().empty())
            skeletonObject = &model->skeletonObjects().front();
        if(!skeletonObject || !skeletonObject->skeleton.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: benchmark model has no skeleton object"));
            return false;
        }

        UniquePtr<NWB::Core::Assets::IAsset> loadedSkeletonAsset;
        const Name skeletonPath = skeletonObject->skeleton.name();
        if(!m_context.assetManager.loadSync(NWB::Impl::Skeleton::AssetTypeName(), skeletonPath, loadedSkeletonAsset)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: failed to load benchmark skeleton"));
            return false;
        }
        if(!loadedSkeletonAsset || loadedSkeletonAsset->assetType() != NWB::Impl::Skeleton::AssetTypeName()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: benchmark skeleton loaded with an unexpected asset type"));
            return false;
        }

        const auto* skeleton = static_cast<const NWB::Impl::Skeleton*>(loadedSkeletonAsset.get());
        if(skeleton->joints().empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: benchmark skeleton has no joints"));
            return false;
        }
        m_bindJoints.clear();
        m_bindJoints.reserve(skeleton->joints().size());
        for(const NWB::Impl::SkeletonJoint& joint : skeleton->joints()){
            if(!NWB::Impl::SkeletonRuntime::IsInvertibleAffineJointMatrix(LoadFloat(joint.localBindPose))){
                NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: benchmark skeleton contains an invalid bind pose"));
                return false;
            }

            m_bindJoints.push_back(joint.localBindPose);
        }
        return !m_bindJoints.empty();
    }

    void configureCamera(const BenchmarkView::Enum view){
        auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(m_cameraEntity);
        if(!transform)
            return;

        switch(view){
        case BenchmarkView::Back:
            transform->position = Float4(0.0f, s_CameraHeight, s_BackCameraDistance, 0.0f);
            StoreFloat(QuaternionRotationRollPitchYaw(0.0f, s_PI, 0.0f), &transform->rotation);
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

    void configureCase(){
        const BenchmarkCase& benchmarkCase = s_BenchmarkCases[m_caseIndex];
        m_runtimeMeshProvider.setMode(benchmarkCase.mode);
        configureCamera(benchmarkCase.view);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: begin repeat={}/{} mode={} view={}")
            , m_repeatIndex + 1u
            , s_BenchmarkRepeatCount
            , StringConvert(BenchmarkModeName(benchmarkCase.mode))
            , StringConvert(BenchmarkViewName(benchmarkCase.view))
        );
    }

    void finishCase(){
        const BenchmarkCase& benchmarkCase = s_BenchmarkCases[m_caseIndex];
        const BenchmarkAccumulation& accum = m_accumulations[m_caseIndex];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: result repeat={}/{} mode={} view={} frames={} cpu_ms={} skinning_gpu_ms={} bounds_gpu_ms={} render_frame_gpu_ms={} deferred_clear_gpu_ms={} deferred_lighting_gpu_ms={} deferred_composite_gpu_ms={} material_upload_gpu_ms={} mesh_dispatch_gpu_ms={} raster_gpu_ms={} skinning_samples={} bounds_samples={} render_frame_samples={} deferred_clear_samples={} deferred_lighting_samples={} deferred_composite_samples={} material_upload_samples={} mesh_samples={} raster_samples={}")
            , m_repeatIndex + 1u
            , s_BenchmarkRepeatCount
            , StringConvert(BenchmarkModeName(benchmarkCase.mode))
            , StringConvert(BenchmarkViewName(benchmarkCase.view))
            , accum.frameCount
            , AverageSeconds(accum.cpuFrameSeconds, accum.frameCount) * 1000.0
            , AverageSeconds(accum.skinningSeconds, accum.skinningSamples) * 1000.0
            , AverageSeconds(accum.boundsSeconds, accum.boundsSamples) * 1000.0
            , AverageSeconds(accum.renderFrameSeconds, accum.renderFrameSamples) * 1000.0
            , AverageSeconds(accum.deferredClearSeconds, accum.deferredClearSamples) * 1000.0
            , AverageSeconds(accum.deferredLightingSeconds, accum.deferredLightingSamples) * 1000.0
            , AverageSeconds(accum.deferredCompositeSeconds, accum.deferredCompositeSamples) * 1000.0
            , AverageSeconds(accum.materialUploadSeconds, accum.materialUploadSamples) * 1000.0
            , AverageSeconds(accum.meshDispatchSeconds, accum.meshDispatchSamples) * 1000.0
            , AverageSeconds(accum.rasterSeconds, accum.rasterSamples) * 1000.0
            , accum.skinningSamples
            , accum.boundsSamples
            , accum.renderFrameSamples
            , accum.deferredClearSamples
            , accum.deferredLightingSamples
            , accum.deferredCompositeSamples
            , accum.materialUploadSamples
            , accum.meshDispatchSamples
            , accum.rasterSamples
        );
    }

    void finishBenchmark(){
        BenchmarkTimingSummary noCullingSummary;
        BenchmarkTimingSummary frustumOnlySummary;
        BenchmarkTimingSummary frustumConeSummary;

        for(usize i = 0u; i < s_BenchmarkCaseCount; ++i){
            const BenchmarkCase& benchmarkCase = s_BenchmarkCases[i];
            const BenchmarkAccumulation& accum = m_accumulations[i];
            if(benchmarkCase.mode == BenchmarkMode::NoCulling){
                AccumulateBenchmarkSummary(noCullingSummary, accum);
            }
            else if(benchmarkCase.mode == BenchmarkMode::FrustumOnly){
                AccumulateBenchmarkSummary(frustumOnlySummary, accum);
            }
            else if(benchmarkCase.mode == BenchmarkMode::FrustumCone){
                AccumulateBenchmarkSummary(frustumConeSummary, accum);
            }
        }

        const f64 noCullingAverage = RenderGpuMilliseconds(noCullingSummary);
        const f64 frustumOnlyAverage = CullingGpuMilliseconds(frustumOnlySummary);
        const f64 frustumConeAverage = CullingGpuMilliseconds(frustumConeSummary);
        const f64 frustumConeRenderAverage = RenderGpuMilliseconds(frustumConeSummary);
        const bool hasComparisonTiming =
            noCullingSummary.hasRenderTiming()
            && frustumConeSummary.hasRenderTiming()
            && frustumConeSummary.hasBoundsTiming()
            && noCullingAverage > 0.0
            && frustumConeRenderAverage > 0.0
        ;
        const bool cullingFaster = hasComparisonTiming && frustumConeRenderAverage < noCullingAverage;
        const f64 cullingRegressionTolerance = Max(
            s_CullingRegressionToleranceMilliseconds,
            noCullingAverage * s_CullingRegressionToleranceRatio
        );
        const bool cullingAcceptable =
            cullingFaster
            || (hasComparisonTiming && frustumConeRenderAverage <= noCullingAverage + cullingRegressionTolerance)
        ;
        const f64 speedup = cullingFaster ? noCullingAverage / frustumConeRenderAverage : 0.0;

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: culling comparison repeats={} no_culling_render_frame_gpu_ms={} with_culling_render_frame_gpu_ms={} with_culling_total_frame_gpu_ms={} with_culling_bounds_frame_gpu_ms={} tolerance_ms={} speedup={} result={}")
            , s_BenchmarkRepeatCount
            , noCullingAverage
            , frustumConeRenderAverage
            , frustumConeAverage
            , BoundsGpuMilliseconds(frustumConeSummary)
            , cullingRegressionTolerance
            , speedup
            , StringConvert(cullingAcceptable ? (cullingFaster ? "pass" : "pass_tolerance") : "fail")
        );
        if(cullingAcceptable){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: culling benchmark passed"));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: culling benchmark failed; with-culling render timing exceeded no-culling render timing beyond tolerance"));
        }

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: policy default=dynamic_frustum_cone measured_frustum_only_gpu_ms={} measured_frustum_cone_gpu_ms={} recommendation={}")
            , frustumOnlyAverage
            , frustumConeAverage
            , StringConvert((frustumConeAverage > 0.0 && (frustumOnlyAverage == 0.0 || frustumConeAverage <= frustumOnlyAverage)) ? "dynamic_frustum_cone" : "dynamic_frustum")
        );
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: completed"));
    }

    void collectPreviousFrame(const f32 delta){
        if(m_caseRenderedFrames == 0u)
            return;

        const u32 previousFrame = m_caseRenderedFrames - 1u;
        if(previousFrame < s_WarmupFrameCount)
            return;

        BenchmarkAccumulation& accum = m_accumulations[m_caseIndex];
        accum.cpuFrameSeconds += IsFinite(delta) && delta > 0.0f ? static_cast<f64>(delta) : 0.0;
        ++accum.frameCount;

        static_cast<void>(m_context.flushPerfSamples());
        const NWB::Core::Perf::SessionReport perfReport = m_context.perfReport();
        const NWB::Core::Perf::TimingView& gpuTiming = perfReport.gpuTiming;
        AccumulateGpuScope(gpuTiming, SkinnedTiming::s_Skinning, accum.skinningSeconds, accum.skinningSamples);
        AccumulateGpuScope(gpuTiming, SkinnedTiming::s_MeshletBounds, accum.boundsSeconds, accum.boundsSamples);
        AccumulateGpuScope(gpuTiming, RendererTiming::s_Frame, accum.renderFrameSeconds, accum.renderFrameSamples);
        AccumulateGpuScope(gpuTiming, RendererTiming::s_DeferredClear, accum.deferredClearSeconds, accum.deferredClearSamples);
        AccumulateGpuScope(gpuTiming, RendererTiming::s_DeferredLighting, accum.deferredLightingSeconds, accum.deferredLightingSamples);
        AccumulateGpuScope(gpuTiming, RendererTiming::s_DeferredComposite, accum.deferredCompositeSeconds, accum.deferredCompositeSamples);
        AccumulateGpuScope(gpuTiming, RendererTiming::s_MaterialUpload, accum.materialUploadSeconds, accum.materialUploadSamples);
        AccumulateGpuScope(gpuTiming, RendererTiming::s_MeshDispatch, accum.meshDispatchSeconds, accum.meshDispatchSamples);
        AccumulateGpuScope(gpuTiming, RendererTiming::s_Raster, accum.rasterSeconds, accum.rasterSamples);
    }

    void advanceCaseOrFinish(){
        if(m_caseRenderedFrames < s_WarmupFrameCount + s_SampleFrameCount)
            return;

        finishCase();
        ++m_caseIndex;
        m_caseRenderedFrames = 0u;

        if(m_caseIndex >= s_BenchmarkCaseCount){
            ++m_repeatIndex;
            if(m_repeatIndex >= s_BenchmarkRepeatCount){
                finishBenchmark();
                m_finished = true;
                m_finishDrainFrames = s_FinishDrainFrameCount;
                return;
            }

            m_caseIndex = 0u;
        }

        configureCase();
    }

    void animatePoses(){
        const f32 timeSeconds = static_cast<f32>(m_totalTimeSeconds);
        for(usize entityIndex = 0u; entityIndex < m_entities.size(); ++entityIndex){
            auto* pose = m_world->tryGetComponent<NWB::Impl::SkeletonPoseComponent>(m_entities[entityIndex]);
            if(!pose)
                continue;

            for(u32 jointIndex = 0u; jointIndex < pose->localJoints.size(); ++jointIndex)
                pose->localJoints[jointIndex] = BuildAnimatedJointMatrix(
                    m_bindJoints[jointIndex],
                    jointIndex,
                    static_cast<u32>(entityIndex),
                    timeSeconds,
                    m_staticPreview
                );
        }
    }

    [[nodiscard]] NWB::Core::ECS::EntityID findSpawnedModelObject(
        const NWB::Core::ECS::EntityID owner,
        const Name objectName,
        const NWB::Impl::ModelObjectKind::Enum objectKind
    )const{
        NWB::Core::ECS::EntityID result = NWB::Core::ECS::ENTITY_ID_INVALID;
        m_world->view<NWB::Impl::ModelObjectComponent>().each(
            [&](const NWB::Core::ECS::EntityID entity, NWB::Impl::ModelObjectComponent& object){
                if(result.valid())
                    return;
                if(object.owner == owner && object.object == objectName && object.kind == objectKind)
                    result = entity;
            }
        );
        return result;
    }


public:
    explicit SkinningCullingBenchmarkProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
        , m_entities(context.objectArena)
        , m_bindJoints(context.objectArena)
        , m_staticPreview(StaticPreviewEnabled())
    {}

    virtual ~SkinningCullingBenchmarkProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        if(!loadSkeletonBindJoints()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: benchmark mesh has no skeleton joints"));
            requestQuit();
            return true;
        }
        auto* meshSystem = m_world->getSystem<NWB::Impl::MeshSystem>();
        auto* meshSkinningSystem = m_world->getSystem<NWB::Impl::MeshSkinningSystem>();
        if(!meshSystem || !meshSkinningSystem || !m_runtimeMeshProvider.install(*meshSystem, *meshSkinningSystem)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: failed to install benchmark runtime mesh provider"));
            requestQuit();
            return true;
        }

        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(*m_world, Float4(0.0f, s_CameraHeight, -s_FrontCameraDistance, 0.0f));
        m_cameraEntity = activeCamera.camera;
        NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

        BenchmarkModelRef model;
        model.virtualPath = Name(s_BenchmarkModelPath);
        BenchmarkMaterialRef material;
        material.virtualPath = Name(s_SkinningBenchmarkMaterialPath);
        const u32 characterCount = m_staticPreview ? 1u : s_CharacterCount;
        m_entities.reserve(characterCount);
        Vector<NWB::Core::ECS::EntityID, NWB::Core::Alloc::GlobalArena> modelOwners(m_context.objectArena);
        modelOwners.reserve(characterCount);

        for(u32 characterIndex = 0u; characterIndex < characterCount; ++characterIndex){
            auto entity = m_world->createEntity();
            auto& transform = entity.addComponent<NWB::Impl::Scene::TransformComponent>();
            transform.position = Float4(m_staticPreview ? 0.0f : (static_cast<f32>(characterIndex) - 1.0f) * 1.15f, 0.0f, 0.0f, 0.0f);
            transform.scale = Float4(1.0f, 1.0f, 1.0f, 0.0f);

            auto& modelComponent = entity.addComponent<NWB::Impl::ModelComponent>();
            modelComponent.model = model;

            auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
            renderer.material = material;
            modelOwners.push_back(entity.id());
        }

        m_world->tick(0.0f);
        for(const NWB::Core::ECS::EntityID owner : modelOwners){
            const NWB::Core::ECS::EntityID skeletonEntity = findSpawnedModelObject(
                owner,
                s_ModelSkeletonObject,
                NWB::Impl::ModelObjectKind::Skeleton
            );
            if(!skeletonEntity.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("SkinningCullingBenchmark: failed to find spawned benchmark skeleton object"));
                requestQuit();
                return true;
            }

            m_entities.push_back(skeletonEntity);
        }

        if(m_staticPreview){
            m_runtimeMeshProvider.setMode(BenchmarkMode::NoCulling);
            configureCamera(BenchmarkView::Front);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: static preview enabled; close the window manually when done"));
        }
        else{
            configureCase();
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: spawned {} characters with {} joints each"), characterCount, static_cast<u32>(m_bindJoints.size()));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        if(m_finished){
            if(m_finishDrainFrames > 0u){
                --m_finishDrainFrames;
                return true;
            }

            requestQuit();
            return true;
        }

        const f32 safeDelta = IsFinite(delta) && delta > 0.0f ? delta : 1.0f / 60.0f;
        if(m_staticPreview){
            m_totalTimeSeconds += static_cast<f64>(safeDelta);
            animatePoses();
            m_world->tick(safeDelta);
            ++m_caseRenderedFrames;
            return true;
        }

        collectPreviousFrame(delta);
        advanceCaseOrFinish();
        if(m_finished)
            return true;

        m_totalTimeSeconds += static_cast<f64>(safeDelta);
        animatePoses();
        m_world->tick(safeDelta);
        ++m_caseRenderedFrames;
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    BenchmarkRuntimeMeshProvider m_runtimeMeshProvider;
    Vector<NWB::Core::ECS::EntityID, NWB::Core::Alloc::GlobalArena> m_entities;
    Vector<NWB::Impl::SkeletonJointMatrix, NWB::Core::Alloc::GlobalArena> m_bindJoints;
    NWB::Core::ECS::EntityID m_cameraEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    BenchmarkAccumulation m_accumulations[s_BenchmarkCaseCount] = {};
    f64 m_totalTimeSeconds = 0.0;
    u32 m_caseRenderedFrames = 0u;
    u32 m_finishDrainFrames = 0u;
    u32 m_repeatIndex = 0u;
    usize m_caseIndex = 0u;
    bool m_finished = false;
    bool m_quitRequested = false;
    bool m_staticPreview = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


const tchar* NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB Skinning Culling Benchmark");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_skinning_culling_benchmark::SkinningCullingBenchmarkProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

