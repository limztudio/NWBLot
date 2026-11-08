// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "complex_gi_scene.h"
#include "smoke_scene_helpers.h"

#include <global/simd/frame.h>
#include <global/simd/constant.h>
#include <global/simd/quaternion.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_complex_gi_scene{

using GiTestMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using GiTestMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;

static constexpr GiTestMeshRef s_BoxMesh{"project/meshes/cube_hard_edges"};
static constexpr GiTestMaterialRef s_ComplexIndirectMaterial{"project/smoke/gi/materials/indirect"};
static constexpr GiTestMaterialRef s_CoverageProbeMaterial{"project/smoke/gi/materials/coverage_probe"};
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";
static constexpr Float4 s_NeutralPanelTint(0.76f, 0.76f, 0.76f, 1.0f);
static constexpr Float4 s_WhiteReceiverTint(0.92f, 0.92f, 0.92f, 1.0f);
static constexpr Float4 s_RedBounceTint(0.84f, 0.10f, 0.08f, 1.0f);
static constexpr Float4 s_BlueBounceTint(0.08f, 0.14f, 0.86f, 1.0f);
static constexpr Float4 s_BlackEnclosureTint = NWB::Tests::Smoke::ComplexGiScene::s_BlackEnclosureTint;
static constexpr u32 s_ComplexRandomBodyCount = 20u;
static constexpr u32 s_ComplexRandomBodySeed = 0x7A31C5E9u;

struct ComplexSceneBox{
    Float4 position;
    Float4 scale;
    Float4 tint;
    f32 yawDegrees;
    b8 indirectOnly = false;
};

// Offset partition doorways expose different mixes of red and blue bounce light.
static constexpr Array<ComplexSceneBox, 28> s_ComplexSceneBoxes = {{
    // Front doorway: x=-0.4..0.5 at z=-0.35, with a high lintel.
    { Float4(-1.20f, 0.625f, -0.35f, 0.0f), Float4(1.60f, 1.25f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.25f, 0.625f, -0.35f, 0.0f), Float4(1.50f, 1.25f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4(-0.42f, 1.525f, -0.35f, 0.0f), Float4(0.13f, 0.55f, 0.16f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 0.52f, 1.525f, -0.35f, 0.0f), Float4(0.13f, 0.55f, 0.16f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 0.05f, 1.90f, -0.35f, 0.0f), Float4(0.90f, 0.20f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },

    // Rear doorway is shifted right, forcing a turn and hiding much of the far receiver from direct view.
    { Float4(-0.675f, 0.85f, 0.95f, 0.0f), Float4(2.65f, 1.70f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.725f, 0.85f, 0.95f, 0.0f), Float4(0.55f, 1.70f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.05f, 1.85f, 0.95f, 0.0f), Float4(0.80f, 0.30f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4(-0.65f, 0.60f, 0.30f, 0.0f), Float4(0.13f, 1.20f, 1.15f, 0.0f), s_NeutralPanelTint, 0.0f },

    // Foreground pillars and plinths provide distinct shadow edges and horizontal indirect-light receivers.
    { Float4(-1.60f, 0.65f, -1.50f, 0.0f), Float4(0.30f, 1.30f, 0.30f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.60f, 0.65f, -1.35f, 0.0f), Float4(0.30f, 1.30f, 0.30f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4(-1.80f, 0.48f, -1.82f, 0.0f), Float4(0.18f, 0.96f, 0.56f, 0.0f), s_BlueBounceTint, 0.0f },
    { Float4( 1.80f, 0.48f, -1.82f, 0.0f), Float4(0.18f, 0.96f, 0.56f, 0.0f), s_RedBounceTint, 0.0f },
    { Float4(-0.85f, 0.23f, -1.30f, 0.0f), Float4(0.70f, 0.46f, 0.60f, 0.0f), s_WhiteReceiverTint, 0.0f },
    { Float4( 0.95f, 0.32f, -1.45f, 0.0f), Float4(0.60f, 0.64f, 0.65f, 0.0f), s_WhiteReceiverTint, 0.0f },
    { Float4( 0.00f, 0.20f,  0.35f, 0.0f), Float4(0.52f, 0.40f, 0.50f, 0.0f), s_WhiteReceiverTint, 0.0f },

    // Angled white fins catch directional bounce from the colored panels.
    { Float4(-1.30f, 0.65f, -0.75f, 0.0f), Float4(0.10f, 1.30f, 1.00f, 0.0f), s_WhiteReceiverTint,  55.0f, true },
    { Float4( 1.30f, 0.65f, -0.75f, 0.0f), Float4(0.10f, 1.30f, 1.00f, 0.0f), s_WhiteReceiverTint, -55.0f, true },
    { Float4( 1.72f, 0.70f,  0.38f, 0.0f), Float4(0.12f, 1.40f, 0.72f, 0.0f), s_RedBounceTint, 0.0f },
    { Float4(-1.72f, 0.70f,  0.38f, 0.0f), Float4(0.12f, 1.40f, 0.72f, 0.0f), s_BlueBounceTint, 0.0f },

    // Rear receivers and broken canopy make a deeper, partly skylit third bay with another indirect-light path.
    { Float4(-1.12f, 0.60f, 1.55f, 0.0f), Float4(0.34f, 1.20f, 0.34f, 0.0f), s_WhiteReceiverTint, 0.0f },
    { Float4( 1.05f, 0.55f, 1.55f, 0.0f), Float4(0.40f, 1.10f, 0.42f, 0.0f), s_WhiteReceiverTint, 0.0f },
    { Float4(-1.10f, 1.93f, 1.55f, 0.0f), Float4(1.50f, 0.14f, 0.78f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.30f, 1.93f, 1.55f, 0.0f), Float4(1.10f, 0.14f, 0.78f, 0.0f), s_NeutralPanelTint, 0.0f },

    // A 1.9 by 1.1 metre skylight admits the near-vertical sun onto the colored floor patches below.
    { Float4(-1.475f, 2.04f,  0.000f, 0.0f), Float4(1.05f, 0.12f, 4.00f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 1.475f, 2.04f,  0.000f, 0.0f), Float4(1.05f, 0.12f, 4.00f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.000f, 2.04f, -1.575f, 0.0f), Float4(1.90f, 0.12f, 0.85f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.000f, 2.04f,  0.975f, 0.0f), Float4(1.90f, 0.12f, 2.05f, 0.0f), s_BlackEnclosureTint, 0.0f },
}};

// Indirect-only white fins next to the floor patches show only bounced light.
static constexpr Array<ComplexSceneBox, 2> s_ComplexBouncePatches = {{
    { Float4(-0.45f, 0.025f, -0.55f, 0.0f), Float4(0.85f, 0.05f, 0.80f, 0.0f), s_RedBounceTint, 0.0f },
    { Float4( 0.45f, 0.025f, -0.55f, 0.0f), Float4(0.85f, 0.05f, 0.80f, 0.0f), s_BlueBounceTint, 0.0f },
}};

// Enclose the maze behind the camera so light enters only through the skylight.
static constexpr Array<ComplexSceneBox, 5> s_ComplexFrontEnclosureBoxes = {{
    { Float4(-2.00f, 1.00f, -1.50f, 0.0f), Float4(0.12f, 6.00f, 7.10f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 2.00f, 1.00f, -1.50f, 0.0f), Float4(0.12f, 6.00f, 7.10f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.00f, 1.00f, -5.00f, 0.0f), Float4(4.10f, 2.10f, 0.12f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.00f,-0.06f, -3.50f, 0.0f), Float4(4.10f, 0.12f, 3.10f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.00f, 2.04f, -3.50f, 0.0f), Float4(4.10f, 0.12f, 3.10f, 0.0f), s_BlackEnclosureTint, 0.0f },
}};

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ComplexGiScene::Create(Core::ECS::World& world, const b8 coverageViewEnabled){
    using namespace __hidden_complex_gi_scene;
    for(const ComplexSceneBox& box : s_ComplexSceneBoxes){
        const auto entity = CreateTintedStaticMeshEntity(s_BoxMesh, box.indirectOnly ? s_ComplexIndirectMaterial : s_DiffuseMaterial, s_SmokeSurfaceMaterialInterface, box.tint, box.position, box.scale, world);
        if(!entity.valid())
            return false;
        if(box.yawDegrees != 0.0f){
            auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
            if(!transform)
                return false;
            StoreFloat(QuaternionRotationRollPitchYawFromVector(VectorScale(VectorSet(0.0f, box.yawDegrees, 0.0f, 0.0f), s_PI / 180.0f)), transform->rotation);
        }
    }

    for(const ComplexSceneBox& box : s_ComplexBouncePatches){
        const auto entity = CreateTintedStaticMeshEntity(s_BoxMesh, s_DiffuseMaterial, s_SmokeSurfaceMaterialInterface, box.tint, box.position, box.scale, world);
        if(!entity.valid())
            return false;
    }

    for(const ComplexSceneBox& box : s_ComplexFrontEnclosureBoxes){
        const auto entity = CreateTintedStaticMeshEntity(s_BoxMesh, s_DiffuseMaterial, s_SmokeSurfaceMaterialInterface, box.tint, box.position, box.scale, world);
        if(!entity.valid())
            return false;
    }

    // Place 20 non-overlapping seeded bodies without blocking the skylight or doorways.
    Array<ComplexSceneBox, s_ComplexRandomBodyCount> placedBodies{};
    u32 placedBodyCount = 0u;
    u32 randomState = s_ComplexRandomBodySeed;
    // Scale the top 24 random bits into 0..1.
    const auto nextRandom24 = [&randomState]()noexcept -> f32{
        randomState = randomState * 1664525u + 1013904223u;
        return static_cast<f32>(randomState >> 8u);
    };
    const auto overlaps = [](const ComplexSceneBox& a, const ComplexSceneBox& b)noexcept -> b8{
        const f32 aHalfY = a.scale.y * 0.5f;
        const f32 bHalfY = b.scale.y * 0.5f;
        if(a.position.y + aHalfY <= b.position.y - bHalfY || b.position.y + bHalfY <= a.position.y - aHalfY)
            return false;
        const SIMDVectorSingle angles = VectorScale(VectorSet(a.yawDegrees, b.yawDegrees, 0.0f, 0.0f), s_PI / 180.0f);
        SIMDVectorSingle sine;
        SIMDVectorSingle cosine;
        VectorSinCos(sine, cosine, angles);
        const SIMDVectorSingle absoluteSine = VectorAbs(sine);
        const SIMDVectorSingle absoluteCosine = VectorAbs(cosine);
        const SIMDVectorSingle cosineSine = VectorMergeX(absoluteCosine, absoluteSine, VectorSplatY(absoluteCosine), VectorSplatY(absoluteSine));
        const SIMDVectorSingle halfExtents = VectorScale(
            VectorMultiplyAddExpression(cosineSine, VectorSet(a.scale.x, a.scale.x, b.scale.x, b.scale.x), VectorMultiply(VectorSwizzle<1, 0, 3, 2>(cosineSine), VectorSet(a.scale.z, a.scale.z, b.scale.z, b.scale.z))),
            0.5f
        );
        const SIMDVectorSingle combinedHalfExtents = VectorAdd(VectorAdd(halfExtents, VectorSwizzle<2, 3, 0, 1>(halfExtents)), VectorReplicate(0.06f));
        const SIMDVectorSingle separation = VectorAbs(VectorSubtract(VectorSet(a.position.x, a.position.z, 0.0f, 0.0f), VectorSet(b.position.x, b.position.z, 0.0f, 0.0f)));
        return (VectorMoveMask(VectorLess(separation, combinedHalfExtents)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY;
    };
    for(u32 attempt = 0u; placedBodyCount < s_ComplexRandomBodyCount && attempt < 4096u; ++attempt){
        // Draw order is x, z, width, depth, height, shade, yaw.
        const f32 xRandom = nextRandom24();
        const f32 zRandom = nextRandom24();
        const f32 widthRandom = nextRandom24();
        const f32 depthRandom = nextRandom24();
        const f32 heightRandom = nextRandom24();
        const f32 shadeRandom = nextRandom24();
        const f32 yawRandom = nextRandom24();
        const SIMDVectorSingle footprint = VectorMultiplyAddExpression(VectorSet(3.40f, 4.45f, 0.26f, 0.36f), VectorScale(VectorSet(xRandom, zRandom, widthRandom, depthRandom), 1.0f / 16777216.0f), VectorSet(-1.70f, -2.70f, 0.18f, 0.18f));
        const f32 x = VectorGetX(footprint);
        const f32 z = VectorGetY(footprint);
        const b8 frontRow = z < -1.8f;
        const f32 height = (frontRow ? 0.22f : 0.30f) + (frontRow ? 0.34f : 0.68f) * (heightRandom / 16777216.0f);
        const f32 shade = 0.62f + 0.32f * (shadeRandom / 16777216.0f);
        // Truncate equals a u32 cast here because the scaled value is in 0..7.
        const f32 yawSteps = (static_cast<f32>(static_cast<u32>((yawRandom / 16777216.0f) * 7.0f)) - 3.0f) * 15.0f;
        const f32 width = VectorGetZ(footprint);
        const f32 depth = VectorGetW(footprint);
        const f32 yawDegrees = yawSteps;
        const Float4 position(x, height * 0.5f, z, 0.0f);
        const ComplexSceneBox body{ position, Float4(width, height, depth, 0.0f), Float4(shade, shade, shade, 1.0f), yawDegrees };
        // Leave the line from the camera through the front doorway clear.
        if(z < -1.8f && Abs(x) < 0.72f)
            continue;
        // Sun through the roof opening must still land on both colored floor sources.
        if(x > -1.12f && x < 1.12f && z > -1.20f && z < 0.18f)
            continue;

        b8 blocked = false;
        for(const ComplexSceneBox& box : s_ComplexSceneBoxes)
            blocked |= overlaps(body, box);
        for(const ComplexSceneBox& box : s_ComplexBouncePatches)
            blocked |= overlaps(body, box);
        for(const ComplexSceneBox& box : s_ComplexFrontEnclosureBoxes)
            blocked |= overlaps(body, box);
        for(u32 i = 0u; i < placedBodyCount; ++i)
            blocked |= overlaps(body, placedBodies[i]);
        if(blocked)
            continue;

        const auto entity = CreateTintedStaticMeshEntity(s_BoxMesh, s_DiffuseMaterial, s_SmokeSurfaceMaterialInterface, body.tint, body.position, body.scale, world);
        if(!entity.valid())
            return false;
        if(body.yawDegrees != 0.0f){
            auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
            if(!transform)
                return false;
            StoreFloat(QuaternionRotationRollPitchYawFromVector(VectorScale(VectorSet(0.0f, body.yawDegrees, 0.0f, 0.0f), s_PI / 180.0f)), transform->rotation);
        }
        placedBodies[placedBodyCount++] = body;
    }
    if(placedBodyCount != s_ComplexRandomBodyCount){
        NWB_LOGGER_ERROR(NWB_TEXT("GiTestSmokeProject: could place only {} of {} seeded bodies"), placedBodyCount, s_ComplexRandomBodyCount);
        return false;
    }

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: complex scene {} maze solids, {} colored floor patches, {} closed-front shell solids, and {} seeded bodies (seed={}) created")
        , s_ComplexSceneBoxes.size()
        , s_ComplexBouncePatches.size()
        , s_ComplexFrontEnclosureBoxes.size()
        , placedBodyCount
        , s_ComplexRandomBodySeed
    );
    if(coverageViewEnabled){
        // Place the probe cube where its -X and -Z faces share one spatial cell.
        const auto probe = CreateTintedStaticMeshEntity(s_BoxMesh, s_CoverageProbeMaterial, s_SmokeSurfaceMaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.95f, 1.48f, -2.65f, 0.0f), Float4(0.60f, 0.30f, 0.30f, 0.0f), world);
        if(!probe.valid())
            return false;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: coverage probe with perpendicular same-cell faces created"));
    }
    return true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

