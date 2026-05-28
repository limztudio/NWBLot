// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/skinned_asset.h>
#include <impl/ecs_camera/system.h>
#include <impl/ecs_lighting/system.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/timing_names.h>
#include <impl/ecs_scene/system.h>
#include <impl/ecs_skinned_mesh_render/module.h>
#include <impl/ecs_skinned_mesh_render/timing_names.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_CharacterCount = 3u;
static constexpr u32 s_WarmupFrameCount = 8u;
static constexpr u32 s_SampleFrameCount = 16u;
static constexpr u32 s_FinishDrainFrameCount = 3u;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;
static constexpr AStringView s_MedicalWorkerMeshPath = "project/characters/medical_worker";
static constexpr AStringView s_SkinnedBenchmarkMaterialPath = "project/materials/mat_skinned_uv";

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

static void RequestQuit(){
#if defined(NWB_PLATFORM_WINDOWS)
    PostQuitMessage(0);
#endif
}

[[nodiscard]] static NWB::Impl::SkinnedMeshJointMatrix BuildAnimatedJointMatrix(
    const u32 jointIndex,
    const u32 characterIndex,
    const f32 timeSeconds
){
    const f32 phase = static_cast<f32>((jointIndex * 37u + characterIndex * 53u) & 255u) * 0.0245436926f;
    const f32 frequency = 0.7f + static_cast<f32>((jointIndex * 17u + 11u) % 29u) * 0.031f;
    const f32 amount = Sin(timeSeconds * frequency + phase);
    const f32 angle = amount * (0.08f + static_cast<f32>((jointIndex * 13u + characterIndex * 7u) % 19u) * 0.006f);

    SIMDMatrix matrix = MatrixRotationRollPitchYaw(angle * 0.35f, angle, angle * 0.21f);
    matrix.v[3] = VectorSet(
        Sin(timeSeconds * (frequency + 0.23f) + phase) * 0.012f,
        Sin(timeSeconds * (frequency + 0.37f) + phase * 0.5f) * 0.008f,
        Sin(timeSeconds * (frequency + 0.19f) + phase * 0.75f) * 0.012f,
        1.0f
    );

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

    [[nodiscard]] u32 loadSkeletonJointCount()const{
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(!m_context.assetManager.loadSync(NWB::Impl::SkinnedMesh::AssetTypeName(), Name(s_MedicalWorkerMeshPath), loadedAsset)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: failed to load benchmark skinned mesh"));
            return 0u;
        }
        if(!loadedAsset || loadedAsset->assetType() != NWB::Impl::SkinnedMesh::AssetTypeName())
            return 0u;

        const auto* mesh = static_cast<const NWB::Impl::SkinnedMesh*>(loadedAsset.get());
        return mesh->skeletonJointCount();
    }

    void configureCamera(const BenchmarkView::Enum view){
        auto* transform = m_world->tryGetComponent<NWB::Impl::TransformComponent>(m_cameraEntity);
        if(!transform)
            return;

        switch(view){
        case BenchmarkView::Back:
            transform->position = Float4(0.0f, 1.1f, 4.0f, 0.0f);
            StoreFloat(QuaternionRotationRollPitchYaw(0.0f, s_PI, 0.0f), &transform->rotation);
            return;
        case BenchmarkView::Close:
            transform->position = Float4(0.0f, 1.1f, -2.0f, 0.0f);
            transform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        case BenchmarkView::Far:
            transform->position = Float4(0.0f, 1.1f, -7.0f, 0.0f);
            transform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        case BenchmarkView::Front:
        default:
            transform->position = Float4(0.0f, 1.1f, -4.0f, 0.0f);
            transform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }
    }

    void configureCase(){
        const BenchmarkCase& benchmarkCase = s_BenchmarkCases[m_caseIndex];
        m_runtimeMeshProvider.setMode(benchmarkCase.mode);
        configureCamera(benchmarkCase.view);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: begin mode={} view={}")
            , StringConvert(BenchmarkModeName(benchmarkCase.mode))
            , StringConvert(BenchmarkViewName(benchmarkCase.view))
        );
    }

    void finishCase(){
        const BenchmarkCase& benchmarkCase = s_BenchmarkCases[m_caseIndex];
        const BenchmarkAccumulation& accum = m_accumulations[m_caseIndex];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: result mode={} view={} frames={} cpu_ms={} skinning_gpu_ms={} bounds_gpu_ms={} mesh_dispatch_gpu_ms={} raster_gpu_ms={} skinning_samples={} bounds_samples={} mesh_samples={} raster_samples={}")
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
        f64 frustumOnlyGpuSeconds = 0.0;
        f64 frustumConeGpuSeconds = 0.0;
        u32 frustumOnlySamples = 0u;
        u32 frustumConeSamples = 0u;

        for(usize i = 0u; i < s_BenchmarkCaseCount; ++i){
            const BenchmarkCase& benchmarkCase = s_BenchmarkCases[i];
            const BenchmarkAccumulation& accum = m_accumulations[i];
            const f64 gpuSeconds = accum.meshDispatchSeconds + accum.rasterSeconds;
            const u32 gpuSamples = accum.meshDispatchSamples + accum.rasterSamples;
            if(benchmarkCase.mode == BenchmarkMode::FrustumOnly){
                frustumOnlyGpuSeconds += gpuSeconds;
                frustumOnlySamples += gpuSamples;
            }
            else if(benchmarkCase.mode == BenchmarkMode::FrustumCone){
                frustumConeGpuSeconds += gpuSeconds;
                frustumConeSamples += gpuSamples;
            }
        }

        const f64 frustumOnlyAverage = AverageSeconds(frustumOnlyGpuSeconds, frustumOnlySamples) * 1000.0;
        const f64 frustumConeAverage = AverageSeconds(frustumConeGpuSeconds, frustumConeSamples) * 1000.0;
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
            finishBenchmark();
            m_finished = true;
            m_finishDrainFrames = s_FinishDrainFrameCount;
            return;
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
                pose->localJoints[jointIndex] = BuildAnimatedJointMatrix(jointIndex, static_cast<u32>(entityIndex), timeSeconds);
        }
    }


public:
    explicit SkinnedConeBenchmarkProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
        , m_entities(context.objectArena)
    {}

    virtual ~SkinnedConeBenchmarkProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        const u32 skeletonJointCount = loadSkeletonJointCount();
        if(skeletonJointCount == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: benchmark mesh has no skeleton joints"));
            RequestQuit();
            return true;
        }
        auto* meshSystem = m_world->getSystem<NWB::Impl::MeshSystem>();
        auto* skinnedMeshSystem = m_world->getSystem<NWB::Impl::SkinnedMeshSystem>();
        if(!meshSystem || !skinnedMeshSystem || !m_runtimeMeshProvider.install(*meshSystem, *skinnedMeshSystem)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedConeBenchmark: failed to install benchmark runtime mesh provider"));
            RequestQuit();
            return true;
        }

        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::CreateSceneCameraEntity(*m_world, Float4(0.0f, 1.1f, -4.0f, 0.0f));
        m_cameraEntity = activeCamera.camera;
        NWB::Impl::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

        BenchmarkMeshRef mesh;
        mesh.virtualPath = Name(s_MedicalWorkerMeshPath);
        BenchmarkMaterialRef material;
        material.virtualPath = Name(s_SkinnedBenchmarkMaterialPath);
        m_entities.reserve(s_CharacterCount);

        for(u32 characterIndex = 0u; characterIndex < s_CharacterCount; ++characterIndex){
            auto entity = m_world->createEntity();
            auto& transform = entity.addComponent<NWB::Impl::TransformComponent>();
            transform.position = Float4((static_cast<f32>(characterIndex) - 1.0f) * 1.15f, 0.0f, 0.0f, 0.0f);
            transform.scale = Float4(1.0f, 1.0f, 1.0f, 0.0f);

            auto& skinnedMesh = entity.addComponent<NWB::Impl::SkinnedMeshComponent>();
            skinnedMesh.skinnedMesh = mesh;

            auto& pose = entity.addComponent<NWB::Impl::SkinnedMeshSkeletonPoseComponent>(m_context.objectArena);
            pose.parentJoints.resize(skeletonJointCount, NWB::Impl::s_SkinnedMeshSkeletonRootParent);
            pose.localJoints.resize(skeletonJointCount, NWB::Impl::MakeIdentitySkinnedMeshJointMatrix());

            auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
            renderer.material = material;
            m_entities.push_back(entity.id());
        }

        configureCase();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedConeBenchmark: spawned {} characters with {} joints each"), s_CharacterCount, skeletonJointCount);
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

            if(!m_quitRequested){
                m_quitRequested = true;
                RequestQuit();
            }
            return true;
        }

        collectPreviousFrame(delta);
        advanceCaseOrFinish();
        if(m_finished)
            return true;

        const f32 safeDelta = IsFinite(delta) && delta > 0.0f ? delta : 1.0f / 60.0f;
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
    NWB::Core::ECS::EntityID m_cameraEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    BenchmarkAccumulation m_accumulations[s_BenchmarkCaseCount] = {};
    f64 m_totalTimeSeconds = 0.0;
    u32 m_caseRenderedFrames = 0u;
    u32 m_finishDrainFrames = 0u;
    usize m_caseIndex = 0u;
    bool m_finished = false;
    bool m_quitRequested = false;
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
