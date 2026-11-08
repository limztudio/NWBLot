// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_shadow_atlas.h"
#include "csg_smoke_helpers.h"

#include <impl/ecs_csg/module.h>

#include <global/simd/frame.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


namespace Arm = CsgShadowArm;
namespace OpenCase = CsgShadowOpenCase;
using EntityID = Core::ECS::EntityID;
using Box = SmokeBoxBounds;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_PlaneShapePath = NWB::Tests::Smoke::s_ShadowSmokePlaneShapePath;
constexpr AStringView s_CapsuleShapePath = NWB::Tests::Smoke::s_ShadowSmokeCapsuleShapePath;
static constexpr AStringView s_EngineCsgSphere = NWB::Tests::Smoke::s_ShadowSmokeEngineCsgSphere;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/csg_caustic/materials/glass"};
#else
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/csg_shadow/materials/glass"};
#endif
static constexpr SmokeMaterialRef s_OpenMaterial{"project/smoke/reflection/materials/optical_volume"};
static constexpr AStringView s_OpenInterface = NWB::Tests::Smoke::s_ShadowSmokeOpenInterface;
static constexpr Array<Name, 7> s_Groups = {
    Name(s_SmokeSceneNameCsgShadowOpaqueHole), Name(s_SmokeSceneNameCsgShadowOpaqueCap), Name(s_SmokeSceneNameCsgShadowGlassDepth), Name(s_SmokeSceneNameCsgShadowGlassHole),
    Name(s_SmokeSceneNameCsgShadowOverlapA), Name(s_SmokeSceneNameCsgShadowOverlapB), Name(s_SmokeSceneNameCsgShadowGlassControl),
};
static constexpr Array<Name, 6> s_AnalyticGroups = {
    Name(s_SmokeSceneNameCsgShadowAnalyticPlane), Name(s_SmokeSceneNameCsgShadowAnalyticSphere), Name(s_SmokeSceneNameCsgShadowAnalyticCapsule),
    Name(s_SmokeSceneNameCsgShadowAnalyticAxialCapsule), Name(s_SmokeSceneNameCsgShadowAnalyticUnion), Name(s_SmokeSceneNameCsgShadowAnalyticControl),
};
static constexpr Array<Name, 2> s_OpenGroups = { Name(s_SmokeSceneNameCsgShadowOpenFive), Name(s_SmokeSceneNameCsgShadowOpenSix) };
static constexpr SmokeMeshRef s_OpenPairMesh{"project/meshes/csg_shadow_open_2"};
static constexpr SmokeMeshRef s_OpenFiveMesh{"project/meshes/csg_shadow_open_5"};
static constexpr SmokeMeshRef s_OpenSixMesh{"project/meshes/csg_shadow_open_6"};
static constexpr SmokeMeshRef s_OpenBoundaryMesh{"project/meshes/csg_shadow_open_64"};
static constexpr SmokeMeshRef s_OpenOverflowMesh{"project/meshes/csg_shadow_open_65"};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgShadowAtlas::create(){
    if(m_mode.openAtlas)
        return createOpenAtlas();
    if(m_mode.analyticAtlas)
        createAnalyticAtlas();
    else
        createAtlas();
    return true;
}

void CsgShadowAtlas::moveCutters(){
    for(u32 index = 0u; index < m_movingCount; ++index){
        auto& cutter = *MakeNotNull(m_world.tryGetComponent<Impl::CsgCutterComponent>(m_movingCutters[index]));
        AssignCsgCutterTransform(LoadFloat(m_finalCutterCenters[index]), QuaternionIdentity(), cutter);
    }
}


EntityID CsgShadowAtlas::createBox(const Box& box, const b8 transparent, const SmokeMeshRef& mesh){
    const SIMDVectorSingle minimum = LoadFloat(box.minimum);
    const SIMDVectorSingle maximum = LoadFloat(box.maximum);
    Float4 center;
    Float4 extent;
    StoreFloat(VectorScale(VectorAdd(minimum, maximum), 0.5f), center);
    StoreFloat(VectorSubtract(maximum, minimum), extent);
    const auto entity = CreateTintedStaticMeshEntity(
        mesh, transparent ? (m_mode.openAtlas ? s_OpenMaterial : s_GlassMaterial) : m_opaqueMaterial,
        transparent && m_mode.openAtlas ? NameText<char>{s_OpenInterface} : NameText<char>{s_CsgShadowMaterialInterface}, Float4(1.0f, 1.0f, 1.0f, 1.0f),
        center, extent, m_world
    );
    NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("CsgShadowSmokeProject: box creation failed"));
#if defined(NWB_CSG_CAUSTIC_SMOKE)
    if(transparent)
        m_world.entity(entity).getComponent<NWB::Impl::RendererComponent>().opticalBoundaryMode = NWB::Impl::OpticalBoundaryMode::ClosedNested;
#endif
    if(transparent && m_mode.openAtlas){
        const Half packedIor = ConvertFloatToHalf(1.5f);
        const Half4U packedTransmission = MakeHalf4U(0.25f, 0.5f, 0.75f, 0.0f);
        NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
            m_world, entity, Name(s_OpenInterface), "runtime.ior", NWB::Impl::MaterialLayoutFieldType::f16,
            NWB::Impl::PackMaterialInstanceValue(packedIor)
        ), NWB_TEXT("CsgShadowSmokeProject: open-sheet IOR override failed")
        );
        NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
            m_world, entity, Name(s_OpenInterface), "runtime.unit_transmission", NWB::Impl::MaterialLayoutFieldType::f16x3,
            NWB::Impl::PackMaterialInstanceBytes(Span<const Half>(packedTransmission.raw, 3u))
        ), NWB_TEXT("CsgShadowSmokeProject: open-sheet transmission override failed")
        );
    }
    return entity;
}

void CsgShadowAtlas::createReferencePieces(const Box& box, const Box& cutter, const b8 transparent){
    // Six disjoint ordinary boxes represent the subtraction without CSG components or generated caps.
    Array<Box, 6> pieces{};
    if(!BuildSmokeBoxSubtractionPieces(box, cutter, pieces)){
        createBox(box, transparent);
        return;
    }
    for(const auto& piece : pieces){
        if(SmokeBoxBoundsValid(piece))
            createBox(piece, transparent);
    }
}

void CsgShadowAtlas::addCaster(const Box& box, const Box& cutterBox, const u32 groupIndex, const b8 transparent, const b8 moving){
    if(m_mode.arm == Arm::Reference){
        createReferencePieces(box, cutterBox, transparent);
        return;
    }
    const auto entity = createBox(box, transparent);
    AddStaticCsgMeshReceiver(entity, s_Groups[groupIndex], !transparent, transparent, m_world);
    auto cutterEntity = m_world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
    cutter.receiverGroup = s_Groups[groupIndex];
    cutter.shapeType = Name(NWB::Impl::s_CsgBoxShapeName);
    cutter.active = m_mode.arm != Arm::Uncut;
    NWB::Impl::CsgBoxShapeParameters parameters;
    const SIMDVectorSingle minimum = LoadFloat(cutterBox.minimum);
    const SIMDVectorSingle maximum = LoadFloat(cutterBox.maximum);
    StoreFloat(VectorScale(VectorSubtract(maximum, minimum), 0.5f), parameters.halfExtents);
    AssignCsgCutterParameters(parameters, cutter);
    const SIMDVectorSingle finalCenter = VectorScale(VectorAdd(minimum, maximum), 0.5f);
    SIMDVectorSingle initialCenter = finalCenter;
    if(moving && m_mode.arm == Arm::Moved)
        initialCenter = VectorSubtract(initialCenter, VectorScale(VectorSet(0.6f, 0.0f, 0.0f, 0.0f), m_mode.pointLight ? 0.5f : 1.0f));
    AssignCsgCutterTransform(initialCenter, QuaternionIdentity(), cutter);
    if(moving){
        NWB_ASSERT(m_movingCount < m_movingCutters.size());
        m_movingCutters[m_movingCount] = cutterEntity.id();
        StoreFloat(finalCenter, m_finalCutterCenters[m_movingCount]);
        ++m_movingCount;
    }
}

void CsgShadowAtlas::createAtlas(){
#if defined(NWB_CSG_CAUSTIC_SMOKE)
    const Box split{ Float3U(-2.0f, -0.7f, -7.0f), Float3U(-0.6f, 0.7f, -5.0f) };
    const Box cavity{ Float3U(-2.2f, -0.9f, -6.5f), Float3U(-0.4f, 0.9f, -5.5f) };
    if(m_mode.arm == Arm::Reference)
        createBox(split, true, SmokeMeshRef{"project/meshes/csg_caustic_split"});
    else
        addCaster(split, cavity, 2u, true, false);
    const Box control{ Float3U(0.6f, -0.7f, -7.0f), Float3U(2.0f, 0.7f, -5.0f) };
    createBox(control, true);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticSmokeProject: split cavity has four interfaces before the receiver; control has two"));
#else
    const f32 xyScale = m_mode.pointLight ? 0.5f : 1.0f;
    for(u32 slot = 0u; slot < 6u; ++slot){
        const f32 centerX = (static_cast<f32>(slot % 3u) - 1.0f) * 2.0f * xyScale;
        const f32 centerY = (slot < 3u ? 1.0f : -1.0f) * xyScale;
        const f32 halfExtent = 0.7f * xyScale;
        const Box box{ Float3U(centerX - halfExtent, centerY - halfExtent, -7.0f), Float3U(centerX + halfExtent, centerY + halfExtent, -5.0f) };
        const b8 hole = slot == 0u || slot == 3u;
        const f32 holeHalfWidth = m_mode.pointLight ? 0.36f : 0.24f;
        const f32 holeCenterX = centerX + 0.3f * xyScale;
        const f32 holeHalfHeight = 0.4f * xyScale;
        Box cutter = hole
            ? Box{ Float3U(holeCenterX - holeHalfWidth, centerY - holeHalfHeight, -7.2f), Float3U(holeCenterX + holeHalfWidth, centerY + holeHalfHeight, -4.8f) }
            : Box{ Float3U(centerX - 1.0f, centerY - 1.0f, -6.0f), Float3U(centerX + 1.0f, centerY + 1.0f, -4.8f) };
        if(slot == 5u){
            cutter.minimum.x += 20.0f;
            cutter.maximum.x += 20.0f;
        }
        const u32 groupIndex = slot == 5u ? 6u : slot;
        addCaster(box, cutter, groupIndex, slot >= 2u, hole);
        if(slot == 4u){
            const Box shiftedBox{ Float3U(box.minimum.x, box.minimum.y, box.minimum.z - 0.5f), Float3U(box.maximum.x, box.maximum.y, box.maximum.z - 0.5f) };
            const Box shiftedCutter{ Float3U(cutter.minimum.x, cutter.minimum.y, cutter.minimum.z - 0.5f), Float3U(cutter.maximum.x, cutter.maximum.y, cutter.maximum.z - 0.5f) };
            addCaster(shiftedBox, shiftedCutter, 5u, true, false);
        }
    }
#endif
}

template<typename ParameterT>
void CsgShadowAtlas::addAnalyticCutter(const u32 groupIndex, const Name shapeType, const ParameterT& parameters, const SIMDMatrix& shapeToWorld){
    auto cutterEntity = m_world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
    cutter.receiverGroup = s_AnalyticGroups[groupIndex];
    cutter.shapeType = shapeType;
    cutter.active = m_mode.arm == Arm::Cut;
    AssignCsgCutterParameters(parameters, cutter);
    SIMDVectorSingle determinant;
    StoreFloat(MatrixInverse(&determinant, shapeToWorld), cutter.worldToShape);
    StoreFloat(shapeToWorld, cutter.shapeToWorld);
}

void CsgShadowAtlas::createAnalyticAtlas(){
    for(u32 slot = 0u; slot < 6u; ++slot){
        const f32 centerX = (static_cast<f32>(slot % 3u) - 1.0f) * 2.0f;
        const f32 centerY = slot < 3u ? 1.0f : -1.0f;
        const Box box{ Float3U(centerX - 0.7f, centerY - 0.7f, -7.0f), Float3U(centerX + 0.7f, centerY + 0.7f, -5.0f) };
        const auto entity = createBox(box, true);
        AddStaticCsgMeshReceiver(entity, s_AnalyticGroups[slot], false, true, m_world);
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

b8 CsgShadowAtlas::createOpenAtlas(){
    const b8 clipped = m_mode.openCase == OpenCase::Clipped || m_mode.openCase == OpenCase::ClippedReference;
    if(m_mode.openCase == OpenCase::Reference){
        if(!createBox({ Float3U(-1.9f, -0.7f, -7.0f), Float3U(-0.5f, 0.7f, -5.0f) }, true, s_OpenFiveMesh).valid())
            return false;
        // Separate ordinary two-sheet instances give an independent exact even-pair reference beyond the ordinary four-event approximation.
        for(u32 pair = 0u; pair < 3u; ++pair){
            const f32 lower = -7.0f + static_cast<f32>(pair) * 0.8f;
            if(!createBox({ Float3U(0.5f, -0.7f, lower), Float3U(1.9f, 0.7f, lower + 0.4f) }, true, s_OpenPairMesh).valid())
                return false;
        }
    }
    else if(m_mode.openCase == OpenCase::ClippedReference){
        for(u32 pair = 0u; pair < 2u; ++pair){
            const f32 lower = -6.5f + static_cast<f32>(pair);
            if(!createBox({ Float3U(-1.9f, -0.7f, lower), Float3U(-0.5f, 0.7f, lower + 0.5f) }, true, s_OpenPairMesh).valid())
                return false;
        }
        if(!createBox({ Float3U(0.5f, -0.7f, -6.6f), Float3U(1.9f, 0.7f, -5.0f) }, true, s_OpenFiveMesh).valid())
            return false;
    }
    else{
        const SmokeMeshRef firstMesh = m_mode.openCase == OpenCase::Boundary64 ? s_OpenBoundaryMesh : m_mode.openCase == OpenCase::Terminal65 ? s_OpenOverflowMesh : s_OpenFiveMesh;
        const Array<SmokeMeshRef, 2> meshes = { firstMesh, s_OpenSixMesh };
        for(u32 tile = 0u; tile < 2u; ++tile){
            const f32 x = tile == 0u ? -1.2f : 1.2f;
            const Box bounds{ Float3U(x - 0.7f, -0.7f, -7.0f), Float3U(x + 0.7f, 0.7f, -5.0f) };
            const auto receiver = createBox(bounds, true, meshes[tile]);
            AddStaticCsgMeshReceiver(receiver, s_OpenGroups[tile], false, true, m_world);
            auto entity = m_world.createEntity();
            if(!entity.id().valid())
                return false;
            auto& cutter = entity.addComponent<NWB::Impl::CsgCutterComponent>();
            cutter.receiverGroup = s_OpenGroups[tile];
            cutter.shapeType = Name(s_PlaneShapePath);
            NWB::Impl::CsgPlaneShapeParameters plane;
            plane.normalDistance = Float4(0.0f, 0.0f, 1.0f, clipped ? 6.75f : 8.0f);
            AssignCsgCutterParameters(plane, cutter);
            AssignCsgCutterTransform(VectorZero(), QuaternionIdentity(), cutter);
        }
    }
    const u32 firstCount = m_mode.openCase == OpenCase::Boundary64 ? 64u : m_mode.openCase == OpenCase::Terminal65 ? 65u : clipped ? 4u : 5u;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: open_case={} retained_events={},{} receiver_z=0 sheets_z_min=-7 sheets_z_max=-5 ior=1.5 unit_transmission=0.25,0.5,0.75 coverage=1")
        , ConvertString(m_mode.openCaseName())
        , firstCount
        , clipped ? 5u : 6u
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

