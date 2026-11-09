// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "csg_smoke_helpers.h"
#include "smoke_scene_helpers.h"

#include <tests/smoke/scene_types.adhd.h>

#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>

#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_visible_scene{

using NWB::Tests::Smoke::AddStaticCsgMeshReceiver;
using NWB::Tests::Smoke::AssignCsgCutterParameters;
using NWB::Tests::Smoke::AssignCsgCutterTransform;
using NWB::Tests::Smoke::BuildCsgVisibleCubeRotation;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CsgVisibleCutterLocalOffset;
using NWB::Tests::Smoke::CsgVisibleShapeColor;
using NWB::Tests::Smoke::CsgVisibleShapePosition;
namespace CsgVisibleShapeSlot = NWB::Tests::Smoke::CsgVisibleShapeSlot;
using NWB::Tests::Smoke::s_SmokeSurfaceMaterialInterface;
using CsgVisibleMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using CsgVisibleMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;

inline constexpr f32 s_ReceiverScale = NWB::Tests::Smoke::s_CsgVisibleReceiverScale;
inline constexpr f32 s_CutterScale = NWB::Tests::Smoke::s_CsgVisibleCutterScale;
inline constexpr usize s_CsgVisibleShapeCount = NWB::Tests::Smoke::s_CsgVisibleShapeCount;
inline constexpr CsgVisibleMeshRef s_CubeMesh{"project/meshes/cube_hard_edges"};
inline constexpr f32 s_OverlappingReceiverDepthOffset = NWB::Tests::Smoke::s_CsgVisibleOverlappingReceiverDepthOffset;

static_assert(static_cast<usize>(CsgVisibleShapeSlot::kCount) == s_CsgVisibleShapeCount, "CSG visible shape table size must stay in sync");

inline constexpr Array<Name, s_CsgVisibleShapeCount> s_CsgVisibleReceiverGroups = {
    Name(NWB::Tests::Smoke::s_SmokeSceneNameProjectSmokeCsgVisiblePlaneReceiver),
    Name(NWB::Tests::Smoke::s_SmokeSceneNameProjectSmokeCsgVisibleBoxReceiver),
    Name(NWB::Tests::Smoke::s_SmokeSceneNameProjectSmokeCsgVisibleSphereReceiver),
    Name(NWB::Tests::Smoke::s_SmokeSceneNameProjectSmokeCsgVisibleCapsuleReceiver),
};

[[nodiscard]] inline NWB::Core::ECS::EntityID CreateSolidCubeEntity(const Name receiverGroup, const Float4& colorTint, const Float4& position, const Float4& scale, const b8 csgReceiver, const CsgVisibleMaterialRef& material, NWB::Core::ECS::World& world){
    const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(s_CubeMesh, material, s_SmokeSurfaceMaterialInterface, colorTint, position, scale, world);
    if(!entity.valid())
        return NWB::Core::ECS::s_InvalidEntityId;

    if(csgReceiver)
        AddStaticCsgMeshReceiver(entity, receiverGroup, true, false, world);

    return entity;
}

inline void ApplyCubeRotation(const NWB::Core::ECS::EntityID entity, const SIMDVectorSingle rotation, NWB::Core::ECS::World& world)noexcept{
    auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
    if(!transform)
        return;

    StoreFloat(rotation, transform->rotation);
}

inline void ApplyCutterTransform(const NWB::Core::ECS::EntityID cutterEntity, const SIMDVectorSingle receiverCenter, const SIMDVectorSingle receiverRotation, const SIMDVectorSingle cutterLocalOffset, NWB::Core::ECS::World& world)noexcept{
    auto* cutter = world.tryGetComponent<NWB::Impl::CsgCutterComponent>(cutterEntity);
    if(!cutter)
        return;

    const SIMDVectorSingle cutterCenter = VectorAdd(receiverCenter, Vector3Rotate(cutterLocalOffset, receiverRotation));
    AssignCsgCutterTransform(cutterCenter, receiverRotation, *cutter);
}

[[nodiscard]] inline NWB::Core::ECS::EntityID CreateCutter(const usize shapeSlot, const Name receiverGroup, const SIMDVectorSingle center, NWB::Core::ECS::World& world){
    auto cutterEntity = world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
    cutter.receiverGroup = receiverGroup;
    AssignCsgCutterTransform(center, QuaternionIdentity(), cutter);

    switch(shapeSlot){
    case static_cast<usize>(CsgVisibleShapeSlot::Plane):{
        cutter.shapeType = Name(NWB::Impl::s_CsgPlaneShapeName);
        NWB::Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(0.0f, 0.0f, 1.0f, 0.0f);
        AssignCsgCutterParameters(parameters, cutter);
        break;
    }
    case static_cast<usize>(CsgVisibleShapeSlot::Box):{
        cutter.shapeType = Name(NWB::Impl::s_CsgBoxShapeName);
        NWB::Impl::CsgBoxShapeParameters parameters;
        parameters.halfExtents = Float4(0.30f * s_CutterScale, 0.30f * s_CutterScale, 0.56f * s_CutterScale, 0.0f);
        AssignCsgCutterParameters(parameters, cutter);
        break;
    }
    case static_cast<usize>(CsgVisibleShapeSlot::Sphere):{
        cutter.shapeType = Name(NWB::Impl::s_CsgSphereShapeName);
        NWB::Impl::CsgSphereShapeParameters parameters;
        parameters.radius = Float4(0.34f * s_CutterScale, 0.0f, 0.0f, 0.0f);
        AssignCsgCutterParameters(parameters, cutter);
        break;
    }
    case static_cast<usize>(CsgVisibleShapeSlot::Capsule):{
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


class CsgVisibleScene final{
public:
    static constexpr f32 s_CameraTargetY = s_CsgVisibleCameraTargetY;
    static constexpr usize s_ShapeCount = s_CsgVisibleShapeCount;


public:
    void create(const SmokeMaterialRef& material, const b8 overlapping, const Core::ECS::EntityID camera, const Core::ECS::EntityID light, Core::ECS::World& world){
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
            StoreFloat(VectorAdd(CsgVisibleShapePosition(static_cast<usize>(CsgVisibleShapeSlot::Plane)), VectorSet(0.0f, 0.0f, s_OverlappingReceiverDepthOffset, 0.0f)), receiverPosition);
            const auto receiver = CreateSolidCubeEntity(
                s_CsgVisibleReceiverGroups[static_cast<usize>(CsgVisibleShapeSlot::Plane)],
                CsgVisibleShapeColor(static_cast<usize>(CsgVisibleShapeSlot::Plane)),
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

    void update(const f32 animationTime, Core::ECS::World& world)noexcept{
        using namespace __hidden_csg_visible_scene;
        for(usize shapeSlot = 0u; shapeSlot < s_CsgVisibleShapeCount; ++shapeSlot){
            const f32 rotationPhase = static_cast<f32>(shapeSlot) * 0.18f;
            const SIMDVectorSingle rotation = BuildCsgVisibleCubeRotation(animationTime, rotationPhase);
            ApplyCubeRotation(m_receivers[shapeSlot], rotation, world);
            ApplyCutterTransform(m_cutters[shapeSlot], LoadFloat(m_receiverCenters[shapeSlot]), rotation, CsgVisibleCutterLocalOffset(shapeSlot), world);
        }
    }


private:
    Array<Float4, s_ShapeCount> m_receiverCenters{};
    Array<Core::ECS::EntityID, s_ShapeCount> m_receivers{};
    Array<Core::ECS::EntityID, s_ShapeCount> m_cutters{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
