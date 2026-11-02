// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_project_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void YawSpinController::update(const f32 safeDelta, const f32 frozenYaw, const b8 frozenYawValid, const ArrowYawInputHandler& arrowYawInput, const f32 manualYawSpeed, const f32 spinSpeed, const f32 maxSpinDelta)noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 BuildSmokeBoxSubtractionPieces(const SmokeBoxBounds& box, const SmokeBoxBounds& cutter, Array<SmokeBoxBounds, 6>& outPieces)noexcept{
    SmokeBoxBounds intersection;
    StoreFloat(VectorMax(LoadFloat(box.minimum), LoadFloat(cutter.minimum)), intersection.minimum);
    StoreFloat(VectorMin(LoadFloat(box.maximum), LoadFloat(cutter.maximum)), intersection.maximum);
    const Float3U& lower = intersection.minimum;
    const Float3U& upper = intersection.maximum;
    outPieces = {{
        { box.minimum, Float3U(lower.x, box.maximum.y, box.maximum.z) },
        { Float3U(upper.x, box.minimum.y, box.minimum.z), box.maximum },
        { Float3U(lower.x, box.minimum.y, box.minimum.z), Float3U(upper.x, lower.y, box.maximum.z) },
        { Float3U(lower.x, upper.y, box.minimum.z), Float3U(upper.x, box.maximum.y, box.maximum.z) },
        { Float3U(lower.x, lower.y, box.minimum.z), Float3U(upper.x, upper.y, lower.z) },
        { Float3U(lower.x, lower.y, upper.z), Float3U(upper.x, upper.y, box.maximum.z) },
    }};
    return SmokeBoxBoundsValid(intersection);
}

NotNullUniquePtr<Core::ECS::World> CreateSmokeWorldOrDie(const TStringView projectName, ProjectRuntimeContext& context){
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

Core::ECS::EntityID CreateSmokeCamera(const f32 cameraHeight, const f32 cameraDistance, const f32 cameraPitch, Core::ECS::World& world){
    auto activeCameraEntity = world.createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<Impl::Scene::ActiveCameraComponent>();
    activeCamera.camera = Impl::Scene::CreateSceneCameraEntity(world, Float4(0.0f, cameraHeight, -cameraDistance, 0.0f));
    auto& cameraTransform = *MakeNotNull(world.tryGetComponent<Impl::Scene::TransformComponent>(activeCamera.camera));
    StoreFloat(QuaternionRotationRollPitchYaw(cameraPitch, 0.0f, 0.0f), cameraTransform.rotation);

    return activeCamera.camera;
}

f32 ReadSmokeFrozenYawFromEnvironment(const AStringView variableName){
    const auto parsed = ReadSmokeEnvironmentF32(variableName);
    return parsed ? *parsed : -1.0f;
}

f32 ReadSmokeFrozenYawCached(const AStringView variableName){
    static const f32 s_Yaw = ReadSmokeFrozenYawFromEnvironment(variableName);
    return s_Yaw;
}

u32 RendererBaselineCaptureFreezeFrame(){
    static const u32 s_CaptureFrame = [](){
        const auto configuredFrame = ReadSmokeEnvironmentF32("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME");
        if(!configuredFrame || !IsFinite(*configuredFrame) || *configuredFrame < 1.0f)
            return 0u;
        return static_cast<u32>(Min(*configuredFrame, 1000000.0f));
    }();
    return s_CaptureFrame;
}

f32 RendererBaselineFixedDelta(){
    static const f32 s_FixedDelta = [](){
        const auto configuredDelta = ReadSmokeEnvironmentF32("NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS");
        if(!configuredDelta || !IsFinite(*configuredDelta) || *configuredDelta <= 0.0f || *configuredDelta > 1.0f)
            return 0.0f;
        return *configuredDelta;
    }();
    return s_FixedDelta;
}

SIMDVectorSingle CalculateSmokeYawDisplay(const SIMDVectorSingle yaw, const SIMDVectorSingle turn)noexcept{
    SIMDVectorSingle wrapped = VectorMod(yaw, turn);
    wrapped = VectorSelect(wrapped, VectorAdd(wrapped, turn), VectorLess(wrapped, VectorZero()));

    return VectorSelect(wrapped, VectorMultiply(wrapped, VectorDivide(VectorReplicate(s_DegreesPerTurn), turn)), s_SIMDMaskY);
}

void SetSmokeYawWindowTitle(const f32 yaw, const b8 manualControl, const f32 twoPi, ProjectRuntimeContext& context){
    const SIMDVectorSingle display = CalculateSmokeYawDisplay(VectorReplicate(yaw), VectorReplicate(twoPi));

    static constexpr usize s_TitleCapacity = 192u;
    Array<tchar, s_TitleCapacity> title{};
    NWB_TSPRINTF(
        title.data(),
        s_TitleCapacity,
        NWB_TEXT("%.*s  |  yaw %.4f rad (%.2f deg)%s"),
        static_cast<i32>(QueryProjectWindowTitle().size()),
        QueryProjectWindowTitle().data(),
        VectorGetX(display),
        VectorGetY(display),
        manualControl ? NWB_TEXT("  [manual: <- ->]") : NWB_TEXT("")
    );
    context.graphics.setWindowTitle(TStringView(title.data()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

