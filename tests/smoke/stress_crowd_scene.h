// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "stress_csg_scene.h"

#include <core/alloc/general.h>

#include <global/simd/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{
struct ProjectRuntimeContext;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class StressCrowdScene final{
public:
    StressCrowdScene(ProjectRuntimeContext& context, Core::ECS::World& world)noexcept;


public:
    [[nodiscard]] b8 initialize(ProjectRuntimeContext& context, u32 charactersPerClass, b8 comparison, b8 opaqueOnly, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 update(f32 yaw);


private:
    [[nodiscard]] Core::ECS::EntityID createCharacter(u32 index);
    [[nodiscard]] Core::ECS::EntityID createWall(const Float4& position, const Float4& colorTint, f32 yawDeg, f32 spanScale);
    [[nodiscard]] Core::ECS::EntityID createCeiling(const Float4& colorTint);
    void spinCharacters(f32 yaw)noexcept;


private:
    Core::ECS::World& m_world;
    Vector<Core::ECS::EntityID, Core::Alloc::GlobalArena> m_characterOwners;
    StressCsgScene m_csgScene;
    Core::ECS::EntityID m_groundEntity = Core::ECS::s_InvalidEntityId;
    Core::ECS::EntityID m_wallPosX = Core::ECS::s_InvalidEntityId;
    Core::ECS::EntityID m_wallNegX = Core::ECS::s_InvalidEntityId;
    Core::ECS::EntityID m_wallPosZ = Core::ECS::s_InvalidEntityId;
    Core::ECS::EntityID m_ceiling = Core::ECS::s_InvalidEntityId;
    u32 m_charactersPerClass = 0u;
    b8 m_comparison = false;
    b8 m_opaqueOnly = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

