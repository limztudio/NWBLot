// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"

#include <stdexcept>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NotNullUniquePtr<NWB::Core::ECS::World> ProjectTestbed::createInitialWorldOrDie(NWB::ProjectRuntimeContext& context){
    UniquePtr<NWB::Core::ECS::World> world;
    if(!NWB::CreateInitialProjectWorld(context, world)){
        NWB_LOGGER_FATAL(NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned false"));
        throw std::runtime_error("ProjectTestbed initialization failed");
    }
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned null world"));
        throw std::runtime_error("ProjectTestbed initialization failed");
    }
    return MakeNotNullUnique(Move(world));
}

NWB::Core::ECSGraphics::RendererSystem& ProjectTestbed::requireRendererSystemOrDie(NWB::Core::ECS::World& world){
    auto* rendererSystem = world.getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    NWB_ASSERT_MSG(rendererSystem, "ProjectTestbed initialization failed: renderer system is missing in initial world");
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

    NWB::Core::ECS::Entity cubeEntity(*m_world, m_world->createEntity());
    cubeEntity.addComponent<NWB::Core::ECSGraphics::CubeComponent>();
    cubeEntity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();

    return true;
}
void ProjectTestbed::onShutdown(){
}


bool ProjectTestbed::onUpdate(f32 delta){
    m_world->tick(delta);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

