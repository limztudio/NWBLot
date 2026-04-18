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

static NWB::Impl::DeformableRuntimeMeshInstance MakeTriangleInstance(){
    NWB::Impl::DeformableRuntimeMeshInstance instance;
    instance.entity = NWB::Core::ECS::EntityID(1u, 0u);
    instance.handle.value = 42u;
    instance.editRevision = 7u;
    instance.sourceTriangleCount = 10u;
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

    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin){
        skin.joint[0] = 0u;
        skin.weight[0] = 1.0f;
    }

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
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.distance, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.x, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[0], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[1], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[2], 0.5f));
}

static void TestPoseStableRestHitRecovery(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin){
        skin.joint[0] = 0u;
        skin.weight[0] = 1.0f;
    }

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
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin){
        skin.joint[0] = 0u;
        skin.weight[0] = 1.0f;
    }

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
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin){
        skin.joint[0] = 0u;
        skin.weight[0] = 1.0f;
    }

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
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin){
        skin.joint[0] = 0u;
        skin.weight[0] = 1.0f;
    }
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
    NWB::Impl::DeformableMorphDelta delta;
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
    NWB::Impl::DeformableMorphDelta delta;
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

static void TestRestSpaceHoleEditCreatesPerInstancePatch(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("boundary_lift");
    NWB::Impl::DeformableMorphDelta delta;
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
    NWB_ECS_GRAPHICS_TEST_CHECK(context, (instance.dirtyFlags & NWB::Impl::RuntimeMeshDirtyFlag::All) == NWB::Impl::RuntimeMeshDirtyFlag::All);
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

    bool foundTransferredMorph = false;
    for(const NWB::Impl::DeformableMorphDelta& transferredDelta : instance.morphs[0].deltas){
        if(transferredDelta.vertexId >= oldVertexCount && NearlyEqual(transferredDelta.deltaPosition.z, 0.2f))
            foundTransferredMorph = true;
    }
    NWB_ECS_GRAPHICS_TEST_CHECK(context, foundTransferredMorph);
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
        NWB::Impl::DeformableMorphDelta delta;
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
    __hidden_ecs_graphics_tests::TestRestSampleRejectsMalformedIndexPayload(context);
    __hidden_ecs_graphics_tests::TestRestSampleRejectsOutOfRangeProvenance(context);
    __hidden_ecs_graphics_tests::TestRestSampleCanonicalizesEdgeTolerance(context);
    __hidden_ecs_graphics_tests::TestPickingVerticesRejectInvalidIndexRange(context);
    __hidden_ecs_graphics_tests::TestPickingVerticesRejectNonFiniteRestData(context);
    __hidden_ecs_graphics_tests::TestRaycastReturnsPoseAndRestHit(context);
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
    __hidden_ecs_graphics_tests::TestRestSpaceHoleEditCreatesPerInstancePatch(context);
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
