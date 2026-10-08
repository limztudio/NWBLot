// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stress_csg_scene.h"

#include "csg_smoke_helpers.h"
#include "smoke_environment.h"
#include "smoke_skinned_scene_helpers.h"

#include <impl/ecs_csg/module.h>

#include <core/common/log.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_stress_csg_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Name s_Groups[] = { Name("stress/csg/transparent"), Name("stress/csg/opaque") };
static constexpr Name s_MeshObject("mesh");
static constexpr usize s_BodyCount = 20u;
static constexpr Float4 s_CutterHalfExtents(4.5f, 0.08f, 0.65f, 0.0f);
static constexpr f32 s_CutterHeight = 0.9f;
static constexpr f32 s_CutterAmplitude = 0.1f;
static constexpr f32 s_RowDepth[] = { -0.55f, 0.55f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool StressCsgScene::initialize(
    Core::ECS::World& world,
    Core::Alloc::GlobalArena& arena,
    const Core::ECS::EntityID* const owners,
    const usize ownerCount,
    const bool opaqueOnly
){
    const auto value = ReadSmokeEnvironmentText(arena, "NWB_STRESS_CSG_PROFILE");
    if(!value || AStringView(value->data(), value->size()) == "none")
        return true;
    if(AStringView(value->data(), value->size()) != "waist_bands"){
        NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: CSG profile must be none or waist_bands"));
        return false;
    }
    if(!owners || ownerCount != __hidden_stress_csg_scene::s_BodyCount || opaqueOnly){
        NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: waist_bands requires twenty bodies with ten per material class"));
        return false;
    }
    Core::ECS::EntityID meshes[__hidden_stress_csg_scene::s_BodyCount] = {};
    for(usize index = 0u; index < ownerCount; ++index){
        meshes[index] = FindSpawnedModelObject(
            world, owners[index], __hidden_stress_csg_scene::s_MeshObject, Impl::ModelObjectKind::SkinnedMesh
        );
        if(!meshes[index].valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: CSG receiver mesh child missing for body {}"), index);
            return false;
        }
    }
    for(usize index = 0u; index < ownerCount; ++index){
        const usize row = index % LengthOf(m_cutters);
        AddSkinnedCsgMeshReceiver(world, meshes[index], __hidden_stress_csg_scene::s_Groups[row], row == 1u, row == 0u);
    }
    for(usize row = 0u; row < LengthOf(m_cutters); ++row){
        auto entity = world.createEntity();
        auto& cutter = entity.addComponent<Impl::CsgCutterComponent>(arena);
        cutter.receiverGroup = __hidden_stress_csg_scene::s_Groups[row];
        cutter.shapeType = Name("engine/csg/box");
        Impl::CsgBoxShapeParameters parameters;
        parameters.halfExtents = __hidden_stress_csg_scene::s_CutterHalfExtents;
        AssignCsgCutterParameters(cutter, parameters);
        m_cutters[row] = entity.id();
    }
    m_enabled = true;
    if(!update(world, 0.0f))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: CSG profile=waist_bands receivers={} transparent={} opaque={} cutters={}")
        NWB_TEXT(" half_x={} half_y={} half_z={} center_y={} amplitude_y={} front_z={} back_z={} motion=crowd_yaw")
        , ownerCount
        , ownerCount / LengthOf(m_cutters)
        , ownerCount / LengthOf(m_cutters)
        , LengthOf(m_cutters)
        , __hidden_stress_csg_scene::s_CutterHalfExtents.x
        , __hidden_stress_csg_scene::s_CutterHalfExtents.y
        , __hidden_stress_csg_scene::s_CutterHalfExtents.z
        , __hidden_stress_csg_scene::s_CutterHeight
        , __hidden_stress_csg_scene::s_CutterAmplitude
        , __hidden_stress_csg_scene::s_RowDepth[0]
        , __hidden_stress_csg_scene::s_RowDepth[1]
    );
    return true;
}

bool StressCsgScene::update(Core::ECS::World& world, const f32 yaw){
    if(!m_enabled)
        return true;
    if(!IsFinite(yaw)){
        NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: nonfinite CSG cutter yaw"));
        return false;
    }
    for(usize row = 0u; row < LengthOf(m_cutters); ++row){
        auto* cutter = world.tryGetComponent<Impl::CsgCutterComponent>(m_cutters[row]);
        if(!cutter){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: CSG cutter disappeared"));
            return false;
        }
        const f32 phase = yaw + (row == 0u ? 0.0f : s_PI);
        const f32 height = __hidden_stress_csg_scene::s_CutterHeight + __hidden_stress_csg_scene::s_CutterAmplitude * Sin(phase);
        AssignCsgCutterTransform(*cutter, VectorSet(0.0f, height, __hidden_stress_csg_scene::s_RowDepth[row], 0.0f), QuaternionIdentity());
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

