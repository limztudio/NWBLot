// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShouldCreateTransparentRenderer(){
    const char* scene = std::getenv("NWB_TESTBED_SCENE");
    if(!scene || scene[0] == '\0')
        return true;

    if(std::strcmp(scene, "opaque") == 0 || std::strcmp(scene, "opaque-only") == 0)
        return false;
    if(std::strcmp(scene, "mixed") == 0 || std::strcmp(scene, "avboit") == 0 || std::strcmp(scene, "transparent") == 0)
        return true;

    NWB_LOGGER_WARNING(NWB_TEXT("ProjectTestbed: unknown NWB_TESTBED_SCENE='{}'; using mixed"), StringConvert(scene));
    return true;
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
    const NWB::Core::Assets::AssetRef<NWB::Impl::Material> cubeMaterial(Name("project/materials/mat_test"));
    const NWB::Core::Assets::AssetRef<NWB::Impl::Material> transparentCubeMaterial(Name("project/materials/mat_transparent"));

    auto cubeEntity = m_world->createEntity();
    auto& renderer = cubeEntity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
    renderer.geometry = cubeGeometry;
    renderer.material = cubeMaterial;

    const bool includeTransparentRenderer = ShouldCreateTransparentRenderer();
    if(includeTransparentRenderer){
        auto transparentCubeEntity = m_world->createEntity();
        auto& transparentRenderer = transparentCubeEntity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
        transparentRenderer.geometry = cubeGeometry;
        transparentRenderer.material = transparentCubeMaterial;
    }

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("ProjectTestbed: startup scene created ({})"),
        includeTransparentRenderer ? NWB_TEXT("mixed opaque/transparent") : NWB_TEXT("opaque-only")
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
