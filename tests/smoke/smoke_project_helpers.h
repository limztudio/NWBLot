// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "arrow_yaw_input_handler.h"
#include "smoke_environment.h"

#include <loader/project_entry.h>

#include <impl/ecs_scene/module.h>

#include <core/common/log.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <core/graphics/runtime/runtime.h>

#include <global/algorithm.h>
#include <global/simd/frame.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_DegreesPerTurn = 360.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline NotNullUniquePtr<Core::ECS::World> CreateSmokeWorldOrDie(ProjectRuntimeContext& context, const TStringView projectName){
    auto world = MakeUnique<Core::ECS::World>(context.objectArena, context.cpuTasks);
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

[[nodiscard]] inline Core::ECS::EntityID CreateSmokeCamera(Core::ECS::World& world, const f32 cameraHeight, const f32 cameraDistance, const f32 cameraPitch){
    auto activeCameraEntity = world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<Impl::Scene::ActiveCameraComponent>();
    Float4 cameraPosition;
    StoreFloat(VectorXorInt(VectorSet(0.0f, cameraHeight, cameraDistance, 0.0f), s_SIMDFlipZ), cameraPosition);
    activeCamera.camera = Impl::Scene::CreateSceneCameraEntity(world, cameraPosition);
    auto* cameraTransform = world.tryGetComponent<Impl::Scene::TransformComponent>(activeCamera.camera);
    NWB_ASSERT(cameraTransform);
    StoreFloat(QuaternionRotationRollPitchYaw(cameraPitch, 0.0f, 0.0f), cameraTransform->rotation);

    return activeCamera.camera;
}

[[nodiscard]] inline f32 ReadSmokeFrozenYawFromEnvironment(const AStringView variableName){
    const auto parsed = ReadSmokeEnvironmentF32(variableName);
    return parsed ? *parsed : -1.0f;
}

[[nodiscard]] inline u32 RendererBaselineCaptureFreezeFrame(){
    static const u32 s_CaptureFrame = [](){
        const auto configuredFrame = ReadSmokeEnvironmentF32("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME");
        if(!configuredFrame || !IsFinite(*configuredFrame) || *configuredFrame < 1.0f){
            return 0u;
        }
        return static_cast<u32>(VectorGetX(VectorMin(VectorReplicate(*configuredFrame), VectorReplicate(1000000.0f))));
    }();
    return s_CaptureFrame;
}

[[nodiscard]] inline f32 RendererBaselineFixedDelta(){
    static const f32 s_FixedDelta = [](){
        const auto configuredDelta = ReadSmokeEnvironmentF32("NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS");
        if(!configuredDelta || !IsFinite(*configuredDelta) || *configuredDelta <= 0.0f || *configuredDelta > 1.0f){
            return 0.0f;
        }
        return *configuredDelta;
    }();
    return s_FixedDelta;
}

class YawSpinController final{
public:
    void update(const f32 safeDelta, const f32 frozenYaw, const bool frozenYawValid, const ArrowYawInputHandler& arrowYawInput, const f32 manualYawSpeed, const f32 spinSpeed, const f32 maxSpinDelta)noexcept{
        if(frozenYawValid){
            m_yaw = frozenYaw;
            return;
        }

        const SIMDVectorSingle manualAxis = arrowYawInput.axis();
        if(VectorGetX(manualAxis) != 0.0f)
            m_manualControl = true;

        const SIMDVectorSingle yaw = VectorReplicate(m_yaw);
        if(m_manualControl)
            m_yaw = VectorGetX(VectorMultiplyAddExpression(VectorScale(manualAxis, manualYawSpeed), VectorReplicate(safeDelta), yaw));
        else
            m_yaw = VectorGetX(VectorMultiplyAddExpression(VectorMin(VectorReplicate(safeDelta), VectorReplicate(maxSpinDelta)), VectorReplicate(spinSpeed), yaw));
    }

    [[nodiscard]] f32 yaw()const noexcept{
        return m_yaw;
    }

    [[nodiscard]] bool manualControl()const noexcept{
        return m_manualControl;
    }


private:
    f32 m_yaw = 0.0f;
    bool m_manualControl = false;
};

[[nodiscard]] inline SIMDVectorSingle CalculateSmokeYawDisplay(const SIMDVectorSingle yaw, const SIMDVectorSingle turn)noexcept{
    SIMDVectorSingle wrapped = VectorMod(yaw, turn);
    wrapped = VectorSelect(wrapped, VectorAdd(wrapped, turn), VectorLess(wrapped, VectorZero()));

    return VectorSelect(wrapped, VectorMultiply(wrapped, VectorDivide(VectorReplicate(s_DegreesPerTurn), turn)), s_SIMDMaskY);
}

inline void SetSmokeYawWindowTitle(ProjectRuntimeContext& context, const f32 yaw, const bool manualControl, const f32 twoPi){
    const SIMDVectorSingle display = CalculateSmokeYawDisplay(VectorReplicate(yaw), VectorReplicate(twoPi));

    static constexpr usize s_TitleCapacity = 192u;
    tchar title[s_TitleCapacity];
    NWB_TSPRINTF(
        title,
        s_TitleCapacity,
        NWB_TEXT("%.*s  |  yaw %.4f rad (%.2f deg)%s"),
        static_cast<i32>(QueryProjectWindowTitle().size()),
        QueryProjectWindowTitle().data(),
        VectorGetX(display),
        VectorGetY(display),
        manualControl ? NWB_TEXT("  [manual: <- ->]") : NWB_TEXT("")
    );
    context.graphics.setWindowTitle(TStringView(title));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

