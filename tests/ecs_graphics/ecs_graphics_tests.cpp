// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_graphics/deformable_picking.h>
#include <impl/ecs_graphics/deformable_surface_edit.h>

#include <core/common/common.h>

#include <global/compile.h>

#include <limits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TestContext{
    u32 passed = 0;
    u32 failed = 0;

    void checkTrue(const bool condition, const char* expression, const char* file, const int line){
        if(condition){
            ++passed;
            return;
        }

        ++failed;
        NWB_CERR << file << '(' << line << "): check failed: " << expression << '\n';
    }
};


#define NWB_ECS_GRAPHICS_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


static bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = 0.00001f){
    const f32 difference = lhs > rhs ? lhs - rhs : rhs - lhs;
    return difference <= epsilon;
}

static NWB::Impl::DeformableVertexRest MakeVertex(const f32 x, const f32 y, const f32 z, const f32 u = 0.0f){
    NWB::Impl::DeformableVertexRest vertex;
    vertex.position = Float3Data(x, y, z);
    vertex.normal = Float3Data(0.0f, 0.0f, 1.0f);
    vertex.tangent = Float4Data(1.0f, 0.0f, 0.0f, 1.0f);
    vertex.uv0 = Float2Data(u, 0.0f);
    vertex.color0 = Float4Data(1.0f, 1.0f, 1.0f, 1.0f);
    return vertex;
}

static NWB::Impl::SourceSample MakeSourceSample(const u32 sourceTri, const f32 a, const f32 b, const f32 c){
    NWB::Impl::SourceSample sample;
    sample.sourceTri = sourceTri;
    sample.bary[0] = a;
    sample.bary[1] = b;
    sample.bary[2] = c;
    return sample;
}

static NWB::Impl::SkinInfluence4 MakeSingleJointSkin(const u16 joint){
    NWB::Impl::SkinInfluence4 skin{};
    skin.joint[0] = joint;
    skin.weight[0] = 1.0f;
    return skin;
}

static void AssignSingleJointSkin(NWB::Impl::DeformableRuntimeMeshInstance& instance, const u16 joint){
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin)
        skin = MakeSingleJointSkin(joint);
}

static f32 SkinWeightForJoint(const NWB::Impl::SkinInfluence4& skin, const u16 joint){
    f32 weight = 0.0f;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        if(skin.joint[influenceIndex] == joint)
            weight += skin.weight[influenceIndex];
    }
    return weight;
}

static bool MorphDeltaPositionZForVertex(
    const NWB::Impl::DeformableMorph& morph,
    const u32 vertexId,
    f32& outDeltaZ)
{
    for(const NWB::Impl::DeformableMorphDelta& delta : morph.deltas){
        if(delta.vertexId != vertexId)
            continue;

        outDeltaZ = delta.deltaPosition.z;
        return true;
    }
    return false;
}

static NWB::Impl::DeformableRuntimeMeshInstance MakeTriangleInstance(){
    NWB::Impl::DeformableRuntimeMeshInstance instance;
    instance.entity = NWB::Core::ECS::EntityID(1u, 0u);
    instance.handle.value = 42u;
    instance.editRevision = 7u;
    instance.sourceTriangleCount = 10u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;
    instance.restVertices.push_back(MakeVertex(-1.0f, -1.0f, 0.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(1.0f, -1.0f, 0.0f, 0.5f));
    instance.restVertices.push_back(MakeVertex(0.0f, 1.0f, 0.0f, 1.0f));
    instance.indices.push_back(0u);
    instance.indices.push_back(1u);
    instance.indices.push_back(2u);
    instance.sourceSamples.push_back(MakeSourceSample(9u, 1.0f, 0.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(9u, 0.0f, 1.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(9u, 0.0f, 0.0f, 1.0f));
    return instance;
}

static NWB::Impl::DeformableRuntimeMeshInstance MakeQuadMixedProvenanceInstance(){
    NWB::Impl::DeformableRuntimeMeshInstance instance;
    instance.entity = NWB::Core::ECS::EntityID(2u, 0u);
    instance.handle.value = 43u;
    instance.sourceTriangleCount = 2u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;
    instance.restVertices.push_back(MakeVertex(-1.0f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(1.0f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(1.0f, 1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(-1.0f, 1.0f, 0.0f));
    instance.indices.push_back(0u);
    instance.indices.push_back(1u);
    instance.indices.push_back(2u);
    instance.indices.push_back(0u);
    instance.indices.push_back(2u);
    instance.indices.push_back(3u);
    instance.sourceSamples.push_back(MakeSourceSample(0u, 1.0f, 0.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(0u, 0.0f, 1.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(0u, 0.0f, 0.0f, 1.0f));
    instance.sourceSamples.push_back(MakeSourceSample(1u, 0.0f, 0.0f, 1.0f));
    return instance;
}

static NWB::Impl::DeformableRuntimeMeshInstance MakeOutOfRangeMixedProvenanceInstance(){
    NWB::Impl::DeformableRuntimeMeshInstance instance;
    instance.entity = NWB::Core::ECS::EntityID(20u, 0u);
    instance.handle.value = 45u;
    instance.sourceTriangleCount = 2u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;
    instance.restVertices.push_back(MakeVertex(-1.0f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(1.0f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(0.0f, 1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(1.5f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(2.5f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(2.0f, 1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(3.0f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(5.0f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(4.0f, 1.0f, 0.0f));
    instance.indices.push_back(0u);
    instance.indices.push_back(1u);
    instance.indices.push_back(2u);
    instance.indices.push_back(3u);
    instance.indices.push_back(4u);
    instance.indices.push_back(5u);
    instance.indices.push_back(6u);
    instance.indices.push_back(7u);
    instance.indices.push_back(8u);
    instance.sourceSamples.push_back(MakeSourceSample(0u, 1.0f, 0.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(0u, 0.0f, 1.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(0u, 0.0f, 0.0f, 1.0f));
    instance.sourceSamples.push_back(MakeSourceSample(1u, 1.0f, 0.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(1u, 0.0f, 1.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(1u, 0.0f, 0.0f, 1.0f));
    instance.sourceSamples.push_back(MakeSourceSample(0u, 1.0f, 0.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(1u, 0.0f, 1.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(1u, 0.0f, 0.0f, 1.0f));
    return instance;
}

static NWB::Impl::DeformableRuntimeMeshInstance MakeGridHoleInstance(){
    NWB::Impl::DeformableRuntimeMeshInstance instance;
    instance.entity = NWB::Core::ECS::EntityID(3u, 0u);
    instance.handle.value = 44u;
    instance.editRevision = 3u;
    instance.sourceTriangleCount = 1u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;

    for(u32 y = 0; y < 4u; ++y){
        for(u32 x = 0; x < 4u; ++x)
            instance.restVertices.push_back(MakeVertex(-1.5f + static_cast<f32>(x), -1.5f + static_cast<f32>(y), 0.0f));
    }

    auto vertexIndex = [](const u32 x, const u32 y) -> u32{
        return (y * 4u) + x;
    };

    for(u32 y = 0; y < 3u; ++y){
        for(u32 x = 0; x < 3u; ++x){
            const u32 v0 = vertexIndex(x, y);
            const u32 v1 = vertexIndex(x + 1u, y);
            const u32 v2 = vertexIndex(x + 1u, y + 1u);
            const u32 v3 = vertexIndex(x, y + 1u);
            instance.indices.push_back(v0);
            instance.indices.push_back(v1);
            instance.indices.push_back(v2);
            instance.indices.push_back(v0);
            instance.indices.push_back(v2);
            instance.indices.push_back(v3);
        }
    }

    AssignSingleJointSkin(instance, 0u);

    instance.sourceSamples.resize(instance.restVertices.size());
    for(NWB::Impl::SourceSample& sample : instance.sourceSamples)
        sample = MakeSourceSample(0u, 1.0f, 0.0f, 0.0f);

    return instance;
}

static NWB::Impl::DeformableHoleEditParams MakeHoleEditParams(
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const f32 radius,
    const f32 depth)
{
    NWB::Impl::DeformableHoleEditParams params;
    params.posedHit.entity = instance.entity;
    params.posedHit.runtimeMesh = instance.handle;
    params.posedHit.editRevision = instance.editRevision;
    params.posedHit.triangle = triangle;
    params.posedHit.bary[0] = 0.25f;
    params.posedHit.bary[1] = 0.25f;
    params.posedHit.bary[2] = 0.5f;
    const usize indexBase = static_cast<usize>(triangle) * 3u;
    const Float3Data& a = instance.restVertices[instance.indices[indexBase + 0u]].position;
    const Float3Data& b = instance.restVertices[instance.indices[indexBase + 1u]].position;
    const Float3Data& c = instance.restVertices[instance.indices[indexBase + 2u]].position;
    params.posedHit.position = Float3Data(
        (params.posedHit.bary[0] * a.x) + (params.posedHit.bary[1] * b.x) + (params.posedHit.bary[2] * c.x),
        (params.posedHit.bary[0] * a.y) + (params.posedHit.bary[1] * b.y) + (params.posedHit.bary[2] * c.y),
        (params.posedHit.bary[0] * a.z) + (params.posedHit.bary[1] * b.z) + (params.posedHit.bary[2] * c.z)
    );
    params.posedHit.normal = Float3Data(0.0f, 0.0f, 1.0f);
    params.posedHit.distance = 1.0f;
    (void)NWB::Impl::ResolveDeformableRestSurfaceSample(
        instance,
        triangle,
        params.posedHit.bary,
        params.posedHit.restSample
    );
    params.radius = radius;
    params.ellipseRatio = 1.0f;
    params.depth = depth;
    return params;
}

static NWB::Impl::DeformableHoleEditParams MakeGridHoleEditParams(
    const NWB::Impl::DeformableRuntimeMeshInstance& instance)
{
    return MakeHoleEditParams(instance, 8u, 0.48f, 0.25f);
}

static Float3Data RestHitPosition(
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const NWB::Impl::DeformableHoleEditParams& params)
{
    const usize indexBase = static_cast<usize>(params.posedHit.triangle) * 3u;
    const Float3Data& a = instance.restVertices[instance.indices[indexBase + 0u]].position;
    const Float3Data& b = instance.restVertices[instance.indices[indexBase + 1u]].position;
    const Float3Data& c = instance.restVertices[instance.indices[indexBase + 2u]].position;
    return Float3Data(
        (params.posedHit.bary[0] * a.x) + (params.posedHit.bary[1] * b.x) + (params.posedHit.bary[2] * c.x),
        (params.posedHit.bary[0] * a.y) + (params.posedHit.bary[1] * b.y) + (params.posedHit.bary[2] * c.y),
        (params.posedHit.bary[0] * a.z) + (params.posedHit.bary[1] * b.z) + (params.posedHit.bary[2] * c.z)
    );
}

static void CheckHoleEditUnchanged(
    TestContext& context,
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const usize oldVertexCount,
    const usize oldIndexCount,
    const u32 oldRevision)
{
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.restVertices.size() == oldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.indices.size() == oldIndexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == oldRevision);
}

static void CheckAddedTrianglesResolveToSample(
    TestContext& context,
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const usize firstAddedTriangle,
    const u32 addedTriangleCount,
    const NWB::Impl::SourceSample& expectedSample)
{
    const f32 bary[3] = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f };
    for(u32 triangleOffset = 0u; triangleOffset < addedTriangleCount; ++triangleOffset){
        NWB::Impl::SourceSample sample;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::ResolveDeformableRestSurfaceSample(
                instance,
                static_cast<u32>(firstAddedTriangle + triangleOffset),
                bary,
                sample
            )
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, sample.sourceTri == expectedSample.sourceTri);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[0], expectedSample.bary[0]));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[1], expectedSample.bary[1]));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[2], expectedSample.bary[2]));
    }
}

static void CheckAllTrianglesResolve(TestContext& context, const NWB::Impl::DeformableRuntimeMeshInstance& instance){
    const f32 bary[3] = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f };
    const usize triangleCount = instance.indices.size() / 3u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        NWB::Impl::SourceSample sample;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::ResolveDeformableRestSurfaceSample(
                instance,
                static_cast<u32>(triangle),
                bary,
                sample
            )
        );
    }
}

static void AssignFirstUseTriangleSourceSamples(NWB::Impl::DeformableRuntimeMeshInstance& instance){
    const usize triangleCount = instance.indices.size() / 3u;
    instance.sourceTriangleCount = static_cast<u32>(triangleCount);
    instance.sourceSamples.clear();
    instance.sourceSamples.resize(instance.restVertices.size());

    Vector<u8> assignedSamples;
    assignedSamples.resize(instance.restVertices.size(), 0u);
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        for(u32 corner = 0u; corner < 3u; ++corner){
            const u32 vertex = instance.indices[indexBase + corner];
            if(assignedSamples[vertex] != 0u)
                continue;

            instance.sourceSamples[vertex] = MakeSourceSample(
                static_cast<u32>(triangle),
                corner == 0u ? 1.0f : 0.0f,
                corner == 1u ? 1.0f : 0.0f,
                corner == 2u ? 1.0f : 0.0f
            );
            assignedSamples[vertex] = 1u;
        }
    }
}

static void TestRestSampleInterpolation(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };

    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, sample.sourceTri == 9u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[0], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[1], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[2], 0.5f));
}

static void TestMixedProvenanceFallsBackToRestTriangle(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeQuadMixedProvenanceInstance();

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(-0.5f, 0.5f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::RaycastDeformableRuntimeMesh(instance, NWB::Impl::DeformablePickingInputs{}, ray, hit)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, hit.triangle == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, hit.restSample.sourceTri == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[0], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[1], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[2], 0.5f));
}

static void TestMixedProvenanceRejectsRuntimeTriangleOutsideSourceRange(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeOutOfRangeMixedProvenanceInstance();
    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };

    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 2u, bary, sample));

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(4.0f, 0.0f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::RaycastDeformableRuntimeMesh(instance, NWB::Impl::DeformablePickingInputs{}, ray, hit)
    );
}

static void TestMixedProvenanceRejectsCurrentTriangleFallbackAfterTopologyChange(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeQuadMixedProvenanceInstance();
    instance.editRevision = 1u;

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 1u, bary, sample));
}

static void TestMissingProvenanceRejectsCurrentTriangleFallbackAfterTopologyChange(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.sourceTriangleCount = static_cast<u32>(instance.indices.size() / 3u);
    instance.sourceSamples.clear();
    instance.editRevision = 1u;

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
}

static void TestMissingProvenanceRejectsCurrentTriangleFallbackWhenSourceCountDiffers(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.sourceTriangleCount = static_cast<u32>((instance.indices.size() / 3u) + 1u);
    instance.sourceSamples.clear();
    instance.editRevision = 0u;

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
}

static void TestMissingProvenanceRejectsCurrentTriangleFallbackWithoutSourceCount(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.sourceTriangleCount = 0u;
    instance.sourceSamples.clear();
    instance.editRevision = 0u;

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
}

static void TestMixedProvenanceRejectsCurrentTriangleFallbackWhenEditedTriangleCountDiffers(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeQuadMixedProvenanceInstance();
    instance.restVertices.push_back(MakeVertex(-2.0f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(-1.5f, -1.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(-2.0f, -0.5f, 0.0f));
    instance.indices.push_back(4u);
    instance.indices.push_back(5u);
    instance.indices.push_back(6u);
    instance.sourceSamples.push_back(MakeSourceSample(0u, 1.0f, 0.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(0u, 0.0f, 1.0f, 0.0f));
    instance.sourceSamples.push_back(MakeSourceSample(0u, 0.0f, 0.0f, 1.0f));

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 1u, bary, sample));
}

static void TestRestSampleRejectsMalformedIndexPayload(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.indices.push_back(0u);

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
}

static void TestRestSampleRejectsOutOfRangeProvenance(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    for(NWB::Impl::SourceSample& sample : instance.sourceSamples)
        sample.sourceTri = 99u;

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
}

static void TestRestSampleCanonicalizesEdgeTolerance(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    const f32 bary[3] = { -0.0000005f, 0.5000005f, 0.5f };

    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, sample.sourceTri == 9u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, sample.bary[0] >= 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, sample.bary[1] >= 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, sample.bary[2] >= 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[0] + sample.bary[1] + sample.bary[2], 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[0], 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[1], 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[2], 0.5f));
}

static void TestPickingVerticesRejectInvalidIndexRange(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.indices[2] = 99u;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
    );
}

static void TestPickingVerticesRejectDegenerateTriangle(TestContext& context){
    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.indices[2] = instance.indices[1];

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.restVertices[2].position = instance.restVertices[0].position;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }
}

static void TestPickingVerticesRejectNonFiniteRestData(TestContext& context){
    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.restVertices[1].position.x = std::numeric_limits<f32>::quiet_NaN();

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.restVertices[1].normal = Float3Data(0.0f, 0.0f, 0.0f);

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.restVertices[1].tangent = Float4Data(0.0f, 0.0f, 1.0f, 1.0f);

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }
}

static void TestRaycastReturnsPoseAndRestHit(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(0.0f, 0.0f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::RaycastDeformableRuntimeMesh(instance, NWB::Impl::DeformablePickingInputs{}, ray, hit)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, hit.entity == instance.entity);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, hit.runtimeMesh == instance.handle);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, hit.editRevision == 7u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, hit.triangle == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.bary[0], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.bary[1], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.bary[2], 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.bary[0] + hit.bary[1] + hit.bary[2], 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.distance, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.x, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[0], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[1], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[2], 0.5f));
}

static void TestRaycastRejectsNegativeMinDistance(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(0.0f, 0.0f, -1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, 1.0f);
    ray.minDistance = -2.0f;
    ray.maxDistance = 1.0f;

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::RaycastDeformableRuntimeMesh(instance, NWB::Impl::DeformablePickingInputs{}, ray, hit)
    );
}

static void TestRaycastRejectsUploadDirtyRuntimeMesh(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty;

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(0.0f, 0.0f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::RaycastDeformableRuntimeMesh(instance, NWB::Impl::DeformablePickingInputs{}, ray, hit)
    );

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.empty());
}

static void TestPoseStableRestHitRecovery(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u);
    joints.joints[0].column0 = Float4Data(1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].column1 = Float4Data(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].column2 = Float4Data(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].column3 = Float4Data(2.0f, 0.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(2.0f, 0.0f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, hit));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.x, 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[0], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[1], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[2], 0.5f));

    joints.joints[0].column0 = Float4Data(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].column1 = Float4Data(-1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].column2 = Float4Data(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].column3 = Float4Data(1.25f, -0.5f, 0.0f, 1.0f);

    ray.origin = Float3Data(1.25f, -0.4f, 1.0f);

    NWB::Impl::DeformablePosedHit rotatedHit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, rotatedHit));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rotatedHit.position.x, 1.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rotatedHit.position.y, -0.4f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rotatedHit.position.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, rotatedHit.restSample.sourceTri == 9u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rotatedHit.restSample.bary[0], 0.2f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rotatedHit.restSample.bary[1], 0.3f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rotatedHit.restSample.bary[2], 0.5f));
}

static void TestPickingUsesEntityTransform(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Core::ECS::TransformComponent transform;
    transform.position = AlignedFloat3Data(3.0f, 0.0f, 0.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.transform = &transform;

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(3.0f, 0.0f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, hit));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.x, 3.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[0], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[1], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[2], 0.5f));
}

static void TestPickingIgnoresJointPaletteForUnskinnedMesh(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u);
    joints.joints[0].column0 = Float4Data(1.0f, 0.0f, 0.0f, 0.25f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(0.0f, 0.0f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, hit));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[0], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[1], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[2], 0.5f));
}

static void TestPickingRejectsNonAffineJointPalette(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u);
    joints.joints[0].column0 = Float4Data(1.0f, 0.0f, 0.0f, 0.25f);
    joints.joints[0].column1 = Float4Data(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].column2 = Float4Data(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].column3 = Float4Data(0.0f, 0.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(0.0f, 0.0f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, hit));
}

static void TestPickingRejectsUnusedNonAffineJointPalette(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(2u);
    joints.joints[0] = NWB::Impl::DeformableJointMatrix{};
    joints.joints[1] = NWB::Impl::DeformableJointMatrix{};
    joints.joints[1].column1 = Float4Data(0.0f, 1.0f, 0.0f, 0.25f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(0.0f, 0.0f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, hit));
}

static void TestPickingRejectsInvalidSkinWeights(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);
    instance.skin[0].weight[0] = std::numeric_limits<f32>::quiet_NaN();

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
    );

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.origin = Float3Data(0.0f, 0.0f, 1.0f);
    ray.direction = Float3Data(0.0f, 0.0f, -1.0f);

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, hit));
}

static void TestPickingVerticesIncludeMorphAndDisplacement(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarUvRamp;
    instance.displacement.amplitude = 2.0f;

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("raise");
    NWB::Impl::DeformableMorphDelta delta{};
    delta.vertexId = 0u;
    delta.deltaPosition = Float3Data(0.0f, 0.0f, 1.0f);
    delta.deltaNormal = Float3Data(0.0f, 0.0f, 0.0f);
    delta.deltaTangent = Float4Data(0.0f, 0.0f, 0.0f, 0.0f);
    morph.deltas.push_back(delta);
    instance.morphs.push_back(morph);

    NWB::Impl::DeformableMorphWeightsComponent weights;
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("raise"), 0.5f });

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.morphWeights = &weights;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.z, 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.z, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[2].position.z, 2.0f));
}

static void TestPickingRejectsInvalidDisplacementDescriptor(TestContext& context){
    Vector<NWB::Impl::DeformableVertexRest> vertices;

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.displacement.mode = 99u;
        instance.displacement.amplitude = 1.0f;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.displacement.amplitude = 1.0f;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.displacement.padding0 = 1u;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }
}

static void TestPickingRejectsInvalidMorphDelta(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("broken");
    NWB::Impl::DeformableMorphDelta delta{};
    delta.vertexId = 99u;
    morph.deltas.push_back(delta);
    instance.morphs.push_back(morph);

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
    );

    NWB::Impl::DeformableMorphWeightsComponent weights;
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("broken"), 1.0f });

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.morphWeights = &weights;

    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));

    instance.morphs[0].deltas[0].vertexId = 0u;
    instance.morphs[0].deltas[0].deltaPosition.x = std::numeric_limits<f32>::quiet_NaN();
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
}

static void TestPickingRejectsActiveEmptyMorph(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("empty");
    instance.morphs.push_back(morph);

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
    );

    NWB::Impl::DeformableMorphWeightsComponent weights;
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("empty"), 1.0f });

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.morphWeights = &weights;

    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
}

static void TestPickingRejectsNonFiniteEvaluatedVertices(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("overflow");
    NWB::Impl::DeformableMorphDelta delta{};
    delta.vertexId = 0u;
    delta.deltaPosition = Float3Data(Limit<f32>::s_Max, 0.0f, 0.0f);
    morph.deltas.push_back(delta);
    instance.morphs.push_back(morph);

    NWB::Impl::DeformableMorphWeightsComponent weights;
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("overflow"), 2.0f });

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.morphWeights = &weights;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
}

static void TestRestSpaceHoleEditCreatesPerInstancePatch(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("boundary_lift");
    NWB::Impl::DeformableMorphDelta delta{};
    delta.vertexId = 5u;
    delta.deltaPosition = Float3Data(0.0f, 0.0f, 0.2f);
    delta.deltaNormal = Float3Data(0.0f, 0.0f, 0.0f);
    delta.deltaTangent = Float4Data(0.0f, 0.0f, 0.0f, 0.0f);
    morph.deltas.push_back(delta);
    instance.morphs.push_back(morph);

    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
    const Float3Data holeCenter = RestHitPosition(instance, params);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitDeformableRestSpaceHole(instance, params, &result));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.removedTriangleCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.addedVertexCount == 8u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.addedTriangleCount == 8u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.editRevision == 4u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == 4u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        (instance.dirtyFlags & NWB::Impl::RuntimeMeshDirtyFlag::All) == NWB::Impl::RuntimeMeshDirtyFlag::All
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.restVertices.size() == oldVertexCount + 8u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.indices.size() == oldIndexCount - 6u + 24u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.skin.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.sourceSamples.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.morphs.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.morphs[0].deltas.size() > 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 0u].position.x, -0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 0u].position.y, -0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 1u].position.x, -0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 1u].position.y, -0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 1u].position.z, -0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 2u].position.x, 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 2u].position.y, -0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 3u].position.x, 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 3u].position.y, -0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[oldVertexCount + 3u].position.z, -0.25f));

    u32 rimVertexCount = 0u;
    u32 innerVertexCount = 0u;
    for(usize vertexIndex = oldVertexCount; vertexIndex < instance.restVertices.size(); ++vertexIndex){
        const NWB::Impl::DeformableVertexRest& vertex = instance.restVertices[vertexIndex];
        if(NearlyEqual(vertex.position.z, 0.0f))
            ++rimVertexCount;
        if(NearlyEqual(vertex.position.z, -0.25f))
            ++innerVertexCount;

        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.normal.z, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.z, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.w, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertex.uv0.x >= 0.0f && vertex.uv0.x < 1.0f);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertex.uv0.y == 0.0f || vertex.uv0.y == 1.0f);
        const f32 inwardDot =
            (vertex.normal.x * (holeCenter.x - vertex.position.x))
            + (vertex.normal.y * (holeCenter.y - vertex.position.y))
            + (vertex.normal.z * (holeCenter.z - vertex.position.z))
        ;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, inwardDot > 0.0f);
    }
    NWB_ECS_GRAPHICS_TEST_CHECK(context, rimVertexCount == 4u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, innerVertexCount == 4u);
    for(const u32 index : instance.indices)
        NWB_ECS_GRAPHICS_TEST_CHECK(context, index < instance.restVertices.size());

    f32 rimDeltaZ = 0.0f;
    f32 innerDeltaZ = 0.0f;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        MorphDeltaPositionZForVertex(instance.morphs[0], static_cast<u32>(oldVertexCount + 0u), rimDeltaZ)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        MorphDeltaPositionZForVertex(instance.morphs[0], static_cast<u32>(oldVertexCount + 1u), innerDeltaZ)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimDeltaZ, 0.2f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(innerDeltaZ, 0.1f));
}

static void TestRestSpaceHoleEditTransfersAndInpaintsWallAttributes(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    const usize oldVertexCount = instance.restVertices.size();
    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
    const u16 joint0 = 3u;
    const u16 joint1 = 5u;
    const u16 joint2 = 7u;
    const u16 joint3 = 11u;

    instance.skin[5u] = MakeSingleJointSkin(joint0);
    instance.skin[6u] = MakeSingleJointSkin(joint1);
    instance.skin[10u] = MakeSingleJointSkin(joint2);
    instance.skin[9u] = MakeSingleJointSkin(joint3);
    instance.restVertices[5u].color0 = Float4Data(1.0f, 0.0f, 0.0f, 1.0f);
    instance.restVertices[6u].color0 = Float4Data(0.0f, 1.0f, 0.0f, 1.0f);
    instance.restVertices[10u].color0 = Float4Data(1.0f, 1.0f, 0.0f, 1.0f);
    instance.restVertices[9u].color0 = Float4Data(0.0f, 0.0f, 1.0f, 1.0f);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitDeformableRestSpaceHole(instance, params, &result));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.addedVertexCount != 0u);

    const NWB::Impl::SkinInfluence4& rimSkin0 = instance.skin[oldVertexCount + 0u];
    const NWB::Impl::SkinInfluence4& innerSkin0 = instance.skin[oldVertexCount + 1u];
    const NWB::Impl::SkinInfluence4& rimSkin1 = instance.skin[oldVertexCount + 2u];
    const NWB::Impl::SkinInfluence4& innerSkin1 = instance.skin[oldVertexCount + 3u];
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(rimSkin0, joint0), 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(innerSkin0, joint3), 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(innerSkin0, joint0), 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(innerSkin0, joint1), 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(rimSkin1, joint1), 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(innerSkin1, joint0), 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(innerSkin1, joint1), 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(innerSkin1, joint2), 0.25f));
    const Float4Data& rimColor0 = instance.restVertices[oldVertexCount + 0u].color0;
    const Float4Data& innerColor0 = instance.restVertices[oldVertexCount + 1u].color0;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimColor0.x, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimColor0.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimColor0.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(innerColor0.x, 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(innerColor0.y, 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(innerColor0.z, 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(innerColor0.w, 1.0f));
}

static void TestRestSpaceHoleEditWallTrianglesKeepRecoverableProvenance(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    instance.sourceTriangleCount = 2u;
    instance.sourceSamples[9u] = MakeSourceSample(1u, 1.0f, 0.0f, 0.0f);
    const usize oldIndexCount = instance.indices.size();

    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitDeformableRestSpaceHole(instance, params, &result));

    const usize keptTriangleCount = (oldIndexCount - (static_cast<usize>(result.removedTriangleCount) * 3u)) / 3u;
    CheckAddedTrianglesResolveToSample(
        context,
        instance,
        keptTriangleCount,
        result.addedTriangleCount,
        params.posedHit.restSample
    );
}

static void TestSurfaceEditFlowAttachesAndPersistsAccessory(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    instance.editRevision = 0u;
    const usize oldVertexCount = instance.restVertices.size();
    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

    NWB::Impl::DeformableRuntimeMeshInstance dirtyInstance = MakeGridHoleInstance();
    const NWB::Impl::DeformableHoleEditParams dirtyParams = MakeGridHoleEditParams(dirtyInstance);
    dirtyInstance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty;
    NWB::Impl::DeformableHoleEditResult dirtyResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::CommitDeformableRestSpaceHole(dirtyInstance, dirtyParams, &dirtyResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dirtyResult.editRevision == 0u);
    NWB::Impl::DeformableSurfaceEditSession dirtySession;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::BeginSurfaceEdit(dirtyInstance, dirtyParams.posedHit, dirtySession)
    );

    NWB::Impl::DeformableSurfaceEditSession session;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(instance, params.posedHit, session));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, session.active);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !session.previewed);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, session.entity == instance.entity);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, session.runtimeMesh == instance.handle);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitHole(instance, session, params));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == 0u);

    const NWB::Impl::DeformableHoleEditParams otherParams = MakeHoleEditParams(
        instance,
        0u,
        params.radius,
        params.depth
    );
    NWB::Impl::DeformableHolePreview preview;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::PreviewHole(instance, session, otherParams, preview));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !preview.valid);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !session.previewed);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitHole(instance, session, otherParams));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(instance, session, params, preview));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, session.previewed);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, preview.valid);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(preview.radius, params.radius));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(preview.ellipseRatio, params.ellipseRatio));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(preview.depth, params.depth));

    NWB::Impl::DeformableHoleEditResult result;
    NWB::Impl::DeformableSurfaceEditRecord record;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(instance, session, params, &result, &record));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.editRevision == instance.editRevision);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        (instance.dirtyFlags & NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.firstWallVertex == oldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.wallVertexCount == 8u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, record.hole.baseEditRevision == params.posedHit.editRevision);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, record.hole.restSample.sourceTri == params.posedHit.restSample.sourceTri);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.restPosition.x, params.posedHit.position.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.restPosition.y, params.posedHit.position.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.restPosition.z, params.posedHit.position.z));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.restNormal.z, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.radius, params.radius));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.depth, params.depth));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, record.result.firstWallVertex == result.firstWallVertex);

    const Name mockGeometry("project/meshes/mock_earring");
    const Name mockMaterial("project/materials/mat_test");
    NWB::Impl::DeformableAccessoryAttachmentComponent attachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::AttachAccessory(
            instance,
            result,
            0.08f,
            0.12f,
            attachment
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, attachment.targetEntity == instance.entity);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, attachment.firstWallVertex == result.firstWallVertex);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, attachment.wallVertexCount == result.wallVertexCount);
    NWB::Impl::DeformableAccessoryAttachmentComponent rejectedAttachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::AttachAccessory(
            instance,
            result,
            -0.01f,
            0.12f,
            rejectedAttachment
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !rejectedAttachment.targetEntity.valid());

    NWB::Impl::DeformableHoleEditResult malformedResult = result;
    malformedResult.wallVertexCount = 4u;
    malformedResult.addedTriangleCount = 4u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::AttachAccessory(
            instance,
            malformedResult,
            0.08f,
            0.12f,
            rejectedAttachment
        )
    );

    malformedResult = result;
    malformedResult.addedTriangleCount = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::AttachAccessory(
            instance,
            malformedResult,
            0.08f,
            0.12f,
            rejectedAttachment
        )
    );

    malformedResult = result;
    malformedResult.firstWallVertex = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::AttachAccessory(
            instance,
            malformedResult,
            0.08f,
            0.12f,
            rejectedAttachment
        )
    );

    NWB::Impl::DeformableRuntimeMeshInstance malformedWallInstance = instance;
    const usize wallIndexBase =
        malformedWallInstance.indices.size() - (static_cast<usize>(result.addedTriangleCount) * 3u)
    ;
    malformedWallInstance.indices[wallIndexBase + 1u] = result.firstWallVertex + 1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::AttachAccessory(
            malformedWallInstance,
            result,
            0.08f,
            0.12f,
            rejectedAttachment
        )
    );

    NWB::Core::ECS::TransformComponent baseTransform;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::ResolveAccessoryAttachmentTransform(
            instance,
            NWB::Impl::DeformablePickingInputs{},
            attachment,
            baseTransform
        )
    );
    instance.dirtyFlags = static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            instance,
            NWB::Impl::DeformablePickingInputs{},
            attachment,
            baseTransform
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.position.x, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.position.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.position.z, 0.08f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.scale.x, 0.12f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.scale.y, 0.12f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.scale.z, 0.12f));

    instance.indices.push_back(0u);
    instance.indices.push_back(1u);
    instance.indices.push_back(5u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            instance,
            NWB::Impl::DeformablePickingInputs{},
            attachment,
            baseTransform
        )
    );

    NWB::Impl::DeformableAccessoryAttachmentComponent forgedAttachment = attachment;
    forgedAttachment.firstWallVertex = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::ResolveAccessoryAttachmentTransform(
            instance,
            NWB::Impl::DeformablePickingInputs{},
            forgedAttachment,
            baseTransform
        )
    );

    NWB::Impl::DeformableRuntimeMeshInstance malformedWallResolveInstance = instance;
    malformedWallResolveInstance.indices[wallIndexBase + 1u] = result.firstWallVertex + 1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::ResolveAccessoryAttachmentTransform(
            malformedWallResolveInstance,
            NWB::Impl::DeformablePickingInputs{},
            attachment,
            baseTransform
        )
    );

    ++instance.editRevision;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            instance,
            NWB::Impl::DeformablePickingInputs{},
            attachment,
            baseTransform
        )
    );
    forgedAttachment = attachment;
    forgedAttachment.editRevision = instance.editRevision + 1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::ResolveAccessoryAttachmentTransform(
            instance,
            NWB::Impl::DeformablePickingInputs{},
            forgedAttachment,
            baseTransform
        )
    );

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u);
    joints.joints[0].column3 = Float4Data(2.0f, 0.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs translatedInputs;
    translatedInputs.jointPalette = &joints;
    NWB::Core::ECS::TransformComponent translatedTransform;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            instance,
            translatedInputs,
            attachment,
            translatedTransform
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(translatedTransform.position.x, baseTransform.position.x + 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(translatedTransform.position.y, baseTransform.position.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(translatedTransform.position.z, baseTransform.position.z));

    NWB::Impl::DeformableSurfaceEditState state;
    NWB::Impl::DeformableAccessoryAttachmentRecord accessoryRecord;
    accessoryRecord.geometry = NWB::Core::Assets::AssetRef<NWB::Impl::Geometry>(mockGeometry);
    accessoryRecord.material = NWB::Core::Assets::AssetRef<NWB::Impl::Material>(mockMaterial);
    accessoryRecord.editRevision = attachment.editRevision;
    accessoryRecord.firstWallVertex = attachment.firstWallVertex;
    accessoryRecord.wallVertexCount = attachment.wallVertexCount;
    accessoryRecord.normalOffset = attachment.normalOffset;
    accessoryRecord.uniformScale = attachment.uniformScale;
    state.edits.push_back(record);
    state.accessories.push_back(accessoryRecord);

    NWB::Core::Assets::AssetBytes binary;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SerializeSurfaceEditState(state, binary));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    NWB::Impl::DeformableSurfaceEditState loadedState;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::DeserializeSurfaceEditState(binary, loadedState));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.edits[0].result.wallVertexCount == result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        loadedState.accessories[0].firstWallVertex == attachment.firstWallVertex
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        loadedState.accessories[0].wallVertexCount == attachment.wallVertexCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories[0].geometry.name() == mockGeometry);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories[0].material.name() == mockMaterial);

    NWB::Impl::DeformableSurfaceEditState malformedState = state;
    malformedState.edits[0].result.wallVertexCount = 7u;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.accessories[0].normalOffset = -0.01f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.edits[0].result.wallVertexCount = 4u;
    malformedState.edits[0].result.addedTriangleCount = 4u;
    malformedState.accessories[0].wallVertexCount = 4u;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.edits[0].result.addedTriangleCount = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.accessories.clear();
    malformedState.edits[0].result.firstWallVertex = Limit<u32>::s_Max;
    malformedState.edits[0].result.wallVertexCount = 0u;
    malformedState.edits[0].result.addedTriangleCount = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.edits[0].hole.restSample.sourceTri = Limit<u32>::s_Max;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    ++malformedState.edits[0].hole.baseEditRevision;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.edits[0].hole.baseEditRevision = 5u;
    malformedState.edits[0].result.editRevision = 6u;
    malformedState.accessories[0].editRevision = 6u;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.edits[0].hole.restNormal = Float3Data(0.0f, 0.0f, 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    ++malformedState.accessories[0].editRevision;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SerializeSurfaceEditState(state, binary));
    binary.push_back(0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::DeserializeSurfaceEditState(binary, loadedState));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.edits.empty());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories.empty());
}

static void TestRestSpaceHoleEditSynthesizesProvenanceWhenSourceSamplesAreEmpty(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    instance.sourceTriangleCount = static_cast<u32>(instance.indices.size() / 3u);
    instance.sourceSamples.clear();
    instance.editRevision = 0u;
    const usize oldIndexCount = instance.indices.size();

    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, params.posedHit.restSample.sourceTri == params.posedHit.triangle);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitDeformableRestSpaceHole(instance, params, &result));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.sourceSamples.size() == instance.restVertices.size());
    CheckAllTrianglesResolve(context, instance);

    const usize keptTriangleCount = (oldIndexCount - (static_cast<usize>(result.removedTriangleCount) * 3u)) / 3u;
    CheckAddedTrianglesResolveToSample(
        context,
        instance,
        keptTriangleCount,
        result.addedTriangleCount,
        params.posedHit.restSample
    );
}

static void TestRestSpaceHoleEditRejectsProvenanceSynthesisWithoutSourceTriangleCount(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    instance.sourceTriangleCount = 0u;
    instance.sourceSamples.clear();
    instance.editRevision = 0u;
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();
    const u32 oldRevision = instance.editRevision;

    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params, &result));
    CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
}

static void TestRestSpaceHoleEditMaterializesMixedFallbackProvenance(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    AssignFirstUseTriangleSourceSamples(instance);
    instance.editRevision = 0u;
    const usize oldIndexCount = instance.indices.size();

    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitDeformableRestSpaceHole(instance, params, &result));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.sourceSamples.size() == instance.restVertices.size());
    CheckAllTrianglesResolve(context, instance);

    const usize keptTriangleCount = (oldIndexCount - (static_cast<usize>(result.removedTriangleCount) * 3u)) / 3u;
    CheckAddedTrianglesResolveToSample(
        context,
        instance,
        keptTriangleCount,
        result.addedTriangleCount,
        params.posedHit.restSample
    );
}

static void TestRestSpaceHoleEditRejectsOpenBoundaryPatch(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();
    const u32 oldRevision = instance.editRevision;

    const NWB::Impl::DeformableHoleEditParams params = MakeHoleEditParams(instance, 0u, 0.25f, 0.25f);

    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
    CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
}

static void TestRestSpaceHoleEditRejectsDegenerateHitFrame(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    instance.restVertices[10u].position = instance.restVertices[6u].position;
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();
    const u32 oldRevision = instance.editRevision;

    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
    CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
}

static void TestRestSpaceHoleEditRejectsNonFiniteWallVertices(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    for(NWB::Impl::DeformableVertexRest& vertex : instance.restVertices)
        vertex.position.z = -Limit<f32>::s_Max;
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();
    const u32 oldRevision = instance.editRevision;

    NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
    params.depth = Limit<f32>::s_Max;

    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
    CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
}

static void TestRestSpaceHoleEditRejectsInvalidAttributeStreams(TestContext& context){
    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        instance.skin[0].weight[0] = 0.5f;

        const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        instance.sourceSamples[0].bary[0] = -0.0000005f;
        instance.sourceSamples[0].bary[1] = 1.0000005f;

        const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        instance.sourceSamples[0].sourceTri = 99u;

        const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        params.posedHit.restSample.bary[0] = 0.0f;
        params.posedHit.restSample.bary[1] = 1.0f;
        params.posedHit.restSample.bary[2] = 0.0f;

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }
}

static void TestRestSpaceHoleEditRejectsMalformedRuntimePayload(TestContext& context){
    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        instance.restVertices[0u].normal = Float3Data(0.0f, 0.0f, 0.0f);

        const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        instance.indices[2u] = instance.indices[1u];

        const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableMorph duplicateMorph;
        duplicateMorph.name = Name("duplicate");
        NWB::Impl::DeformableMorphDelta delta{};
        delta.vertexId = 5u;
        duplicateMorph.deltas.push_back(delta);
        duplicateMorph.deltas.push_back(delta);
        instance.morphs.push_back(duplicateMorph);

        const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }
}

static void TestRestSpaceHoleEditRejectsInvalidDisplacementDescriptor(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();
    const u32 oldRevision = instance.editRevision;
    instance.displacement.mode = 99u;
    instance.displacement.amplitude = 1.0f;

    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
    CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
}

static void TestRestSpaceHoleEditRejectsStaleOrMismatchedHit(TestContext& context){
    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        params.posedHit.entity = NWB::Core::ECS::ENTITY_ID_INVALID;

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        params.posedHit.entity = NWB::Core::ECS::EntityID(99u, 0u);

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        params.posedHit.runtimeMesh.reset();

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        ++params.posedHit.runtimeMesh.value;

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        ++params.posedHit.editRevision;

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        params.posedHit.position.x = std::numeric_limits<f32>::quiet_NaN();

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        params.posedHit.normal = Float3Data(0.0f, 0.0f, 0.0f);

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        params.posedHit.distance = -1.0f;

        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitDeformableRestSpaceHole(instance, params));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }
}

#undef NWB_ECS_GRAPHICS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(){
    NWB::Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "ecs graphics tests failed: common initialization failed\n";
        return 1;
    }

    __hidden_ecs_graphics_tests::TestContext context;
    __hidden_ecs_graphics_tests::TestRestSampleInterpolation(context);
    __hidden_ecs_graphics_tests::TestMixedProvenanceFallsBackToRestTriangle(context);
    __hidden_ecs_graphics_tests::TestMixedProvenanceRejectsRuntimeTriangleOutsideSourceRange(context);
    __hidden_ecs_graphics_tests::TestMixedProvenanceRejectsCurrentTriangleFallbackAfterTopologyChange(context);
    __hidden_ecs_graphics_tests::TestMissingProvenanceRejectsCurrentTriangleFallbackAfterTopologyChange(context);
    __hidden_ecs_graphics_tests::TestMissingProvenanceRejectsCurrentTriangleFallbackWhenSourceCountDiffers(context);
    __hidden_ecs_graphics_tests::TestMissingProvenanceRejectsCurrentTriangleFallbackWithoutSourceCount(context);
    __hidden_ecs_graphics_tests::TestMixedProvenanceRejectsCurrentTriangleFallbackWhenEditedTriangleCountDiffers(context);
    __hidden_ecs_graphics_tests::TestRestSampleRejectsMalformedIndexPayload(context);
    __hidden_ecs_graphics_tests::TestRestSampleRejectsOutOfRangeProvenance(context);
    __hidden_ecs_graphics_tests::TestRestSampleCanonicalizesEdgeTolerance(context);
    __hidden_ecs_graphics_tests::TestPickingVerticesRejectInvalidIndexRange(context);
    __hidden_ecs_graphics_tests::TestPickingVerticesRejectDegenerateTriangle(context);
    __hidden_ecs_graphics_tests::TestPickingVerticesRejectNonFiniteRestData(context);
    __hidden_ecs_graphics_tests::TestRaycastReturnsPoseAndRestHit(context);
    __hidden_ecs_graphics_tests::TestRaycastRejectsNegativeMinDistance(context);
    __hidden_ecs_graphics_tests::TestRaycastRejectsUploadDirtyRuntimeMesh(context);
    __hidden_ecs_graphics_tests::TestPoseStableRestHitRecovery(context);
    __hidden_ecs_graphics_tests::TestPickingUsesEntityTransform(context);
    __hidden_ecs_graphics_tests::TestPickingIgnoresJointPaletteForUnskinnedMesh(context);
    __hidden_ecs_graphics_tests::TestPickingRejectsNonAffineJointPalette(context);
    __hidden_ecs_graphics_tests::TestPickingRejectsUnusedNonAffineJointPalette(context);
    __hidden_ecs_graphics_tests::TestPickingRejectsInvalidSkinWeights(context);
    __hidden_ecs_graphics_tests::TestPickingVerticesIncludeMorphAndDisplacement(context);
    __hidden_ecs_graphics_tests::TestPickingRejectsInvalidDisplacementDescriptor(context);
    __hidden_ecs_graphics_tests::TestPickingRejectsInvalidMorphDelta(context);
    __hidden_ecs_graphics_tests::TestPickingRejectsActiveEmptyMorph(context);
    __hidden_ecs_graphics_tests::TestPickingRejectsNonFiniteEvaluatedVertices(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditCreatesPerInstancePatch(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditTransfersAndInpaintsWallAttributes(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditWallTrianglesKeepRecoverableProvenance(context);
    __hidden_ecs_graphics_tests::TestSurfaceEditFlowAttachesAndPersistsAccessory(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditSynthesizesProvenanceWhenSourceSamplesAreEmpty(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsProvenanceSynthesisWithoutSourceTriangleCount(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditMaterializesMixedFallbackProvenance(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsOpenBoundaryPatch(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsDegenerateHitFrame(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsNonFiniteWallVertices(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsInvalidAttributeStreams(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsMalformedRuntimePayload(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsInvalidDisplacementDescriptor(context);
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsStaleOrMismatchedHit(context);
    if(context.failed != 0u){
        NWB_CERR << "ecs graphics tests failed: " << context.failed << " failed, " << context.passed << " passed\n";
        return 1;
    }

    NWB_COUT << "ecs graphics tests passed: " << context.passed << '\n';
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

