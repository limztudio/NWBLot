// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_scene_helpers.h"

#include <loader/project_entry.h>
#include <impl/assets_skeleton/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ReflectionRoughnessCase{
    enum Enum : u8{ Static, Furnace, Camera, Transform, Material, Light, Deform, StaticDeform };
};

class ReflectionRoughnessScene final{
public:
    ReflectionRoughnessScene(ProjectRuntimeContext& context, Core::ECS::World& world,
        Core::ECS::EntityID camera, Core::ECS::EntityID light, f32 roughness);
    [[nodiscard]] bool create(AStringView caseName, bool finalState);
    [[nodiscard]] bool mutationCase()const{ return m_case >= ReflectionRoughnessCase::Camera && m_case <= ReflectionRoughnessCase::Deform; }
    [[nodiscard]] bool deforming()const{ return m_case == ReflectionRoughnessCase::Deform || m_case == ReflectionRoughnessCase::StaticDeform; }
    [[nodiscard]] bool applyMutation();

private:
    [[nodiscard]] Core::ECS::EntityID createPanel(const SmokeMaterialRef& material, const Float4& color,
        const Float4& position, const Float4& scale, f32 f0 = 0.f, f32 roughness = 0.f);
    [[nodiscard]] bool createDeformingSource();

private:
    ProjectRuntimeContext& m_context;
    Core::ECS::World& m_world;
    Vector<Impl::SkeletonJointMatrix, Core::Alloc::GlobalArena> m_bindJoints;
    Core::ECS::EntityID m_camera;
    Core::ECS::EntityID m_light;
    Core::ECS::EntityID m_red;
    Core::ECS::EntityID m_skeleton;
    f32 m_roughness;
    ReflectionRoughnessCase::Enum m_case = ReflectionRoughnessCase::Static;
    bool m_finalState = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

