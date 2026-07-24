// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/kernel/module.h>
#include <impl/ecs_skeleton/runtime_helpers.h>
#include <impl/ecs_mesh/skinning/module.h>

#include <global/environment.h>

#include "smoke_skinned_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_culling_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using BenchmarkModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using BenchmarkMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using NWB::Tests::Smoke::AddSmokeSkinnedRenderSystems;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::FindSpawnedModelObject;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;

inline constexpr Name s_EnvironmentFlagArena("tests/smoke/skinning_culling_benchmark/environment_flag");

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
    NWB::Core::Alloc::GlobalArena arena(s_EnvironmentFlagArena);
    AString<NWB::Core::Alloc::GlobalArena> value(arena);
    if(!ReadEnvironmentVariable(name, value))
        return false;

    return !value.empty() && value[0] != '\0' && value[0] != '0';
}

[[nodiscard]] static bool StaticPreviewEnabled(){
    return EnvironmentFlagEnabled(s_StaticPreviewEnv);
}

static void BuildAnimatedJointMatrix(
    const SIMDMatrix& bindJoint,
    const u32 jointIndex,
    const u32 characterIndex,
    const f32 timeSeconds,
    const bool staticPreview,
    SIMDMatrix& outMatrix
){
    outMatrix = bindJoint;
    if(!ShouldAnimateJoint(jointIndex, characterIndex, staticPreview))
        return;

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
    matrix = NWB::Impl::SkeletonRuntime::MultiplyJointMatrices(bindJoint, matrix);
    if(!NWB::Impl::SkeletonRuntime::IsInvertibleAffineJointMatrix(matrix))
        return;

    outMatrix = matrix;
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

        AddSmokeSkinnedRenderSystems(*world, context);
        return MakeNotNullUnique(Move(world));
    }

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
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: end repeat={}/{} mode={} view={} sample_frames={}")
            , m_repeatIndex + 1u
            , s_BenchmarkRepeatCount
            , StringConvert(BenchmarkModeName(benchmarkCase.mode))
            , StringConvert(BenchmarkViewName(benchmarkCase.view))
            , s_SampleFrameCount
        );
    }

    void finishBenchmark(){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinningCullingBenchmark: completed repeats={} cases={} warmup_frames={} sample_frames={}")
            , s_BenchmarkRepeatCount
            , static_cast<u32>(s_BenchmarkCaseCount)
            , s_WarmupFrameCount
            , s_SampleFrameCount
        );
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

            for(u32 jointIndex = 0u; jointIndex < pose->localJoints.size(); ++jointIndex){
                SIMDMatrix animatedJoint;
                BuildAnimatedJointMatrix(
                    LoadFloat(m_bindJoints[jointIndex]),
                    jointIndex,
                    static_cast<u32>(entityIndex),
                    timeSeconds,
                    m_staticPreview,
                    animatedJoint
                );
                StoreFloat(animatedJoint, &pose->localJoints[jointIndex]);
            }
        }
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
        if(!m_staticPreview)
            m_context.setTelemetryCapture(NWB::Core::Telemetry::CaptureOptions::All());

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

        SyncSmokeModelRuntimes(*m_world);
        for(const NWB::Core::ECS::EntityID owner : modelOwners){
            const NWB::Core::ECS::EntityID skeletonEntity = FindSpawnedModelObject(
                *m_world,
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
        if(!m_staticPreview){
            if(!m_context.flushTelemetryUpload(true))
                NWB_LOGGER_WARNING(NWB_TEXT("SkinningCullingBenchmark: failed to flush telemetry upload during shutdown"));
        }
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
    f64 m_totalTimeSeconds = 0.0;
    usize m_caseIndex = 0u;
    NWB::Core::ECS::EntityID m_cameraEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    u32 m_caseRenderedFrames = 0u;
    u32 m_finishDrainFrames = 0u;
    u32 m_repeatIndex = 0u;
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

