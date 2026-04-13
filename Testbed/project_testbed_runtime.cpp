// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"

#include <stdexcept>

#include <logger/client/logger.h>


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
    NWB_FATAL_ASSERT_MSG(rendererSystem, NWB_TEXT("ProjectTestbed initialization failed: renderer system is missing in initial world"));
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

    const NWB::Core::Assets::AssetRef<NWB::Impl::Geometry> cubeGeometry(Name("project/meshes/cube"));
    const NWB::Core::Assets::AssetRef<NWB::Impl::Geometry> sphereGeometry(Name("project/meshes/sphere"));
    const NWB::Core::Assets::AssetRef<NWB::Impl::Geometry> tetrahedronGeometry(Name("project/meshes/tetrahedron"));
    const NWB::Core::Assets::AssetRef<NWB::Impl::Material> cubeMaterial(Name("project/materials/mat_test"));
    const NWB::Core::Assets::AssetRef<NWB::Impl::Material> transparentMaterial(Name("project/materials/mat_transparent"));

    auto cubeEntity = m_world->createEntity();
    auto& renderer = cubeEntity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
    renderer.geometry = cubeGeometry;
    renderer.material = cubeMaterial;

    auto sphereEntity = m_world->createEntity();
    auto& sphereRenderer = sphereEntity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
    sphereRenderer.geometry = sphereGeometry;
    sphereRenderer.material = transparentMaterial;

    auto tetrahedronEntity = m_world->createEntity();
    auto& tetrahedronRenderer = tetrahedronEntity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
    tetrahedronRenderer.geometry = tetrahedronGeometry;
    tetrahedronRenderer.material = transparentMaterial;

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
