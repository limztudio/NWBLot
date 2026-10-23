// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_smoke_helpers.h"
#include "framebuffer_capture.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"

#if defined(NWB_CSG_CAUSTIC_SMOKE)
#include "csg_caustic_timing_probe.h"
#endif

#include <loader/project_entry.h>

#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_render/module.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/frame.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_shadow_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ProjectName = NWB_TEXT("CsgShadowSmokeProject");
constexpr AStringView s_AnalyticAtlas = "analytic";
constexpr AStringView s_OpenAtlas = "open";
constexpr AStringView s_PlaneShapePath = "engine/csg/plane";
constexpr AStringView s_CapsuleShapePath = "engine/csg/capsule";

static constexpr AStringView s_EngineCsgSphere = "engine/csg/sphere";
static constexpr AStringView s_Directional = "directional";
static constexpr AStringView s_Point = "point";
static constexpr AStringView s_Hard = "hard";
static constexpr AStringView s_Finite = "finite";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::Smoke;
using EntityID = NWB::Core::ECS::EntityID;

static constexpr SmokeMeshRef s_CubeMesh{"project/meshes/cube_hard_edges"};
static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
#if defined(NWB_CSG_CAUSTIC_SMOKE)
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/csg_caustic/materials/receiver"};
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/csg_caustic/materials/glass"};
#else
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/csg_shadow/materials/opaque"};
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/csg_shadow/materials/glass"};
#endif
static constexpr AStringView s_MaterialInterface = "project/shaders/csg_shadow";
static constexpr SmokeMaterialRef s_OpenMaterial{"project/smoke/reflection/materials/optical_volume"};
static constexpr AStringView s_OpenInterface = "project/shaders/reflection_optical";
static constexpr Name s_Groups[] = { Name("csg_shadow/opaque_hole"), Name("csg_shadow/opaque_cap"), Name("csg_shadow/glass_depth"), Name("csg_shadow/glass_hole"), Name("csg_shadow/overlap_a"), Name("csg_shadow/overlap_b"), Name("csg_shadow/glass_control"), };
static constexpr Name s_AnalyticGroups[] = {
    Name("csg_shadow/analytic_plane"), Name("csg_shadow/analytic_sphere"), Name("csg_shadow/analytic_capsule"),
    Name("csg_shadow/analytic_axial_capsule"), Name("csg_shadow/analytic_union"), Name("csg_shadow/analytic_control"),
};
static constexpr AStringView s_OpenCaseNames[] = { "reference", "retained", "clipped_reference", "clipped", "boundary64", "terminal65", };
static constexpr Name s_OpenGroups[] = { Name("csg_shadow/open_five"), Name("csg_shadow/open_six") };
static constexpr SmokeMeshRef s_OpenPairMesh{"project/meshes/csg_shadow_open_2"};
static constexpr SmokeMeshRef s_OpenFiveMesh{"project/meshes/csg_shadow_open_5"};
static constexpr SmokeMeshRef s_OpenSixMesh{"project/meshes/csg_shadow_open_6"};
static constexpr SmokeMeshRef s_OpenBoundaryMesh{"project/meshes/csg_shadow_open_64"};
static constexpr SmokeMeshRef s_OpenOverflowMesh{"project/meshes/csg_shadow_open_65"};
static constexpr f32 s_CameraDepth = -4.0f;
static constexpr u32 s_MoveUpdate = 40u;

namespace Arm{
    enum Enum : u8{ Reference, Cut, Uncut, Moved, CameraShift };
};

namespace OpenCase{
    enum Enum : u8{ Reference, Retained, ClippedReference, Clipped, Boundary64, Terminal65 };
};

struct Box{
    Float3U minimum;
    Float3U maximum;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Box StoreBoxBounds(const SIMDVectorSingle minimum, const SIMDVectorSingle maximum)noexcept{
    Box box;
    StoreFloat(minimum, box.minimum);
    StoreFloat(maximum, box.maximum);
    return box;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgShadowSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorld(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, s_ProjectName);
        auto& renderer = AddSmokeRenderSystems(*world, context);
        NWB::Impl::ReflectionSettings reflection;
        reflection.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
        NWB_FATAL_ASSERT_MSG(renderer.setReflectionSettings(reflection), NWB_TEXT("Invalid CSG shadow reflection settings"));
        renderer.setRefractionEnabled(false);
        NWB::Impl::PresentationSettings presentation;
        presentation.toneMap = NWB::Impl::PresentationToneMap::LinearClamp;
        NWB_FATAL_ASSERT_MSG(renderer.setPresentationSettings(presentation), NWB_TEXT("Invalid CSG shadow presentation settings"));
        return world;
    }


private:
    bool readMode(){
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
        if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_ARM")){
            const AStringView arm(value->data(), value->size());
            if(arm == "reference")
                m_arm = Arm::Reference;
            else if(arm == "cut")
                m_arm = Arm::Cut;
            else if(arm == "uncut")
                m_arm = Arm::Uncut;
            else if(arm == "moved")
                m_arm = Arm::Moved;
            else if(arm == "camera_shift")
                m_arm = Arm::CameraShift;
            else
                return false;
        }
        if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_LIGHT")){
            const AStringView light(value->data(), value->size());
            if(light != s_Directional && light != s_Point)
                return false;
            m_pointLight = light == s_Point;
        }
        if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_LIGHT_SOURCE")){
            const AStringView source(value->data(), value->size());
            if(source != s_Hard && source != s_Finite)
                return false;
            m_finiteLightSource = source == s_Finite;
        }
        if(const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_ATLAS")){
            const AStringView atlas(value->data(), value->size());
            if(atlas != "boxes" && atlas != s_AnalyticAtlas && atlas != s_OpenAtlas)
                return false;
            m_analyticAtlas = atlas == s_AnalyticAtlas;
            m_openAtlas = atlas == s_OpenAtlas;
        }
        if(m_openAtlas){
            const auto value = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_SHADOW_OPEN_CASE");
            if(!value || m_pointLight || m_finiteLightSource || m_arm != Arm::Cut)
                return false;
            const AStringView selected(value->data(), value->size());
            bool found = false;
            for(u32 index = 0u; index < LengthOf(s_OpenCaseNames); ++index){
                if(selected == s_OpenCaseNames[index]){
                    m_openCase = static_cast<OpenCase::Enum>(index);
                    found = true;
                    break;
                }
            }
            if(!found)
                return false;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
            return false;
#endif
        }
        return !m_analyticAtlas || (!m_pointLight && (m_arm == Arm::Cut || m_arm == Arm::Uncut));
    }

    EntityID createBox(const Box& box, const bool transparent, const SmokeMeshRef& mesh = s_CubeMesh){
        const SIMDVectorSingle minimum = LoadFloat(box.minimum);
        const SIMDVectorSingle maximum = LoadFloat(box.maximum);
        Float4 center;
        Float4 extent;
        StoreFloat(VectorScale(VectorAdd(minimum, maximum), 0.5f), center);
        StoreFloat(VectorSubtract(maximum, minimum), extent);
        const auto entity = CreateTintedStaticMeshEntity(
            *m_world, m_context.objectArena, mesh, transparent ? (m_openAtlas ? s_OpenMaterial : s_GlassMaterial) : s_OpaqueMaterial,
            transparent && m_openAtlas ? NameText<char>{s_OpenInterface} : NameText<char>{s_MaterialInterface}, Float4(1.0f, 1.0f, 1.0f, 1.0f),
            center, extent
        );
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("CsgShadowSmokeProject: box creation failed"));
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        if(transparent)
            m_world->entity(entity).getComponent<NWB::Impl::RendererComponent>().opticalBoundaryMode = NWB::Impl::OpticalBoundaryMode::ClosedNested;
#endif
        if(transparent && m_openAtlas){
            const Half packedIor = ConvertFloatToHalf(1.5f);
            const Half4U packedTransmission = MakeHalf4U(0.25f, 0.5f, 0.75f, 0.0f);
            NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
                *m_world, entity, Name(s_OpenInterface), "runtime.ior", NWB::Impl::MaterialLayoutFieldType::Half,
                NWB::Impl::PackMaterialInstanceBytes(&packedIor, sizeof(packedIor))
            ), NWB_TEXT("CsgShadowSmokeProject: open-sheet IOR override failed")
            );
            NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
                *m_world, entity, Name(s_OpenInterface), "runtime.unit_transmission", NWB::Impl::MaterialLayoutFieldType::Half3,
                NWB::Impl::PackMaterialInstanceBytes(packedTransmission.raw, sizeof(Half) * 3u)
            ), NWB_TEXT("CsgShadowSmokeProject: open-sheet transmission override failed")
            );
        }
        return entity;
    }

    void createReferencePieces(const Box& box, const Box& cutter, const bool transparent){
        Float3U lower;
        Float3U upper;
        StoreFloat(VectorMax(LoadFloat(box.minimum), LoadFloat(cutter.minimum)), lower);
        StoreFloat(VectorMin(LoadFloat(box.maximum), LoadFloat(cutter.maximum)), upper);
        if(lower.x >= upper.x || lower.y >= upper.y || lower.z >= upper.z){
            createBox(box, transparent);
            return;
        }
        // Six disjoint ordinary boxes represent the subtraction without CSG components or generated caps.
        const Box pieces[] = {
            { box.minimum, Float3U(lower.x, box.maximum.y, box.maximum.z) },
            { Float3U(upper.x, box.minimum.y, box.minimum.z), box.maximum },
            { Float3U(lower.x, box.minimum.y, box.minimum.z), Float3U(upper.x, lower.y, box.maximum.z) },
            { Float3U(lower.x, upper.y, box.minimum.z), Float3U(upper.x, box.maximum.y, box.maximum.z) },
            { Float3U(lower.x, lower.y, box.minimum.z), Float3U(upper.x, upper.y, lower.z) },
            { Float3U(lower.x, lower.y, upper.z), Float3U(upper.x, upper.y, box.maximum.z) },
        };
        for(const auto& piece : pieces){
            if(piece.minimum.x < piece.maximum.x && piece.minimum.y < piece.maximum.y && piece.minimum.z < piece.maximum.z)
                createBox(piece, transparent);
        }
    }

    void addCaster(const Box& box, const Box& cutterBox, const u32 groupIndex, const bool transparent, const bool moving){
        if(m_arm == Arm::Reference){
            createReferencePieces(box, cutterBox, transparent);
            return;
        }
        const auto entity = createBox(box, transparent);
        AddStaticCsgMeshReceiver(*m_world, entity, s_Groups[groupIndex], !transparent, transparent);
        auto cutterEntity = m_world->createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = s_Groups[groupIndex];
        cutter.shapeType = Name("engine/csg/box");
        cutter.active = m_arm != Arm::Uncut;
        NWB::Impl::CsgBoxShapeParameters parameters;
        const SIMDVectorSingle minimum = LoadFloat(cutterBox.minimum);
        const SIMDVectorSingle maximum = LoadFloat(cutterBox.maximum);
        StoreFloat(VectorScale(VectorSubtract(maximum, minimum), 0.5f), parameters.halfExtents);
        AssignCsgCutterParameters(cutter, parameters);
        const SIMDVectorSingle finalCenter = VectorScale(VectorAdd(minimum, maximum), 0.5f);
        SIMDVectorSingle initialCenter = finalCenter;
        if(moving && m_arm == Arm::Moved)
            initialCenter = VectorSubtract(initialCenter, VectorScale(VectorSet(0.6f, 0.0f, 0.0f, 0.0f), m_pointLight ? 0.5f : 1.0f));
        AssignCsgCutterTransform(cutter, initialCenter, QuaternionIdentity());
        if(moving){
            NWB_ASSERT(m_movingCount < LengthOf(m_movingCutters));
            m_movingCutters[m_movingCount] = cutterEntity.id();
            StoreFloat(finalCenter, m_finalCutterCenters[m_movingCount]);
            ++m_movingCount;
        }
    }

    void createAtlas(){
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        const Box split{ Float3U(-2.0f, -0.7f, -7.0f), Float3U(-0.6f, 0.7f, -5.0f) };
        const Box cavity{ Float3U(-2.2f, -0.9f, -6.5f), Float3U(-0.4f, 0.9f, -5.5f) };
        if(m_arm == Arm::Reference)
            createBox(split, true, SmokeMeshRef{"project/meshes/csg_caustic_split"});
        else
            addCaster(split, cavity, 2u, true, false);
        const Box control{ Float3U(0.6f, -0.7f, -7.0f), Float3U(2.0f, 0.7f, -5.0f) };
        createBox(control, true);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticSmokeProject: split cavity has four interfaces before the receiver; control has two"));
#else
        const f32 xyScale = m_pointLight ? 0.5f : 1.0f;
        for(u32 slot = 0u; slot < 6u; ++slot){
            const SIMDVectorSingle grid = VectorSubtract(VectorSet(static_cast<f32>(slot % 3u), slot < 3u ? 1.0f : -1.0f, 0.0f, 0.0f), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
            const SIMDVectorSingle center = VectorScale(VectorMultiply(grid, VectorSet(2.0f, 1.0f, 0.0f, 0.0f)), xyScale);
            const SIMDVectorSingle halfExtent = VectorScale(VectorSet(0.7f, 0.7f, 0.0f, 0.0f), xyScale);
            const SIMDVectorSingle boxMin = VectorSetZ(VectorSubtract(center, halfExtent), -7.0f);
            const SIMDVectorSingle boxMax = VectorSetZ(VectorAdd(center, halfExtent), -5.0f);
            const bool hole = slot == 0u || slot == 3u;
            const f32 holeHalfWidth = m_pointLight ? 0.36f : 0.24f;
            const SIMDVectorSingle holeOffsets = VectorScale(VectorSet(0.3f, 0.4f, 0.0f, 0.0f), xyScale);
            const SIMDVectorSingle holeCenter = VectorAdd(center, VectorAndInt(holeOffsets, s_SIMDMaskX));
            const SIMDVectorSingle holeExtent = VectorSetX(holeOffsets, holeHalfWidth);
            const SIMDVectorSingle solidExtent = VectorSet(1.0f, 1.0f, 0.0f, 0.0f);
            SIMDVectorSingle cutterMin = hole ? VectorSetZ(VectorSubtract(holeCenter, holeExtent), -7.2f) : VectorSetZ(VectorSubtract(center, solidExtent), -6.0f);
            SIMDVectorSingle cutterMax = hole ? VectorSetZ(VectorAdd(holeCenter, holeExtent), -4.8f) : VectorSetZ(VectorAdd(center, solidExtent), -4.8f);
            if(slot == 5u){
                const SIMDVectorSingle shift = VectorSet(20.0f, 0.0f, 0.0f, 0.0f);
                cutterMin = VectorAdd(cutterMin, shift);
                cutterMax = VectorAdd(cutterMax, shift);
            }
            const u32 groupIndex = slot == 5u ? 6u : slot;
            addCaster(StoreBoxBounds(boxMin, boxMax), StoreBoxBounds(cutterMin, cutterMax), groupIndex, slot >= 2u, hole);
            if(slot == 4u){
                const SIMDVectorSingle shift = VectorSet(0.0f, 0.0f, 0.5f, 0.0f);
                addCaster(StoreBoxBounds(VectorSubtract(boxMin, shift), VectorSubtract(boxMax, shift)), StoreBoxBounds(VectorSubtract(cutterMin, shift), VectorSubtract(cutterMax, shift)), 5u, true, false);
            }
        }
#endif
    }

    template<typename ParameterT>
    void addAnalyticCutter(const u32 groupIndex, const Name shapeType, const ParameterT& parameters, const SIMDMatrix& shapeToWorld){
        auto cutterEntity = m_world->createEntity();
        auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = s_AnalyticGroups[groupIndex];
        cutter.shapeType = shapeType;
        cutter.active = m_arm == Arm::Cut;
        AssignCsgCutterParameters(cutter, parameters);
        SIMDVectorSingle determinant;
        StoreFloat(MatrixInverse(&determinant, shapeToWorld), cutter.worldToShape);
        StoreFloat(shapeToWorld, cutter.shapeToWorld);
    }

    void createAnalyticAtlas(){
        for(u32 slot = 0u; slot < 6u; ++slot){
            const SIMDVectorSingle grid = VectorSubtract(VectorSet(static_cast<f32>(slot % 3u), slot < 3u ? 1.0f : -1.0f, 0.0f, 0.0f), VectorSet(1.0f, 0.0f, 0.0f, 0.0f));
            const SIMDVectorSingle center = VectorMultiply(grid, VectorSet(2.0f, 1.0f, 0.0f, 0.0f));
            const SIMDVectorSingle halfExtent = VectorSet(0.7f, 0.7f, 0.0f, 0.0f);
            const Box box = StoreBoxBounds(VectorSetZ(VectorSubtract(center, halfExtent), -7.0f), VectorSetZ(VectorAdd(center, halfExtent), -5.0f));
            const auto entity = createBox(box, true);
            AddStaticCsgMeshReceiver(*m_world, entity, s_AnalyticGroups[slot], false, true);
        }

        NWB::Impl::CsgPlaneShapeParameters plane;
        plane.normalDistance = Float4(0.5f, 0.0f, 1.0f, 0.0f);
        addAnalyticCutter(0u, Name(s_PlaneShapePath), plane, MatrixTranslation(-2.0f, 1.0f, -6.0f));

        NWB::Impl::CsgSphereShapeParameters sphere;
        sphere.radius = Float4(1.0f, 0.0f, 0.0f, 0.0f);
        const SIMDMatrix ellipsoid = MatrixAffineTransformation(VectorSet(0.5f, 0.45f, 0.75f, 0.0f), VectorZero(), QuaternionIdentity(), VectorSet(0.0f, 1.0f, -6.0f, 0.0f));
        addAnalyticCutter(1u, Name(s_EngineCsgSphere), sphere, ellipsoid);

        NWB::Impl::CsgCapsuleShapeParameters capsule;
        capsule.radiusHalfHeight = Float4(0.35f, 0.25f, 0.0f, 0.0f);
        addAnalyticCutter(2u, Name(s_CapsuleShapePath), capsule, MatrixTranslation(2.0f, 1.0f, -6.0f));
        capsule.radiusHalfHeight = Float4(0.3f, 0.4f, 0.0f, 0.0f);
        // An exact axis permutation exercises the capsule's zero radial-speed branch.
        const SIMDMatrix alongLight = MatrixSet(1.0f, 0.0f, 0.0f, -2.0f, 0.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f, 0.0f, -6.0f, 0.0f, 0.0f, 0.0f, 1.0f);
        addAnalyticCutter(3u, Name(s_CapsuleShapePath), capsule, alongLight);

        sphere.radius = Float4(0.55f, 0.0f, 0.0f, 0.0f);
        addAnalyticCutter(4u, Name(s_EngineCsgSphere), sphere, MatrixTranslation(0.0f, -1.0f, -6.25f));
        addAnalyticCutter(4u, Name(s_EngineCsgSphere), sphere, MatrixTranslation(0.0f, -1.0f, -5.75f));
        // The infinite plane keeps the control in the CSG route while removing only z <= -8, outside its box.
        plane.normalDistance = Float4(0.0f, 0.0f, 1.0f, 8.0f);
        addAnalyticCutter(5u, Name(s_PlaneShapePath), plane, MatrixIdentity());
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: analytic atlas tiles=6 shapes=plane,sphere,capsule"));
    }

    bool createOpenAtlas(){
        const bool clipped = m_openCase == OpenCase::Clipped || m_openCase == OpenCase::ClippedReference;
        if(m_openCase == OpenCase::Reference){
            if(!createBox({ Float3U(-1.9f, -0.7f, -7.0f), Float3U(-0.5f, 0.7f, -5.0f) }, true, s_OpenFiveMesh).valid())
                return false;
            // Separate ordinary two-sheet instances give an independent exact even-pair reference beyond the ordinary four-event approximation.
            for(u32 pair = 0u; pair < 3u; ++pair){
                const SIMDVectorSingle lower = VectorAdd(VectorReplicate(-7.0f), VectorScale(VectorReplicate(static_cast<f32>(pair)), 0.8f));
                const SIMDVectorSingle depths = VectorAdd(lower, VectorSet(0.0f, 0.4f, 0.0f, 0.0f));
                if(!createBox(StoreBoxBounds(VectorPermute<0, 1, 4, 3>(VectorSet(0.5f, -0.7f, 0.0f, 0.0f), depths), VectorPermute<0, 1, 5, 3>(VectorSet(1.9f, 0.7f, 0.0f, 0.0f), depths)), true, s_OpenPairMesh).valid())
                    return false;
            }
        }
        else if(m_openCase == OpenCase::ClippedReference){
            for(u32 pair = 0u; pair < 2u; ++pair){
                const SIMDVectorSingle lower = VectorAdd(VectorReplicate(-6.5f), VectorReplicate(static_cast<f32>(pair)));
                const SIMDVectorSingle depths = VectorAdd(lower, VectorSet(0.0f, 0.5f, 0.0f, 0.0f));
                if(!createBox(StoreBoxBounds(VectorPermute<0, 1, 4, 3>(VectorSet(-1.9f, -0.7f, 0.0f, 0.0f), depths), VectorPermute<0, 1, 5, 3>(VectorSet(-0.5f, 0.7f, 0.0f, 0.0f), depths)), true, s_OpenPairMesh).valid())
                    return false;
            }
            if(!createBox({ Float3U(0.5f, -0.7f, -6.6f), Float3U(1.9f, 0.7f, -5.0f) }, true, s_OpenFiveMesh).valid())
                return false;
        }
        else{
            const SmokeMeshRef firstMesh = m_openCase == OpenCase::Boundary64 ? s_OpenBoundaryMesh : m_openCase == OpenCase::Terminal65 ? s_OpenOverflowMesh : s_OpenFiveMesh;
            const SmokeMeshRef meshes[] = { firstMesh, s_OpenSixMesh };
            for(u32 tile = 0u; tile < 2u; ++tile){
                const f32 x = tile == 0u ? -1.2f : 1.2f;
                const SIMDVectorSingle center = VectorSet(x, 0.0f, 0.0f, 0.0f);
                const SIMDVectorSingle halfExtent = VectorSet(0.7f, 0.7f, 0.0f, 0.0f);
                const Box bounds = StoreBoxBounds(VectorSetZ(VectorSubtract(center, halfExtent), -7.0f), VectorSetZ(VectorAdd(center, halfExtent), -5.0f));
                const auto receiver = createBox(bounds, true, meshes[tile]);
                AddStaticCsgMeshReceiver(*m_world, receiver, s_OpenGroups[tile], false, true);
                auto entity = m_world->createEntity();
                if(!entity.id().valid())
                    return false;
                auto& cutter = entity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
                cutter.receiverGroup = s_OpenGroups[tile];
                cutter.shapeType = Name(s_PlaneShapePath);
                NWB::Impl::CsgPlaneShapeParameters plane;
                plane.normalDistance = Float4(0.0f, 0.0f, 1.0f, clipped ? 6.75f : 8.0f);
                AssignCsgCutterParameters(cutter, plane);
                AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
            }
        }
        const u32 firstCount = m_openCase == OpenCase::Boundary64 ? 64u : m_openCase == OpenCase::Terminal65 ? 65u : clipped ? 4u : 5u;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: open_case={} retained_events={},{} receiver_z=0 sheets_z_min=-7 sheets_z_max=-5 ior=1.5 unit_transmission=0.25,0.5,0.75 coverage=1")
            , StringConvert(s_OpenCaseNames[static_cast<u32>(m_openCase)]), firstCount, clipped ? 5u : 6u
        );
        return true;
    }

#if defined(NWB_CSG_CAUSTIC_SMOKE)
    bool configureTiming(NWB::Core::Alloc::ScratchArena& scratchArena){
        const auto mode = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_TIMING_MODE");
        const auto output = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_TIMING_FILE");
        const auto cpuOutput = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_CPU_GPU_FILE");
        const auto start = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_TIMING_START_FILE");
        if(!mode){
            if(output || cpuOutput || start){
                NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: mode and explicit output must agree"));
                return false;
            }
            return true;
        }
        const AStringView modeView(mode->data(), mode->size());
        const auto route = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_TIMING_ROUTE");
        const AStringView routeView = route ? AStringView(route->data(), route->size()) : AStringView{};
        if(
            (modeView != "performance" && modeView != "diagnostic") || (routeView != "hardware" && routeView != "software") || !output || !start || m_capture || !m_context.requestQuit
            || (m_arm != Arm::Reference && m_arm != Arm::Cut) || m_pointLight || m_finiteLightSource || m_analyticAtlas || m_openAtlas || ReadSmokeEnvironmentFlag("NWB_CSG_CAUSTIC_DISABLED")
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: requires uncaptured directional hard-light reference/cut boxes and an explicit route"));
            return false;
        }
        const bool rayTracing = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct);
        const bool rayQuery = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
        const bool hardware = rayTracing && rayQuery;
        if(routeView == "hardware" && !hardware){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticTiming: hardware unavailable rt_accel={} ray_query={}"), rayTracing ? 1u : 0u, rayQuery ? 1u : 0u);
            m_context.requestQuit();
            return true;
        }
        if((routeView == "hardware") != hardware){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: actual route differs from requested acquisition"));
            return false;
        }
        const usize receivers = m_world->view<NWB::Impl::StaticCsgMeshComponent>().candidateCount() + m_world->view<NWB::Impl::SkinnedCsgMeshComponent>().candidateCount();
        const usize cutters = m_world->view<NWB::Impl::CsgCutterComponent>().candidateCount();
        if(receivers != (m_arm == Arm::Cut ? 1u : 0u) || cutters != receivers){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: fixture receiver/cutter identity differs from selected arm"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticTiming: scene arm={} receivers={} cutters={} expected_shader_csg={} camera=960x720 light=directional capture=0")
            , m_arm == Arm::Cut ? NWB_TEXT("cut") : NWB_TEXT("reference"), receivers, cutters, receivers != 0u ? 1u : 0u
        );
        m_timing = MakeUnique<NWB::Tests::Smoke::CsgCausticTimingProbe>(m_context);
        if(!m_timing->initialize(
            modeView == "diagnostic", receivers != 0u, hardware, AStringView(output->data(), output->size())
            , cpuOutput ? AStringView(cpuOutput->data(), cpuOutput->size()) : AStringView{}, AStringView(start->data(), start->size())
        ))
            return false;
        m_context.graphics.addRenderPassToBack(*m_timing);
        m_timingRegistered = true;
        return true;
    }
#endif

    void destroyWorld(){
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        if(m_timingRegistered){
            m_context.graphics.removeRenderPass(*m_timing);
            m_timingRegistered = false;
        }
        m_timing.reset();
#endif
        if(m_capture){
            m_capture->stop();
            m_capture.reset();
        }
        DestroySmokeRenderWorld(m_context, m_world);
    }


public:
    explicit CsgShadowSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorld(context))
    {}
    virtual ~CsgShadowSmokeProject()noexcept override{ destroyWorld(); }


public:
    virtual bool onStartup()override{
        if(!readMode())
            return false;
        const auto camera = CreateSmokeCamera(*m_world, 0.0f, -s_CameraDepth, 0.0f);
        auto* cameraTransform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(camera);
        NWB_FATAL_ASSERT_MSG(cameraTransform, NWB_TEXT("CsgShadowSmokeProject: camera creation failed"));
        const f32 cameraX = m_arm == Arm::CameraShift ? 0.3f : 0.0f;
        cameraTransform->position.x = cameraX;
        const auto receiver = CreateTintedStaticMeshEntity(*m_world, m_context.objectArena, s_PlaneMesh, s_OpaqueMaterial, s_MaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(4.0f, 1.0f, 3.0f, 0.0f));
        auto* receiverTransform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(receiver);
        NWB_FATAL_ASSERT_MSG(camera.valid() && receiverTransform, NWB_TEXT("CsgShadowSmokeProject: camera/receiver creation failed"));
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), receiverTransform->rotation);
        const auto light = m_pointLight
            ? NWB::Impl::Scene::CreatePointLightEntity(*m_world, Float4(0.0f, 0.0f, -12.0f, 0.0f), Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 30.0f)
            : NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.0f, 0.0f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        auto* lightComponent = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(light);
        NWB_FATAL_ASSERT_MSG(lightComponent, NWB_TEXT("CsgShadowSmokeProject: light creation failed"));
        lightComponent->angularRadius = m_finiteLightSource && !m_pointLight ? 0.005f : 0.0f;
        lightComponent->sourceRadius = m_finiteLightSource && m_pointLight ? 0.02f : 0.0f;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        lightComponent->enableCaustics = !ReadSmokeEnvironmentFlag("NWB_CSG_CAUSTIC_DISABLED");
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticSmokeProject: photons {}"), lightComponent->enableCaustics ? NWB_TEXT("enabled") : NWB_TEXT("disabled"));
#else
        lightComponent->enableCaustics = false;
#endif
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: light_source={} angular_radius={:.3f} source_radius={:.3f}")
            , StringConvert(m_finiteLightSource ? s_Finite : s_Hard)
            , static_cast<f64>(lightComponent->angularRadius), static_cast<f64>(lightComponent->sourceRadius)
        );
        if(m_openAtlas){
            if(!createOpenAtlas())
                return false;
        }
        else if(m_analyticAtlas)
            createAnalyticAtlas();
        else
            createAtlas();
        const bool hardware = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery) && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: atlas arm={} light={} hardware={} camera_x={} caster_z_max=-5 receiver_z=0")
            , static_cast<u32>(m_arm), StringConvert(m_pointLight ? s_Point : s_Directional), hardware ? 1u : 0u
            , static_cast<f64>(cameraX)
        );
        constexpr u32 s_DefaultCaptureFrameCount = 120u;
        auto capture = ConfigureSmokeFramebufferCapture(m_context, s_ProjectName, s_DefaultCaptureFrameCount);
        if(!capture)
            return false;
        m_capture = Move(*capture);
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
        if(!configureTiming(scratchArena))
            return false;
#endif
        return true;
    }

    virtual void onShutdown()override{
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        if(m_timing)
            m_timing->finish();
#endif
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        if(m_capture)
            m_capture->update();
        ++m_update;
        if(m_arm == Arm::Moved && m_update == s_MoveUpdate){
            for(u32 index = 0u; index < m_movingCount; ++index){
                auto* cutter = m_world->tryGetComponent<NWB::Impl::CsgCutterComponent>(m_movingCutters[index]);
                NWB_ASSERT(cutter);
                AssignCsgCutterTransform(*cutter, LoadFloat(m_finalCutterCenters[index]), QuaternionIdentity());
            }
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: cutters moved at update40"));
        }
        const f32 fixedDelta = RendererBaselineFixedDelta();
        const auto updated = m_world->tick(fixedDelta > 0.0f ? fixedDelta : delta);
        if(!updated){
            NWB_LOGGER_ERROR(NWB_TEXT("Project: world update rejected CPU work: {}"), static_cast<u32>(updated.error()));
            return false;
        }
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        if(m_timing && !m_timing->update())
            return false;
#endif
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    UniquePtr<FramebufferCapture> m_capture;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
    UniquePtr<NWB::Tests::Smoke::CsgCausticTimingProbe> m_timing;
    bool m_timingRegistered = false;
#endif
    EntityID m_movingCutters[2] = {};
    Float4 m_finalCutterCenters[2] = {};
    u32 m_movingCount = 0u;
    u32 m_update = 0u;
    Arm::Enum m_arm = Arm::Cut;
    bool m_pointLight = false;
    bool m_finiteLightSource = false;
    bool m_analyticAtlas = false;
    bool m_openAtlas = false;
    OpenCase::Enum m_openCase = OpenCase::Retained;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){ return { 960, 720 }; }

TStringView NWB::QueryProjectWindowTitle()noexcept{ return NWB_TEXT("NWB CSG Shadow Smoke"); }

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_shadow_smoke::CsgShadowSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

