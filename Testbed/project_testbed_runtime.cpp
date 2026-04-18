// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"

#include <stdexcept>

#include <logger/client/logger.h>


namespace __hidden_project_testbed_runtime{


using TestbedGeometryRef = NWB::Core::Assets::AssetRef<NWB::Impl::Geometry>;
using TestbedMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


[[nodiscard]] static NWB::Core::ECS::EntityID CreateMainCameraEntity(NWB::Core::ECS::World& world){
    auto cameraEntity = world.createEntity();
    cameraEntity.addComponent<NWB::Core::ECS::TransformComponent>();
    cameraEntity.addComponent<NWB::Core::ECS::CameraComponent>();
    return cameraEntity.id();
}

static void CreateRendererEntity(
    NWB::Core::ECS::World& world,
    const TestbedGeometryRef& geometry,
    const TestbedMaterialRef& material
){
    auto entity = world.createEntity();
    entity.addComponent<NWB::Core::ECS::TransformComponent>();

    auto& renderer = entity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
    renderer.geometry = geometry;
    renderer.material = material;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NotNullUniquePtr<NWB::Core::ECS::World> ProjectTestbed::createInitialWorldOrDie(NWB::ProjectRuntimeContext& context){
    UniquePtr<NWB::Core::ECS::World> world;
    if(!NWB::CreateInitialProjectWorld(context, world)){
        NWB_LOGGER_FATAL(NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned false"));
        throw RuntimeException("ProjectTestbed initialization failed");
    }
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned null world"));
        throw RuntimeException("ProjectTestbed initialization failed");
    }
    return MakeNotNullUnique(Move(world));
}

NWB::Core::ECSGraphics::RendererSystem& ProjectTestbed::requireRendererSystemOrDie(NWB::Core::ECS::World& world){
    auto* rendererSystem = world.getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    NWB_FATAL_ASSERT_MSG(
        rendererSystem,
        NWB_TEXT("ProjectTestbed initialization failed: renderer system is missing in initial world")
    );
    return *rendererSystem;
}


ProjectTestbed::ProjectTestbed(NWB::ProjectRuntimeContext& context)
    : m_context(context)
    , m_world(createInitialWorldOrDie(context))
    , m_rendererSystem(requireRendererSystemOrDie(*m_world))
{}

ProjectTestbed::~ProjectTestbed(){
    NWB::DestroyInitialProjectWorld(m_context, m_world.owner());
}


bool ProjectTestbed::onStartup(){
    (void)m_rendererSystem;

    using TestbedGeometryRef = __hidden_project_testbed_runtime::TestbedGeometryRef;
    using TestbedMaterialRef = __hidden_project_testbed_runtime::TestbedMaterialRef;

    auto projectEntity = m_world->createEntity();
    auto& project = projectEntity.addComponent<NWB::Core::ECS::ProjectComponent>();
    project.mainCamera = __hidden_project_testbed_runtime::CreateMainCameraEntity(*m_world);

    const TestbedMaterialRef cubeMaterial(Name("project/materials/mat_test"));
    const TestbedMaterialRef transparentMaterial(Name("project/materials/mat_transparent"));

    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/cube")),
        cubeMaterial
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/sphere")),
        transparentMaterial
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/tetrahedron")),
        transparentMaterial
    );

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("ProjectTestbed: startup scene created ({})"),
        NWB_TEXT("opaque cube with transparent sphere/tetrahedron")
    );
    return true;
}

void ProjectTestbed::onShutdown(){
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ProjectTestbed: shutdown"));
}


bool ProjectTestbed::onUpdate(f32 delta){
    m_world->tick(delta);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

