// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/ecs/entity_id.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Core::ECS{ class World; }

namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TransparentMultiSceneTransforms final{
public:
    static constexpr f32 s_CameraTargetY = 0.85f;
    static constexpr Float4 s_TransparentLeftShapeBasePosition{ -0.68f, s_CameraTargetY, 0.02f, 0.0f };
    static constexpr Float4 s_TransparentCenterShapeBasePosition{ 0.0f, s_CameraTargetY, 0.0f, 0.0f };
    static constexpr Float4 s_TransparentRightShapeBasePosition{ 0.68f, s_CameraTargetY, 0.04f, 0.0f };
    // Opaque occluders orbit through colored shadows to exercise their multiplicative combination.
    static constexpr Float4 s_OpaqueLeftShapeBasePosition{ -0.34f, s_CameraTargetY, 0.30f, 0.0f };
    static constexpr Float4 s_OpaqueRightShapeBasePosition{ 0.34f, s_CameraTargetY, 0.30f, 0.0f };


public:
    void update(f32 yaw, Core::ECS::World& world)const noexcept;


public:
    Core::ECS::EntityID leftShape = {};
    Core::ECS::EntityID centerShape = {};
    Core::ECS::EntityID rightShape = {};
    Core::ECS::EntityID opaqueLeftShape = {};
    Core::ECS::EntityID opaqueRightShape = {};
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    Core::ECS::EntityID csgReceiver = {};
    Core::ECS::EntityID csgCutter = {};
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

