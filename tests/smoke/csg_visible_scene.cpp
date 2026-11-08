// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_visible_scene.h"
#include "csg_smoke_helpers.h"

#include <tests/smoke/scene_types.adhd.h>

#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_visible_scene{

using NWB::Tests::Smoke::AddStaticCsgMeshReceiver;
using NWB::Tests::Smoke::AssignCsgCutterParameters;
using NWB::Tests::Smoke::AssignCsgCutterTransform;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::s_SmokeSurfaceMaterialInterface;
using CsgVisibleMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using CsgVisibleMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;

static constexpr f32 s_CameraTargetY = NWB::Tests::Smoke::CsgVisibleScene::s_CameraTargetY;
static constexpr Float2U s_ShapeGridHalfSpacing = Float2U(1.55f, 1.05f);
static constexpr f32 s_ReceiverBaseScale = NWB::Tests::Smoke::s_CsgVisibleReceiverBaseScale;
static constexpr f32 s_ReceiverScale = NWB::Tests::Smoke::s_CsgVisibleReceiverScale;
static constexpr f32 s_CutterScale = s_ReceiverScale / s_ReceiverBaseScale;
static constexpr usize s_CsgVisibleShapeCount = NWB::Tests::Smoke::CsgVisibleScene::s_ShapeCount;
static constexpr CsgVisibleMeshRef s_CubeMesh{"project/meshes/cube_hard_edges"};
static constexpr f32 s_OverlappingReceiverDepthOffset = NWB::Tests::Smoke::s_CsgVisibleOverlappingReceiverDepthOffset;

namespace CsgVisibleShapeSlot{
    enum Enum : usize{
        Plane,
        Box,
        Sphere,
        Capsule,
        kCount
    };
};

static_assert(CsgVisibleShapeSlot::kCount == s_CsgVisibleShapeCount, "CSG visible shape table size must stay in sync");

inline constexpr Array<Name, s_CsgVisibleShapeCount> s_CsgVisibleReceiverGroups = {
    Name(NWB::Tests::Smoke::s_SmokeSceneNameProjectSmokeCsgVisiblePlaneReceiver),
    Name(NWB::Tests::Smoke::s_SmokeSceneNameProjectSmokeCsgVisibleBoxReceiver),
    Name(NWB::Tests::Smoke::s_SmokeSceneNameProjectSmokeCsgVisibleSphereReceiver),
    Name(NWB::Tests::Smoke::s_SmokeSceneNameProjectSmokeCsgVisibleCapsuleReceiver),
};

[[nodiscard]] static NWB::Core::ECS::EntityID CreateSolidCubeEntity(const Name receiverGroup, const Float4& colorTint, const Float4& position, const Float4& scale, const b8 csgReceiver, const CsgVisibleMaterialRef& material, NWB::Core::ECS::World& world){
    const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(s_CubeMesh, material, s_SmokeSurfaceMaterialInterface, colorTint, position, scale, world);
    if(!entity.valid())
        return NWB::Core::ECS::s_InvalidEntityId;

    if(csgReceiver)
        AddStaticCsgMeshReceiver(entity, receiverGroup, true, false, world);

    return entity;
}

[[nodiscard]] static SIMDVectorSingle BuildCubeRotation(const f32 time, const f32 phase)noexcept{
    const SIMDVectorSingle angles = VectorMultiply(VectorSet(time, 0.0f, time, 0.0f), VectorSet(0.35f, 0.0f, 0.18f, 0.0f));
    return QuaternionRotationRollPitchYawFromVector(VectorSelect(angles, VectorSet(0.0f, time + phase, 0.0f, 0.0f), s_SIMDMaskY));
}

static void ApplyCubeRotation(const NWB::Core::ECS::EntityID entity, const SIMDVectorSingle rotation, NWB::Core::ECS::World& world)noexcept{
    auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
    if(!transform)
        return;

    StoreFloat(rotation, transform->rotation);
}

[[nodiscard]] static SIMDVectorSingle CsgVisibleShapePosition(const usize shapeSlot)noexcept{
    // Move only lane Y.
    const SIMDVectorSingle offsetY = VectorSet(0.0f, s_ShapeGridHalfSpacing.y, 0.0f, 0.0f);
    const SIMDVectorSingle left = VectorSet(-s_ShapeGridHalfSpacing.x, s_CameraTargetY, 0.0f, 0.0f);
    const SIMDVectorSingle right = VectorSet(s_ShapeGridHalfSpacing.x, s_CameraTargetY, 0.0f, 0.0f);
    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane: return VectorAdd(left, offsetY);
    case CsgVisibleShapeSlot::Box: return VectorAdd(right, offsetY);
    case CsgVisibleShapeSlot::Sphere: return VectorSubtract(left, offsetY);
    case CsgVisibleShapeSlot::Capsule: return VectorSubtract(right, offsetY);
    default: return VectorSet(0.0f, s_CameraTargetY, 0.0f, 0.0f);
    }
}

[[nodiscard]] static Float4 CsgVisibleShapeColor(const usize shapeSlot)noexcept{
    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane: return Float4(0.24f, 0.56f, 0.86f, 1.0f);
    case CsgVisibleShapeSlot::Box: return Float4(0.72f, 0.40f, 0.20f, 1.0f);
    case CsgVisibleShapeSlot::Sphere: return Float4(0.34f, 0.70f, 0.42f, 1.0f);
    case CsgVisibleShapeSlot::Capsule: return Float4(0.84f, 0.28f, 0.44f, 1.0f);
    default: return Float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

[[nodiscard]] static SIMDVectorSingle CsgVisibleCutterLocalOffset(const usize shapeSlot)noexcept{
    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane: return VectorZero();
    case CsgVisibleShapeSlot::Box: return VectorSet(0.0f, 0.0f, -0.30f * s_CutterScale, 0.0f);
    case CsgVisibleShapeSlot::Sphere: return VectorSet(0.0f, 0.0f, -0.32f * s_CutterScale, 0.0f);
    case CsgVisibleShapeSlot::Capsule: return VectorSet(0.0f, 0.0f, -0.32f * s_CutterScale, 0.0f);
    default: return VectorZero();
    }
}

static void ApplyCutterTransform(const NWB::Core::ECS::EntityID cutterEntity, const SIMDVectorSingle receiverCenter, const SIMDVectorSingle receiverRotation, const SIMDVectorSingle cutterLocalOffset, NWB::Core::ECS::World& world)noexcept{
    auto* cutter = world.tryGetComponent<NWB::Impl::CsgCutterComponent>(cutterEntity);
    if(!cutter)
        return;

    const SIMDVectorSingle cutterCenter = VectorAdd(receiverCenter, Vector3Rotate(cutterLocalOffset, receiverRotation));
    AssignCsgCutterTransform(cutterCenter, receiverRotation, *cutter);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateCutter(const usize shapeSlot, const Name receiverGroup, const SIMDVectorSingle center, NWB::Core::ECS::World& world){
    auto cutterEntity = world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
    cutter.receiverGroup = receiverGroup;
    AssignCsgCutterTransform(center, QuaternionIdentity(), cutter);

    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane:{
        cutter.shapeType = Name(NWB::Impl::s_CsgPlaneShapeName);
        NWB::Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(0.0f, 0.0f, 1.0f, 0.0f);
        AssignCsgCutterParameters(parameters, cutter);
        break;
    }
    case CsgVisibleShapeSlot::Box:{
        cutter.shapeType = Name(NWB::Impl::s_CsgBoxShapeName);
        NWB::Impl::CsgBoxShapeParameters parameters;
        parameters.halfExtents = Float4(0.30f * s_CutterScale, 0.30f * s_CutterScale, 0.56f * s_CutterScale, 0.0f);
        AssignCsgCutterParameters(parameters, cutter);
        break;
    }
    case CsgVisibleShapeSlot::Sphere:{
        cutter.shapeType = Name(NWB::Impl::s_CsgSphereShapeName);
        NWB::Impl::CsgSphereShapeParameters parameters;
        parameters.radius = Float4(0.34f * s_CutterScale, 0.0f, 0.0f, 0.0f);
        AssignCsgCutterParameters(parameters, cutter);
        break;
    }
    case CsgVisibleShapeSlot::Capsule:{
        cutter.shapeType = Name(NWB::Impl::s_CsgCapsuleShapeName);
        NWB::Impl::CsgCapsuleShapeParameters parameters;
        parameters.radiusHalfHeight = Float4(0.22f * s_CutterScale, 0.28f * s_CutterScale, 0.0f, 0.0f);
        AssignCsgCutterParameters(parameters, cutter);
        break;
    }
    default:
        cutter.active = false;
        break;
    }
    return cutterEntity.id();
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CsgVisibleScene::create(const SmokeMaterialRef& material, const b8 overlapping, const Core::ECS::EntityID camera, const Core::ECS::EntityID light, Core::ECS::World& world){
    using namespace __hidden_csg_visible_scene;
    for(usize shapeSlot = 0u; shapeSlot < s_CsgVisibleShapeCount; ++shapeSlot){
        const SIMDVectorSingle receiverPositionVector = CsgVisibleShapePosition(shapeSlot);
        Float4 receiverPosition;
        StoreFloat(receiverPositionVector, receiverPosition);
        const SIMDVectorSingle cutterLocalOffset = CsgVisibleCutterLocalOffset(shapeSlot);
        const SIMDVectorSingle cutterPosition = VectorSetW(VectorAdd(receiverPositionVector, cutterLocalOffset), 0.0f);
        m_receivers[shapeSlot] = CreateSolidCubeEntity(s_CsgVisibleReceiverGroups[shapeSlot], CsgVisibleShapeColor(shapeSlot), receiverPosition, Float4(s_ReceiverScale, s_ReceiverScale, s_ReceiverScale, 0.0f), true, material, world);
        m_cutters[shapeSlot] = CreateCutter(shapeSlot, s_CsgVisibleReceiverGroups[shapeSlot], cutterPosition, world);
        m_receiverCenters[shapeSlot] = receiverPosition;
    }

    b8 allEntitiesValid = camera.valid() && light.valid();
    if(overlapping){
        // Move only lane Z.
        Float4 receiverPosition;
        StoreFloat(VectorAdd(CsgVisibleShapePosition(CsgVisibleShapeSlot::Plane), VectorSet(0.0f, 0.0f, s_OverlappingReceiverDepthOffset, 0.0f)), receiverPosition);
        const auto receiver = CreateSolidCubeEntity(
            s_CsgVisibleReceiverGroups[CsgVisibleShapeSlot::Plane],
            CsgVisibleShapeColor(CsgVisibleShapeSlot::Plane),
            receiverPosition,
            Float4(s_ReceiverScale, s_ReceiverScale, s_ReceiverScale, 0.0f),
            true,
            material,
            world
        );
        allEntitiesValid = allEntitiesValid && receiver.valid();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: overlapping plane receiver created z_offset=0.125"));
    }
    for(usize shapeSlot = 0u; shapeSlot < s_CsgVisibleShapeCount; ++shapeSlot)
        allEntitiesValid = allEntitiesValid && m_receivers[shapeSlot].valid() && m_cutters[shapeSlot].valid();
    NWB_FATAL_ASSERT_MSG(allEntitiesValid, NWB_TEXT("CsgVisibleSmokeProject failed to create all scene entities"));
}

void CsgVisibleScene::update(const f32 animationTime, Core::ECS::World& world)noexcept{
    using namespace __hidden_csg_visible_scene;
    for(usize shapeSlot = 0u; shapeSlot < s_CsgVisibleShapeCount; ++shapeSlot){
        const f32 rotationPhase = static_cast<f32>(shapeSlot) * 0.18f;
        const SIMDVectorSingle rotation = BuildCubeRotation(animationTime, rotationPhase);
        ApplyCubeRotation(m_receivers[shapeSlot], rotation, world);
        ApplyCutterTransform(m_cutters[shapeSlot], LoadFloat(m_receiverCenters[shapeSlot]), rotation, CsgVisibleCutterLocalOffset(shapeSlot), world);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

