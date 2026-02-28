// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ProjectTestbed::onStartup(NWB::ProjectRuntimeContext& context){
    if(!NWB::CreateBasicProjectWorld(context, m_world, m_rendererSystem))
        return false;

    NWB::Core::ECS::Entity cubeEntity = m_world->createEntity();
    m_world->addComponent<NWB::Core::ECSGraphics::CubeComponent>(cubeEntity);
    m_world->addComponent<NWB::Core::ECSGraphics::RendererComponent>(cubeEntity);

    return true;
}
void ProjectTestbed::onShutdown(NWB::ProjectRuntimeContext& context){
    NWB::DestroyBasicProjectWorld(context, m_world, m_rendererSystem);
}


bool ProjectTestbed::onUpdate(NWB::ProjectRuntimeContext& context, f32 delta){
    (void)context;

    if(m_world)
        m_world->tick(delta);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

