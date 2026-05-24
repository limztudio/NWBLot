// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/ecs.h>
#include <core/geometry/frame_math.h>
#include <core/graphics/graphics.h>
#include <impl/assets_geometry/geometry_asset.h>
#include <impl/assets_material/material_asset.h>
#include <impl/ecs_camera/camera.h>
#include <impl/ecs_geometry/ecs_geometry.h>
#include <impl/ecs_lighting/lighting.h>
#include <impl/ecs_render/ecs_render.h>
#include <impl/ecs_scene/scene.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_multi_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeGeometryRef = NWB::Core::Assets::AssetRef<NWB::Impl::Geometry>;
using SmokeMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


static constexpr f32 s_CameraStartDepth = 2.2f;
static constexpr f32 s_CameraTargetY = 0.85f;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;
static constexpr AStringView s_CubeGeometryPath = "project/meshes/cube";
static constexpr AStringView s_TransparentOrangeMaterialPath = "project/smoke/transparent_multi/materials/orange";
static constexpr AStringView s_TransparentGreenMaterialPath = "project/smoke/transparent_multi/materials/green";
static constexpr AStringView s_TransparentBlueMaterialPath = "project/smoke/transparent_multi/materials/blue";


[[nodiscard]] static NWB::Core::ECS::EntityID CreateTransparentStaticMeshEntity(
    NWB::Core::ECS::World& world,
    const AStringView geometryPath,
    const AStringView materialPath,
    const Float4& position,
    const Float4& scale
){
    SmokeGeometryRef geometry;
    geometry.virtualPath = Name(geometryPath);
    SmokeMaterialRef material;
    material.virtualPath = Name(materialPath);

    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Impl::TransformComponent>();
    transform.position = position;
    transform.scale = scale;

    auto& geometryComponent = entity.addComponent<NWB::Impl::GeometryComponent>();
    geometryComponent.geometry = geometry;

    auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
    renderer.material = material;
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

        world->addSystem<NWB::Impl::GeometrySystem>(*world);
        if(!world->getSystem<NWB::Impl::GeometrySystem>()){
            NWB_LOGGER_FATAL(NWB_TEXT("TransparentMultiSmokeProject initialization failed: geometry system is missing"));
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
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::CreateSceneCameraEntity(
            *m_world,
            Float4(0.0f, s_CameraTargetY, -s_CameraStartDepth)
        );
        NWB::Impl::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

        const auto cubeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            s_CubeGeometryPath,
            s_TransparentOrangeMaterialPath,
            Float4(-0.68f, s_CameraTargetY, 0.02f),
            Float4(0.62f, 0.62f, 0.62f)
        );
        const auto centerCubeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            s_CubeGeometryPath,
            s_TransparentGreenMaterialPath,
            Float4(0.0f, s_CameraTargetY, 0.0f),
            Float4(0.78f, 0.78f, 0.78f)
        );
        const auto rightCubeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            s_CubeGeometryPath,
            s_TransparentBlueMaterialPath,
            Float4(0.68f, s_CameraTargetY, 0.04f),
            Float4(0.68f, 0.68f, 0.68f)
        );
        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid() && cubeEntity.valid() && centerCubeEntity.valid() && rightCubeEntity.valid(),
            NWB_TEXT("TransparentMultiSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: three transparent static meshes created"));
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

