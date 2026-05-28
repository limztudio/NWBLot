// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/skinned_asset.h>
#include <core/scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/timing_names.h>
#include <impl/ecs_skinned_mesh/runtime_helpers.h>
#include <impl/ecs_skinned_mesh_render/module.h>
#include <impl/ecs_skinned_mesh_render/timing_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_cone_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using BenchmarkMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::SkinnedMesh>;
using BenchmarkMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;

namespace RendererTiming = NWB::Impl::RendererGpuTimingScope;
namespace SkinnedTiming = NWB::Impl::SkinnedMeshGpuTimingScope;

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
    f64 meshDispatchSeconds = 0.0;
    f64 rasterSeconds = 0.0;
    u32 frameCount = 0u;
    u32 skinningSamples = 0u;
    u32 boundsSamples = 0u;
    u32 meshDispatchSamples = 0u;
    u32 rasterSamples = 0u;
};

struct BenchmarkTimingSummary{
    f64 cpuFrameSeconds = 0.0;
    f64 skinningSeconds = 0.0;
    f64 boundsSeconds = 0.0;
    f64 meshDispatchSeconds = 0.0;
    f64 rasterSeconds = 0.0;
    u32 frameCount = 0u;
    u32 skinningSamples = 0u;
    u32 boundsSamples = 0u;
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
static constexpr AStringView s_BenchmarkSkinnedMeshPath = "project/characters/skinned_cone_female";
static constexpr AStringView s_SkinnedBenchmarkMaterialPath = "project/materials/mat_skinned_solid";
static constexpr const char* s_StaticPreviewEnv = "NWB_SKINNED_CONE_STATIC_PREVIEW";

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

[[nodiscard]] static bool StaticPreviewEnabled(){
    const char* value = NWB_GETENV(s_StaticPreviewEnv);
    return value && value[0] != '\0' && value[0] != '0';
}

static void AccumulateGpuStats(const NWB::Core::GpuTimingStats& stats, f64& seconds, u32& samples){
    if(!stats.valid())
        return;

    seconds += static_cast<f64>(stats.seconds);
    samples += stats.sampleCount;
}

static void AccumulateGpuScope(
    const NWB::Core::GpuTimingRecorder& gpuTiming,
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
    summary.meshDispatchSeconds += accum.meshDispatchSeconds;
    summary.rasterSeconds += accum.rasterSeconds;
    summary.frameCount += accum.frameCount;
    summary.skinningSamples += accum.skinningSamples;
    summary.boundsSamples += accum.boundsSamples;
    summary.meshDispatchSamples += accum.meshDispatchSamples;
    summary.rasterSamples += accum.rasterSamples;
}

[[nodiscard]] static NWB::Impl::SkinnedMeshJointMatrix BuildAnimatedJointMatrix(
    const NWB::Impl::SkinnedMeshJointMatrix& bindJoint,
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
    matrix = NWB::Impl::SkinnedMeshRuntime::MultiplyJointMatrices(LoadFloat(bindJoint), matrix);
    if(!NWB::Impl::SkinnedMeshRuntime::IsInvertibleAffineJointMatrix(matrix))
        return bindJoint;

    NWB::Impl::SkinnedMeshJointMatrix stored{};
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
    bool install(NWB::Impl::MeshSystem& meshSystem, NWB::Impl::SkinnedMeshSystem& skinnedMeshSystem){
        if(m_registered)
            return true;

        m_meshSystem = &meshSystem;
        m_skinnedMeshSystem = &skinnedMeshSystem;
        m_meshSystem->unregisterRuntimeMeshProvider(*m_skinnedMeshSystem);
        m_meshSystem->registerRuntimeMeshProvider(*this);
        m_registered = true;
        return true;
    }

    void uninstall(){
        if(!m_registered || !m_meshSystem || !m_skinnedMeshSystem)
            return;

        m_meshSystem->unregisterRuntimeMeshProvider(*this);
        m_meshSystem->registerRuntimeMeshProvider(*m_skinnedMeshSystem);
        m_meshSystem = nullptr;
        m_skinnedMeshSystem = nullptr;
        m_registered = false;
    }

    void setMode(const BenchmarkMode::Enum mode){
        m_mode = mode;
    }

    virtual bool resolveRuntimeMesh(const NWB::Core::ECS::EntityID entity, NWB::Impl::RuntimeMeshDesc& outMesh)override{
        if(!m_skinnedMeshSystem || !m_skinnedMeshSystem->resolveRuntimeMesh(entity, outMesh))
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
        return m_skinnedMeshSystem && m_skinnedMeshSystem->containsRuntimeMesh(meshKey, version);
    }


private:
    NWB::Impl::MeshSystem* m_meshSystem = nullptr;
    NWB::Impl::SkinnedMeshSystem* m_skinnedMeshSystem = nullptr;
    BenchmarkMode::Enum m_mode = BenchmarkMode::FrustumCone;
    bool m_registered = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedConeBenchmarkProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("SkinnedConeBenchmark: ECS world allocation failed"));
            throw RuntimeException("SkinnedConeBenchmark initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("SkinnedConeBenchmark: shader path resolver callback is null"));
            throw RuntimeException("SkinnedConeBenchmark initialization failed");
        }

        auto& meshSystem = world->addSystem<NWB::Impl::MeshSystem>(*world);
        auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(
            *world,
            context.graphics,
            context.assetManager,
            context.shaderPathResolver
        );
        auto& skinnedMeshSystem = world->addSystem<NWB::Impl::SkinnedMeshSystem>(
            *world,
            context.graphics,
            context.assetManager,
            meshSystem,
            context.shaderPathResolver
        );
        context.graphics.gpuTiming().setEnabled(true);

        context.graphics.addRenderPassToBack(skinnedMeshSystem);
        context.graphics.addRenderPassToBack(rendererSystem);
        return MakeNotNullUnique(Move(world));
    }

    void destroyWorld(){
        if(!m_world.owner())
            return;

        m_runtimeMeshProvider.uninstall();

        auto* skinnedMeshSystem = m_world->getSystem<NWB::Impl::SkinnedMeshSystem>();
        if(skinnedMeshSystem)
            m_context.graphics.removeRenderPass(*skinnedMeshSystem);

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
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(!m_context.assetManager.loadSync(NWB::Impl::SkinnedMesh::AssetTypeName(), Name(s_BenchmarkSkinnedMeshPath), loadedAsset)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: failed to load benchmark skinned mesh"));
            return false;
        }
        if(!loadedAsset || loadedAsset->assetType() != NWB::Impl::SkinnedMesh::AssetTypeName()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: benchmark mesh loaded with an unexpected asset type"));
            return false;
        }

        const auto* mesh = static_cast<const NWB::Impl::SkinnedMesh*>(loadedAsset.get());
        if(mesh->skeletonJointCount() == 0u || mesh->inverseBindMatrices().size() != mesh->skeletonJointCount()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: benchmark mesh has invalid skeleton bind data"));
            return false;
        }

        m_bindJoints.clear();
        m_bindJoints.reserve(mesh->inverseBindMatrices().size());
        for(const NWB::Impl::SkinnedMeshJointMatrix& inverseBind : mesh->inverseBindMatrices()){
            SIMDVector determinant = VectorZero();
            const SIMDMatrix bindJoint = MatrixInverse(&determinant, LoadFloat(inverseBind));
            if(!NWB::Impl::SkinnedMeshRuntime::IsInvertibleAffineJointMatrix(bindJoint)){
                NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: benchmark mesh inverse bind matrix produced an invalid bind pose"));
                return false;
            }

            NWB::Impl::SkinnedMeshJointMatrix storedBind{};
            StoreFloat(bindJoint, &storedBind);
            m_bindJoints.push_back(storedBind);
        }
        return !m_bindJoints.empty();
    }

    void initializePose(NWB::Impl::SkinnedMeshSkeletonPoseComponent& pose)const{
        pose.parentJoints.resize(m_bindJoints.size(), NWB::Impl::s_SkinnedMeshSkeletonRootParent);
        pose.localJoints.clear();
        pose.localJoints.reserve(m_bindJoints.size());
        for(usize jointIndex = 0u; jointIndex < m_bindJoints.size(); ++jointIndex)
            pose.localJoints.push_back(m_bindJoints[jointIndex]);
    }

    void configureCamera(const BenchmarkView::Enum view){
        auto* transform = m_world->tryGetComponent<NWB::Core::Scene::TransformComponent>(m_cameraEntity);
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
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: begin repeat={}/{} mode={} view={}")
            , m_repeatIndex + 1u
            , s_BenchmarkRepeatCount
            , StringConvert(BenchmarkModeName(benchmarkCase.mode))
            , StringConvert(BenchmarkViewName(benchmarkCase.view))
        );
    }

    void finishCase(){
        const BenchmarkCase& benchmarkCase = s_BenchmarkCases[m_caseIndex];
        const BenchmarkAccumulation& accum = m_accumulations[m_caseIndex];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: result repeat={}/{} mode={} view={} frames={} cpu_ms={} skinning_gpu_ms={} bounds_gpu_ms={} mesh_dispatch_gpu_ms={} raster_gpu_ms={} skinning_samples={} bounds_samples={} mesh_samples={} raster_samples={}")
            , m_repeatIndex + 1u
            , s_BenchmarkRepeatCount
            , StringConvert(BenchmarkModeName(benchmarkCase.mode))
            , StringConvert(BenchmarkViewName(benchmarkCase.view))
            , accum.frameCount
            , AverageSeconds(accum.cpuFrameSeconds, accum.frameCount) * 1000.0
            , AverageSeconds(accum.skinningSeconds, accum.skinningSamples) * 1000.0
            , AverageSeconds(accum.boundsSeconds, accum.boundsSamples) * 1000.0
            , AverageSeconds(accum.meshDispatchSeconds, accum.meshDispatchSamples) * 1000.0
            , AverageSeconds(accum.rasterSeconds, accum.rasterSamples) * 1000.0
            , accum.skinningSamples
            , accum.boundsSamples
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
        const f64 speedup = cullingFaster ? noCullingAverage / frustumConeRenderAverage : 0.0;

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: culling comparison repeats={} no_culling_render_frame_gpu_ms={} with_culling_render_frame_gpu_ms={} with_culling_total_frame_gpu_ms={} with_culling_bounds_frame_gpu_ms={} speedup={} result={}")
            , s_BenchmarkRepeatCount
            , noCullingAverage
            , frustumConeRenderAverage
            , frustumConeAverage
            , BoundsGpuMilliseconds(frustumConeSummary)
            , speedup
            , StringConvert(cullingFaster ? "pass" : "fail")
        );
        if(cullingFaster){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: culling benchmark passed"));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: culling benchmark failed; with-culling render timing must be lower than no-culling render timing"));
        }

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: policy default=dynamic_frustum_cone measured_frustum_only_gpu_ms={} measured_frustum_cone_gpu_ms={} recommendation={}")
            , frustumOnlyAverage
            , frustumConeAverage
            , StringConvert((frustumConeAverage > 0.0 && (frustumOnlyAverage == 0.0 || frustumConeAverage <= frustumOnlyAverage)) ? "dynamic_frustum_cone" : "dynamic_frustum")
        );
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: completed"));
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

        if(auto* device = m_context.graphics.getDevice()){
            device->waitForIdle();
            m_context.graphics.gpuTiming().collect(*device);
        }
        const NWB::Core::GpuTimingRecorder& gpuTiming = m_context.graphics.gpuTiming();
        AccumulateGpuScope(gpuTiming, SkinnedTiming::Skinning(), accum.skinningSeconds, accum.skinningSamples);
        AccumulateGpuScope(gpuTiming, SkinnedTiming::MeshletBounds(), accum.boundsSeconds, accum.boundsSamples);
        AccumulateGpuScope(gpuTiming, RendererTiming::MeshDispatch(), accum.meshDispatchSeconds, accum.meshDispatchSamples);
        AccumulateGpuScope(gpuTiming, RendererTiming::Raster(), accum.rasterSeconds, accum.rasterSamples);
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
            auto* pose = m_world->tryGetComponent<NWB::Impl::SkinnedMeshSkeletonPoseComponent>(m_entities[entityIndex]);
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


public:
    explicit SkinnedConeBenchmarkProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
        , m_entities(context.objectArena)
        , m_bindJoints(context.objectArena)
        , m_staticPreview(StaticPreviewEnabled())
    {}

    virtual ~SkinnedConeBenchmarkProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        if(!loadSkeletonBindJoints()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: benchmark mesh has no skeleton joints"));
            requestQuit();
            return true;
        }
        auto* meshSystem = m_world->getSystem<NWB::Impl::MeshSystem>();
        auto* skinnedMeshSystem = m_world->getSystem<NWB::Impl::SkinnedMeshSystem>();
        if(!meshSystem || !skinnedMeshSystem || !m_runtimeMeshProvider.install(*meshSystem, *skinnedMeshSystem)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: failed to install benchmark runtime mesh provider"));
            requestQuit();
            return true;
        }

        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Core::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Core::Scene::CreateSceneCameraEntity(*m_world, Float4(0.0f, s_CameraHeight, -s_FrontCameraDistance, 0.0f));
        m_cameraEntity = activeCamera.camera;
        NWB::Core::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

        BenchmarkMeshRef mesh;
        mesh.virtualPath = Name(s_BenchmarkSkinnedMeshPath);
        BenchmarkMaterialRef material;
        material.virtualPath = Name(s_SkinnedBenchmarkMaterialPath);
        const u32 characterCount = m_staticPreview ? 1u : s_CharacterCount;
        m_entities.reserve(characterCount);

        for(u32 characterIndex = 0u; characterIndex < characterCount; ++characterIndex){
            auto entity = m_world->createEntity();
            auto& transform = entity.addComponent<NWB::Core::Scene::TransformComponent>();
            transform.position = Float4(m_staticPreview ? 0.0f : (static_cast<f32>(characterIndex) - 1.0f) * 1.15f, 0.0f, 0.0f, 0.0f);
            transform.scale = Float4(1.0f, 1.0f, 1.0f, 0.0f);

            auto& skinnedMesh = entity.addComponent<NWB::Impl::SkinnedMeshComponent>();
            skinnedMesh.skinnedMesh = mesh;

            auto& pose = entity.addComponent<NWB::Impl::SkinnedMeshSkeletonPoseComponent>(m_context.objectArena);
            initializePose(pose);

            auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
            renderer.material = material;
            m_entities.push_back(entity.id());
        }

        if(m_staticPreview){
            m_runtimeMeshProvider.setMode(BenchmarkMode::NoCulling);
            configureCamera(BenchmarkView::Front);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: static preview enabled; close the window manually when done"));
        }
        else{
            configureCase();
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: spawned {} characters with {} joints each"), characterCount, static_cast<u32>(m_bindJoints.size()));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: shutdown"));
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
    Vector<NWB::Impl::SkinnedMeshJointMatrix, NWB::Core::Alloc::GlobalArena> m_bindJoints;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_skinned_cone_benchmark::SkinnedConeBenchmarkProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
