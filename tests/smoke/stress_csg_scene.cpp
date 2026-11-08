// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stress_csg_scene.h"

#include "csg_smoke_helpers.h"
#include "smoke_environment.h"
#include "smoke_skinned_scene_helpers.h"

#include <tests/smoke/presentation_types.adhd.h>

#include <impl/assets_csg/shape_id.adhd.h>
#include <impl/ecs_csg/module.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_stress_csg_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<Name, 2> s_Groups = { Name(NWB::Tests::Smoke::s_SmokeNameStressCsgTransparent), Name(NWB::Tests::Smoke::s_SmokeNameStressCsgOpaque) };
using NWB::Tests::Smoke::s_SmokeNameMesh;
static constexpr usize s_BodyCount = 20u;
static constexpr Float4 s_CutterHalfExtents(4.5f, 0.08f, 0.65f, 0.0f);
static constexpr f32 s_CutterHeight = 0.9f;
static constexpr f32 s_CutterAmplitude = 0.1f;
static constexpr Array<f32, 2> s_RowDepth = { -0.55f, 0.55f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 StressCsgScene::initialize(const Span<const Core::ECS::EntityID> owners, const b8 opaqueOnly, Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena){
    const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_STRESS_CSG_PROFILE");
    if(!value || AStringView(value->data(), value->size()) == "none")
        return true;
    if(AStringView(value->data(), value->size()) != "waist_bands"){
        NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: CSG profile must be none or waist_bands"));
        return false;
    }
    const usize ownerCount = owners.size();
    if(ownerCount != __hidden_stress_csg_scene::s_BodyCount || opaqueOnly){
        NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: waist_bands requires twenty bodies with ten per material class"));
        return false;
    }
    Array<Core::ECS::EntityID, __hidden_stress_csg_scene::s_BodyCount> meshes = {};
    for(usize index = 0u; index < ownerCount; ++index){
        meshes[index] = FindSpawnedModelObject(owners[index], __hidden_stress_csg_scene::s_SmokeNameMesh, Impl::ModelObjectKind::SkinnedMesh, world);
        if(!meshes[index].valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: CSG receiver mesh child missing for body {}"), index);
            return false;
        }
    }
    for(usize index = 0u; index < ownerCount; ++index){
        const usize row = index % m_cutters.size();
        AddSkinnedCsgMeshReceiver(meshes[index], __hidden_stress_csg_scene::s_Groups[row], row == 1u, row == 0u, world);
    }
    for(usize row = 0u; row < m_cutters.size(); ++row){
        auto entity = world.createEntity();
        auto& cutter = entity.addComponent<Impl::CsgCutterComponent>();
        cutter.receiverGroup = __hidden_stress_csg_scene::s_Groups[row];
        cutter.shapeType = Name(NWB::Impl::s_CsgBoxShapeName);
        Impl::CsgBoxShapeParameters parameters;
        parameters.halfExtents = __hidden_stress_csg_scene::s_CutterHalfExtents;
        AssignCsgCutterParameters(parameters, cutter);
        m_cutters[row] = entity.id();
    }
    m_enabled = true;
    if(!update(0.0f, world))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: CSG profile=waist_bands receivers={} transparent={} opaque={} cutters={}") NWB_TEXT(" half_x={} half_y={} half_z={} center_y={} amplitude_y={} front_z={} back_z={} motion=crowd_yaw")
        , ownerCount
        , ownerCount / m_cutters.size()
        , ownerCount / m_cutters.size()
        , m_cutters.size()
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

b8 StressCsgScene::update(const f32 yaw, Core::ECS::World& world){
    if(!m_enabled)
        return true;
    if(!IsFinite(yaw)){
        NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: nonfinite CSG cutter yaw"));
        return false;
    }
    const SIMDVectorSingle phases = VectorAdd(VectorReplicate(yaw), VectorSet(0.0f, s_PI, 0.0f, 0.0f));
    const SIMDVectorSingle heights = VectorMultiplyAddExpression(VectorSin(phases), VectorReplicate(__hidden_stress_csg_scene::s_CutterAmplitude), VectorReplicate(__hidden_stress_csg_scene::s_CutterHeight));
    for(usize row = 0u; row < m_cutters.size(); ++row){
        auto* cutter = world.tryGetComponent<Impl::CsgCutterComponent>(m_cutters[row]);
        if(!cutter){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: CSG cutter disappeared"));
            return false;
        }
        const f32 height = row == 0u ? VectorGetX(heights) : VectorGetY(heights);
        AssignCsgCutterTransform(VectorSet(0.0f, height, __hidden_stress_csg_scene::s_RowDepth[row], 0.0f), QuaternionIdentity(), *cutter);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

