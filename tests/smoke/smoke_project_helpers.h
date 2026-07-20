#pragma once

#ifndef NWB_TESTS_SMOKE_PROJECT_HELPERS_H
#define NWB_TESTS_SMOKE_PROJECT_HELPERS_H

#include "arrow_yaw_input_handler.h"
#include "smoke_environment.h"

#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/module.h>
#include <global/math/frame.h>
#include <global/simplemath.h>
#include <impl/ecs_scene/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline constexpr f32 s_DegreesPerTurn = 360.0f;


[[nodiscard]] inline NotNullUniquePtr<Core::ECS::World> CreateSmokeWorldOrDie(
    ProjectRuntimeContext& context,
    const tchar* const projectName
){
    auto world = MakeUnique<Core::ECS::World>(context.objectArena, context.threadPool);
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("{} initialization failed: ECS world allocation failed"), projectName);
        throw RuntimeException("Smoke project initialization failed");
    }
    if(!context.shaderPathResolver){
        NWB_LOGGER_FATAL(NWB_TEXT("{} initialization failed: shader path resolver callback is null"), projectName);
        throw RuntimeException("Smoke project initialization failed");
    }

    return MakeNotNullUnique(Move(world));
}

[[nodiscard]] inline Core::ECS::EntityID CreateSmokeCamera(
    Core::ECS::World& world,
    const f32 cameraHeight,
    const f32 cameraDistance,
    const f32 cameraPitch
){
    auto activeCameraEntity = world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<Impl::Scene::ActiveCameraComponent>();
    activeCamera.camera = Impl::Scene::CreateSceneCameraEntity(
        world,
        Float4(0.0f, cameraHeight, -cameraDistance, 0.0f)
    );
    if(auto* cameraTransform = world.tryGetComponent<Impl::Scene::TransformComponent>(activeCamera.camera))
        StoreFloat(QuaternionRotationRollPitchYaw(cameraPitch, 0.0f, 0.0f), &cameraTransform->rotation);

    return activeCamera.camera;
}

[[nodiscard]] inline f32 ReadSmokeFrozenYawFromEnvironment(const char* const variableName){
    f32 parsed = -1.0f;
    return ReadSmokeEnvironmentF32(variableName, parsed) ? parsed : -1.0f;
}

class YawSpinController final{
public:
    void update(
        const f32 safeDelta,
        const f32 frozenYaw,
        const bool frozenYawValid,
        const ArrowYawInputHandler& arrowYawInput,
        const f32 manualYawSpeed,
        const f32 spinSpeed,
        const f32 maxSpinDelta
    ){
        if(frozenYawValid){
            m_yaw = frozenYaw;
            return;
        }

        const f32 manualAxis = arrowYawInput.axis();
        if(manualAxis != 0.0f)
            m_manualControl = true;

        if(m_manualControl)
            m_yaw += manualAxis * manualYawSpeed * safeDelta;
        else
            m_yaw += Min(safeDelta, maxSpinDelta) * spinSpeed;
    }

    [[nodiscard]] f32 yaw()const{
        return m_yaw;
    }

    [[nodiscard]] bool manualControl()const{
        return m_manualControl;
    }


private:
    f32 m_yaw = 0.0f;
    bool m_manualControl = false;
};

struct SmokeYawDisplay{
    f32 wrappedRadians;
    f32 degrees;
};

[[nodiscard]] inline SmokeYawDisplay MakeSmokeYawDisplay(const f32 yaw, const f32 twoPi){
    f32 wrapped = FMod(yaw, twoPi);
    if(wrapped < 0.0f)
        wrapped += twoPi;

    return {
        wrapped,
        wrapped * (s_DegreesPerTurn / twoPi),
    };
}

inline void SetSmokeYawWindowTitle(
    ProjectRuntimeContext& context,
    const f32 yaw,
    const bool manualControl,
    const f32 twoPi
){
    const SmokeYawDisplay display = MakeSmokeYawDisplay(yaw, twoPi);

    static constexpr usize s_TitleCapacity = 192u;
    tchar title[s_TitleCapacity];
    NWB_TSPRINTF(
        title,
        s_TitleCapacity,
        NWB_TEXT("%s  |  yaw %.4f rad (%.2f deg)%s"),
        QueryProjectWindowTitle(),
        display.wrappedRadians,
        display.degrees,
        manualControl ? NWB_TEXT("  [manual: <- ->]") : NWB_TEXT("")
    );
    const tchar* titlePtr = title;
    context.graphics.setWindowTitle(MakeNotNull(titlePtr));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

