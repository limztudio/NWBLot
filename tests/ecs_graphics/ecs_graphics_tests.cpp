// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_graphics/deformable_picking.h>

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

static void TestPickingRejectsInvalidSkinWeights(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin){
        skin.joint[0] = 0u;
        skin.weight[0] = 1.0f;
    }
    instance.skin[0].weight[0] = std::numeric_limits<f32>::quiet_NaN();

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
    __hidden_ecs_graphics_tests::TestRaycastReturnsPoseAndRestHit(context);
    __hidden_ecs_graphics_tests::TestPoseStableRestHitRecovery(context);
    __hidden_ecs_graphics_tests::TestPickingUsesEntityTransform(context);
    __hidden_ecs_graphics_tests::TestPickingRejectsNonAffineJointPalette(context);
    __hidden_ecs_graphics_tests::TestPickingRejectsInvalidSkinWeights(context);
    __hidden_ecs_graphics_tests::TestPickingVerticesIncludeMorphAndDisplacement(context);

    if(context.failed != 0u){
        NWB_CERR << "ecs graphics tests failed: " << context.failed << " failed, " << context.passed << " passed\n";
        return 1;
    }

    NWB_COUT << "ecs graphics tests passed: " << context.passed << '\n';
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
