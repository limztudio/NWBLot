// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/mesh/frame_math.h>
#include <core/graphics/module.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material_instance.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_multi_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using SmokeMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


static constexpr f32 s_CameraStartDepth = 2.2f;
static constexpr f32 s_CameraTargetY = 0.85f;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;
static constexpr AStringView s_CubeMeshPath = "project/meshes/cube";
static constexpr AStringView s_SmokeBxdfSurfaceMaterialInterface = "project/shaders/smoke_bxdf_surface";
static constexpr AStringView s_TransparentSharedMaterialPath = "project/smoke/transparent_multi/materials/shared";


[[nodiscard]] static NWB::Core::ECS::EntityID CreateTransparentStaticMeshEntity(
    NWB::Core::ECS::World& world,
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView meshPath,
    const AStringView materialPath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale
){
    SmokeMeshRef mesh;
    mesh.virtualPath = Name(meshPath);
    SmokeMaterialRef material;
    material.virtualPath = Name(materialPath);

    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Impl::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = scale;

    auto& meshComponent = entity.addComponent<NWB::Impl::MeshComponent>();
    meshComponent.mesh = mesh;

    auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
    renderer.material = material;

    const Name materialInterface(s_SmokeBxdfSurfaceMaterialInterface);
    entity.addComponent<NWB::Impl::MaterialInstanceComponent>(arena, materialInterface);
    if(!NWB::Impl::SetMaterialMutableFloat4(
        world,
        entity.id(),
        materialInterface,
        "runtime.color_tint",
        colorTint
    ))
        return NWB::Core::ECS::ENTITY_ID_INVALID;

    return entity.id();
}


class TransparentMultiSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("TransparentMultiSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("TransparentMultiSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("TransparentMultiSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("TransparentMultiSmokeProject initialization failed");
        }

        world->addSystem<NWB::Impl::MeshSystem>(*world);
        if(!world->getSystem<NWB::Impl::MeshSystem>()){
            NWB_LOGGER_FATAL(NWB_TEXT("TransparentMultiSmokeProject initialization failed: mesh system is missing"));
            throw RuntimeException("TransparentMultiSmokeProject initialization failed");
        }

        auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(
            *world,
            context.graphics,
            context.assetManager,
            context.shaderPathResolver
        );
        if(!world->getSystem<NWB::Impl::RendererSystem>()){
            NWB_LOGGER_FATAL(NWB_TEXT("TransparentMultiSmokeProject initialization failed: renderer system is missing"));
            throw RuntimeException("TransparentMultiSmokeProject initialization failed");
        }

        context.graphics.addRenderPassToBack(rendererSystem);
        return MakeNotNullUnique(Move(world));
    }

    void destroyWorld(){
        if(!m_world.owner())
            return;

        auto* rendererSystem = m_world->getSystem<NWB::Impl::RendererSystem>();
        if(rendererSystem)
            m_context.graphics.removeRenderPass(*rendererSystem);

        m_context.graphics.waitAllJobs();
        if(auto* device = m_context.graphics.getDevice())
            device->waitForIdle();

        m_world->clear();
        m_world.owner().reset();
    }


public:
    explicit TransparentMultiSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~TransparentMultiSmokeProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(0.0f, s_CameraTargetY, -s_CameraStartDepth)
        );
        NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

        const auto cubeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_CubeMeshPath,
            s_TransparentSharedMaterialPath,
            Float4(1.0f, 0.42f, 0.20f, 0.42f),
            Float4(-0.68f, s_CameraTargetY, 0.02f),
            Float4(0.62f, 0.62f, 0.62f)
        );
        const auto centerCubeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_CubeMeshPath,
            s_TransparentSharedMaterialPath,
            Float4(0.10f, 1.0f, 0.45f, 0.42f),
            Float4(0.0f, s_CameraTargetY, 0.0f),
            Float4(0.78f, 0.78f, 0.78f)
        );
        const auto rightCubeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_CubeMeshPath,
            s_TransparentSharedMaterialPath,
            Float4(0.12f, 0.44f, 1.0f, 0.42f),
            Float4(0.68f, s_CameraTargetY, 0.04f),
            Float4(0.68f, 0.68f, 0.68f)
        );
        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid() && cubeEntity.valid() && centerCubeEntity.valid() && rightCubeEntity.valid(),
            NWB_TEXT("TransparentMultiSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created"));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_world->tick(safeDelta);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_transparent_multi_smoke::TransparentMultiSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

