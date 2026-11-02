// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "arrow_yaw_input_handler.h"
#include "framebuffer_capture.h"
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


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_DegreesPerTurn = 360.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class YawSpinController final{
public:
    void update(f32 safeDelta, f32 frozenYaw, b8 frozenYawValid, const ArrowYawInputHandler& arrowYawInput, f32 manualYawSpeed, f32 spinSpeed, f32 maxSpinDelta)noexcept;

    [[nodiscard]] f32 yaw()const noexcept{ return m_yaw; }

    [[nodiscard]] b8 manualControl()const noexcept{ return m_manualControl; }


private:
    f32 m_yaw = 0.0f;
    b8 m_manualControl = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SmokeBoxBounds{
    Float3U minimum;
    Float3U maximum;
};

[[nodiscard]] inline constexpr b8 SmokeBoxBoundsValid(const SmokeBoxBounds& box)noexcept{
    return box.minimum.x < box.maximum.x && box.minimum.y < box.maximum.y && box.minimum.z < box.maximum.z;
}

[[nodiscard]] b8 BuildSmokeBoxSubtractionPieces(const SmokeBoxBounds& box, const SmokeBoxBounds& cutter, Array<SmokeBoxBounds, 6>& outPieces)noexcept;

[[nodiscard]] NotNullUniquePtr<Core::ECS::World> CreateSmokeWorldOrDie(TStringView projectName, ProjectRuntimeContext& context);
[[nodiscard]] Core::ECS::EntityID CreateSmokeCamera(f32 cameraHeight, f32 cameraDistance, f32 cameraPitch, Core::ECS::World& world);
[[nodiscard]] f32 ReadSmokeFrozenYawFromEnvironment(AStringView variableName);
[[nodiscard]] f32 ReadSmokeFrozenYawCached(AStringView variableName);
[[nodiscard]] u32 RendererBaselineCaptureFreezeFrame();
[[nodiscard]] f32 RendererBaselineFixedDelta();
[[nodiscard]] SIMDVectorSingle CalculateSmokeYawDisplay(SIMDVectorSingle yaw, SIMDVectorSingle turn)noexcept;
void SetSmokeYawWindowTitle(f32 yaw, b8 manualControl, f32 twoPi, ProjectRuntimeContext& context);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

