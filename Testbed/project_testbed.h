// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <loader/project_entry.h>

#include <core/ecs/ecs.h>
#include <core/ecs_graphics/ecs_graphics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ProjectTestbed final : public NWB::IProjectEntryCallbacks{
public:
    virtual NWB::ProjectFrameClientSize queryFrameClientSize()const override;

public:
    virtual bool onStartup(NWB::ProjectRuntimeContext& context)override;
    virtual void onShutdown(NWB::ProjectRuntimeContext& context)override;

public:
    virtual bool onUpdate(NWB::ProjectRuntimeContext& context, f32 delta)override;


private:
    NWB::Core::ECSGraphics::RendererSystem* m_rendererSystem = nullptr;
    UniquePtr<NWB::Core::ECS::World> m_world;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

