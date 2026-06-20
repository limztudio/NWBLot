// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/skinning/resource_names.h>
#include <impl/ecs_mesh/skinning/skin_payload.h>

#include <tests/capturing_logger.h>
#include <tests/ecs_test_world.h>
#include <tests/meshlet_ref_test_data.h>
#include <tests/test_context.h>
#include <gtest/gtest.h>

#include <core/common/module.h>
#include <core/ecs/module.h>
#include <core/mesh/classification.h>
#include <impl/ecs_mesh/components.h>
#include <impl/ecs_skeleton/components.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_render/material_typed_private.h>
#include <impl/ecs_render/material_instance.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_mesh/skin_types.h>
#include <impl/assets_mesh/asset.h>

#include <core/common/log.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::MakeTriangleIndices;
using NWB::Tests::NearlyEqual;
using AString = NWB::Tests::TestAString;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;


#define NWB_ECS_GRAPHICS_TEST_CHECK NWB_TEST_CHECK

inline constexpr Name s_ScratchArena("tests/ecs_graphics/scratch");


static void TestRuntimeResourceNameBuilderMatchesFormattedSuffix(TestContext& context){
    NWB::Tests::TestArena<> arena;
    const Name sourceName("project/meshes/mesh_skinning_source");
    const auto suffix = NWB::Impl::BuildRuntimeResourceSuffix(arena.arena, 42u, 17u, "mesh_skinning_ranges");
    NWB_ECS_GRAPHICS_TEST_CHECK(context, AStringView(suffix.data(), suffix.size()) == AStringView(":runtime_42_revision_17_mesh_skinning_ranges"));

    const Name builtName = NWB::Impl::DeriveRuntimeResourceName(sourceName, 42u, 17u, "mesh_skinning_ranges");
    const Name formattedName = DeriveName(sourceName, AStringView(":runtime_42_revision_17_mesh_skinning_ranges"));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, builtName == formattedName);
}


using TestWorld = NWB::Tests::EcsTestWorld;

static void TestLightComponents(TestContext& context){
    TestWorld testWorld;

    auto directionalEntity = testWorld.world.createEntity();
    auto& directionalTransform = directionalEntity.addComponent<NWB::Impl::Scene::TransformComponent>();
    auto& directionalLight = directionalEntity.addComponent<NWB::Impl::Scene::LightComponent>();

    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalEntity.hasComponent<NWB::Impl::Scene::TransformComponent>());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalEntity.hasComponent<NWB::Impl::Scene::LightComponent>());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.type == NWB::Impl::Scene::LightType::Directional);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.color().x == 1.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.color().y == 1.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.color().z == 1.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.intensity() > 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.range > 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalTransform.rotation.w == 1.0f);

    auto pointEntity = testWorld.world.createEntity();
    auto& pointTransform = pointEntity.addComponent<NWB::Impl::Scene::TransformComponent>();
    auto& pointLight = pointEntity.addComponent<NWB::Impl::Scene::LightComponent>();
    pointTransform.position = Float4(1.0f, 2.0f, 3.0f);
    pointLight.type = NWB::Impl::Scene::LightType::Point;
    pointLight.setColor(Float4(1.0f, 0.75f, 0.5f));
    pointLight.setIntensity(4.0f);
    pointLight.range = 12.0f;

    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointEntity.hasComponent<NWB::Impl::Scene::TransformComponent>());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointEntity.hasComponent<NWB::Impl::Scene::LightComponent>());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.type == NWB::Impl::Scene::LightType::Point);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.color().x == 1.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.color().y == 0.75f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.color().z == 0.5f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.intensity() == 4.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.range == 12.0f);

    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        (reinterpret_cast<usize>(&directionalLight) % alignof(NWB::Impl::Scene::LightComponent)) == 0
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, (reinterpret_cast<usize>(&pointLight) % alignof(NWB::Impl::Scene::LightComponent)) == 0);

    const NWB::Core::ECS::EntityID pointEntityId = pointEntity.id();
    usize lightViewCount = 0;
    usize directionalLightCount = 0;
    usize pointLightCount = 0;
    testWorld.world.view<
        NWB::Impl::Scene::TransformComponent,
        NWB::Impl::Scene::LightComponent
    >().each(
        [&context, &lightViewCount, &directionalLightCount, &pointLightCount, pointEntityId](
            NWB::Core::ECS::EntityID entityId,
            NWB::Impl::Scene::TransformComponent& viewTransform,
            NWB::Impl::Scene::LightComponent& viewLight
        ){
            ++lightViewCount;
            if(viewLight.type == NWB::Impl::Scene::LightType::Directional){
                ++directionalLightCount;
                NWB_ECS_GRAPHICS_TEST_CHECK(context, viewLight.intensity() > 0.0f);
            }
            else if(viewLight.type == NWB::Impl::Scene::LightType::Point){
                ++pointLightCount;
                NWB_ECS_GRAPHICS_TEST_CHECK(context, entityId == pointEntityId);
                NWB_ECS_GRAPHICS_TEST_CHECK(context, viewTransform.position.x == 1.0f);
                NWB_ECS_GRAPHICS_TEST_CHECK(context, viewLight.range > 0.0f);
            }
            else{
                NWB_ECS_GRAPHICS_TEST_CHECK(context, false);
            }
        }
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, lightViewCount == 2);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLightCount == 1);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLightCount == 1);
}

static void TestMeshSystemResolvesMeshComponent(TestContext& context){
    TestWorld testWorld;
    auto& meshSystem = testWorld.world.addSystem<NWB::Impl::MeshSystem>(testWorld.world);

    auto entity = testWorld.world.createEntity();
    auto& mesh = entity.addComponent<NWB::Impl::MeshComponent>();
    mesh.mesh.virtualPath = Name("project/meshes/static_mesh");

    NWB::Core::Assets::AssetRef<NWB::Impl::Mesh> resolvedMesh;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, meshSystem.resolveMesh(entity.id(), resolvedMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedMesh.name() == mesh.mesh.name());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, meshSystem.findMesh(entity.id()) == &mesh);

    auto missingMeshEntity = testWorld.world.createEntity();
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !meshSystem.resolveMesh(missingMeshEntity.id(), resolvedMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !resolvedMesh.valid());
}

static u32 TestFloatBits(const f32 value){
    u32 bits = 0u;
    NWB_MEMCPY(&bits, sizeof(bits), &value, sizeof(value));
    return bits;
}

static void TestMaterialInstanceComponentSetters(TestContext& context){
    TestWorld testWorld;
    const Name materialInterface("project/material_interfaces/test_surface");
    auto entity = testWorld.world.createEntity();
    auto& materialInstance = entity.addComponent<NWB::Impl::MaterialInstanceComponent>(
        NWB::Tests::TestDetail::Arena(),
        materialInterface
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.materialInterface == materialInterface);

    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SetMaterialMutableFloat(
        testWorld.world,
        entity.id(),
        materialInterface,
        "runtime.fade_alpha",
        0.5f
    ));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.revision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[0u].parameterName == Name("runtime.fade_alpha"));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[0u].blockName == Name("runtime"));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[0u].fieldName == Name("fade_alpha"));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        materialInstance.overrides[0u].fieldType == NWB::Impl::MaterialLayoutFieldType::Float
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[0u].value.raw[0u] == TestFloatBits(0.5f));

    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SetMaterialMutableFloat(
        testWorld.world,
        entity.id(),
        materialInterface,
        "runtime.fade_alpha",
        0.75f
    ));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.revision == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[0u].value.raw[0u] == TestFloatBits(0.75f));

    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SetMaterialMutableFloat4(
        testWorld.world,
        entity.id(),
        materialInterface,
        "runtime.tint",
        Float4(1.0f, 0.5f, 0.25f, 0.125f)
    ));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides.size() == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.revision == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[1u].parameterName == Name("runtime.tint"));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        materialInstance.overrides[1u].fieldType == NWB::Impl::MaterialLayoutFieldType::Float4
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[1u].value.raw[0u] == TestFloatBits(1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[1u].value.raw[1u] == TestFloatBits(0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[1u].value.raw[2u] == TestFloatBits(0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, materialInstance.overrides[1u].value.raw[3u] == TestFloatBits(0.125f));
}

static void TestMaterialTypedByteRangeDeduplicatesContent(TestContext& context){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    using ByteVector = ::Vector<u8, NWB::Core::Alloc::ScratchArena>;
    using MaterialTypedByteContentKey = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentKey;
    using RangeMap = ::HashMap<
        MaterialTypedByteContentKey,
        NWB::Impl::ECSRenderDetail::MaterialTypedByteRange,
        NWB::Impl::ECSRenderDetail::MaterialTypedByteContentKeyHasher,
        ::EqualTo<MaterialTypedByteContentKey>,
        NWB::Core::Alloc::ScratchArena
    >;

    ByteVector uploadBytes{scratchArena};
    RangeMap ranges(
        0,
        NWB::Impl::ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        ::EqualTo<MaterialTypedByteContentKey>(),
        scratchArena
    );

    ByteVector firstBytes{scratchArena};
    firstBytes.push_back(1u);
    firstBytes.push_back(2u);
    firstBytes.push_back(3u);
    firstBytes.push_back(4u);

    NWB::Impl::ECSRenderDetail::MaterialTypedByteRange firstRange;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
        uploadBytes,
        ranges,
        firstBytes,
        firstRange
    ));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstRange.byteOffset == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstRange.byteCount == 4u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, uploadBytes.size() == 4u);

    ByteVector duplicateBytes{scratchArena};
    duplicateBytes.assign(firstBytes.begin(), firstBytes.end());
    NWB::Impl::ECSRenderDetail::MaterialTypedByteRange duplicateRange;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
        uploadBytes,
        ranges,
        duplicateBytes,
        duplicateRange
    ));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, duplicateRange.byteOffset == firstRange.byteOffset);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, duplicateRange.byteCount == firstRange.byteCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, uploadBytes.size() == 4u);

    ByteVector secondBytes{scratchArena};
    secondBytes.push_back(1u);
    secondBytes.push_back(2u);
    secondBytes.push_back(3u);
    secondBytes.push_back(5u);
    NWB::Impl::ECSRenderDetail::MaterialTypedByteRange secondRange;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
        uploadBytes,
        ranges,
        secondBytes,
        secondRange
    ));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondRange.byteOffset == 4u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondRange.byteCount == 4u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, uploadBytes.size() == 8u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges.size() == 2u);

    ByteVector emptyBytes{scratchArena};
    NWB::Impl::ECSRenderDetail::MaterialTypedByteRange emptyRange;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
        uploadBytes,
        ranges,
        emptyBytes,
        emptyRange
    ));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, emptyRange.byteOffset == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, emptyRange.byteCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, uploadBytes.size() == 8u);

    for(u32 instanceIndex = 0u; instanceIndex < 128u; ++instanceIndex){
        ByteVector overrideBytes{scratchArena};
        const u8 packedValue = static_cast<u8>(64u + (instanceIndex % 32u));
        overrideBytes.push_back(packedValue);
        overrideBytes.push_back(static_cast<u8>(packedValue + 1u));
        overrideBytes.push_back(static_cast<u8>(packedValue + 2u));
        overrideBytes.push_back(static_cast<u8>(packedValue + 3u));

        NWB::Impl::ECSRenderDetail::MaterialTypedByteRange stressRange;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
            uploadBytes,
            ranges,
            overrideBytes,
            stressRange
        ));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, stressRange.byteCount == 4u);
    }
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges.size() == 34u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, uploadBytes.size() == 136u);

    NWB::Impl::ECSRenderDetail::MaterialTypedInstanceRanges instanceRange;
    instanceRange.constantRange = firstRange;
    instanceRange.mutableRange = secondRange;
    const NWB::Impl::InstanceGpuData gpuData = NWB::Impl::ECSRenderDetail::BuildInstanceGpuData(nullptr, instanceRange);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, gpuData.translation.w == secondRange.byteOffset);
#if defined(NWB_DEBUG)
    NWB::Impl::ECSRenderDetail::MaterialTypedInstanceRangeVector instanceRanges{scratchArena};
    instanceRanges.push_back(instanceRange);
    NWB::Impl::ECSRenderDetail::AssertMaterialTypedUploadRanges(
        instanceRanges,
        uploadBytes
    );
#endif
}

static NWB::Impl::SkeletonJointMatrix MakeTranslationJointMatrix(const f32 x, const f32 y, const f32 z){
    NWB::Impl::SkeletonJointMatrix joint = NWB::Impl::MakeIdentitySkeletonJointMatrix();
    joint.rows[0].w = x;
    joint.rows[1].w = y;
    joint.rows[2].w = z;
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeZHalfTurnJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = NWB::Impl::MakeIdentitySkeletonJointMatrix();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeXHalfTurnJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = NWB::Impl::MakeIdentitySkeletonJointMatrix();
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeYHalfTurnJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = NWB::Impl::MakeIdentitySkeletonJointMatrix();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeZQuarterTurnJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = NWB::Impl::MakeIdentitySkeletonJointMatrix();
    joint.rows[0] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeNonUniformScaleJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = NWB::Impl::MakeIdentitySkeletonJointMatrix();
    joint.rows[0] = Float4(2.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinInfluence4 MakeSingleJointSkin(const u16 joint){
    NWB::Impl::SkinInfluence4 skin{};
    skin.joint[0] = joint;
    skin.weight[0] = 1.0f;
    return skin;
}

static void AssignSingleJointSkin(NWB::Impl::MeshSkinningRuntimeInstance& instance, const u16 joint){
    instance.meshClass = NWB::Core::Mesh::MeshClass::Skinned;
    instance.skin.assign(instance.restPositions.size(), MakeSingleJointSkin(joint));
    instance.skeletonJointCount = Max(instance.skeletonJointCount, static_cast<u32>(joint) + 1u);
}

static void AppendRuntimeVertex(NWB::Impl::MeshSkinningRuntimeInstance& instance, const Float3U& position, const f32 u){
    instance.restPositions.push_back(position);
    instance.restNormals.push_back(MakeHalf4U(0.0f, 0.0f, 1.0f, 0.0f));
    instance.restTangents.push_back(MakeHalf4U(1.0f, 0.0f, 0.0f, 1.0f));
    instance.uv0.push_back(Float2U(u, 0.0f));
    instance.colors.push_back(MakeHalf4U(1.0f, 1.0f, 1.0f, 1.0f));
}

static NWB::Impl::MeshSkinningRuntimeInstance MakeTriangleInstance(){
    NWB::Impl::MeshSkinningRuntimeInstance instance(NWB::Tests::TestDetail::Arena());
    instance.entity = NWB::Core::ECS::EntityID(1u, 0u);
    instance.handle.value = 42u;
    instance.editRevision = 7u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;
    AppendRuntimeVertex(instance, Float3U(-1.0f, -1.0f, 0.0f), 0.0f);
    AppendRuntimeVertex(instance, Float3U(1.0f, -1.0f, 0.0f), 0.5f);
    AppendRuntimeVertex(instance, Float3U(0.0f, 1.0f, 0.0f), 1.0f);

    instance.meshlets.push_back(NWB::Impl::MeshletDesc{
        0u,
        0u,
        0u,
        0u,
        NWB::Impl::PackMeshletCounts(3u, 1u, 3u, 3u),
    });
    instance.meshlets.back().skinBase = 0u;
    instance.meshletBounds.push_back(NWB::Impl::MeshletBounds{
        Float4U(0.0f, 0.0f, 0.0f, 2.0f),
        NWB::Impl::PackMeshletCone(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), 1.0f),
        0u,
    });
    Vector<NWB::Impl::MeshletPositionStreamRef> meshletPositionStreamRefs;
    Vector<NWB::Impl::MeshletAttributeStreamRef> meshletAttributeStreamRefs;
    NWB::Tests::AppendSequentialMeshletRefs(
        instance.restPositions.size(),
        meshletPositionStreamRefs,
        meshletAttributeStreamRefs,
        instance.meshletLocalVertexRefs
    );
    for(usize vertexIndex = 0u; vertexIndex < instance.restPositions.size(); ++vertexIndex){
        instance.attributeSkins.push_back(static_cast<u32>(vertexIndex));
    }
    for(const u32 index : MakeTriangleIndices())
        instance.meshletPrimitiveIndices.push_back(static_cast<u8>(index));
    const bool meshletRefsEncoded = NWB::Impl::EncodeMeshletRefDeltas(
        instance.meshlets,
        meshletPositionStreamRefs,
        meshletAttributeStreamRefs,
        instance.meshletPositionRefDeltas,
        instance.meshletAttributeRefDeltas,
        true,
        [](const usize, const tchar*){ return false; }
    );
    NWB_ASSERT(meshletRefsEncoded);
    static_cast<void>(meshletRefsEncoded);
    instance.meshletPositionRefCount = static_cast<u32>(meshletPositionStreamRefs.size());
    instance.meshletAttributeRefCount = static_cast<u32>(meshletAttributeStreamRefs.size());

    return instance;
}

static NWB::Impl::SkeletonJointMatrix MakeIdentityJointMatrix(){
    return MakeTranslationJointMatrix(0.0f, 0.0f, 0.0f);
}

static void CheckJointRotationQuaternion(
    TestContext& context,
    const NWB::Impl::SkeletonJointMatrix& joint,
    const f32 x,
    const f32 y,
    const f32 z,
    const f32 w){
    SIMDVector quaternion = QuaternionIdentity();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkeletonRuntime::TryBuildJointRotationQuaternion(
            LoadFloat(joint),
            quaternion
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetX(quaternion), x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetY(quaternion), y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetZ(quaternion), z));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetW(quaternion), w));
}

static void TestJointRotationQuaternionBuildsColumnVectorRotations(TestContext& context){
    constexpr f32 s_HalfSqrtTwo = 0.70710678118f;

    CheckJointRotationQuaternion(context, MakeIdentityJointMatrix(), 0.0f, 0.0f, 0.0f, 1.0f);
    CheckJointRotationQuaternion(context, MakeZQuarterTurnJointMatrix(), 0.0f, 0.0f, s_HalfSqrtTwo, s_HalfSqrtTwo);
    CheckJointRotationQuaternion(context, MakeXHalfTurnJointMatrix(), 1.0f, 0.0f, 0.0f, 0.0f);
    CheckJointRotationQuaternion(context, MakeYHalfTurnJointMatrix(), 0.0f, 1.0f, 0.0f, 0.0f);
    CheckJointRotationQuaternion(context, MakeZHalfTurnJointMatrix(), 0.0f, 0.0f, 1.0f, 0.0f);

    SIMDVector quaternion = QuaternionIdentity();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkeletonRuntime::TryBuildJointRotationQuaternion(
            LoadFloat(MakeNonUniformScaleJointMatrix()),
            quaternion
        )
    );
}

static NWB::Impl::SkeletonPoseComponent MakeTwoJointSkeletonPose(
    const NWB::Impl::SkeletonJointMatrix& rootJoint,
    const NWB::Impl::SkeletonJointMatrix& childJoint){
    NWB::Impl::SkeletonPoseComponent pose(NWB::Tests::TestDetail::Arena());
    pose.parentJoints.push_back(NWB::Impl::s_SkeletonRootParent);
    pose.parentJoints.push_back(0u);
    pose.localJoints.push_back(rootJoint);
    pose.localJoints.push_back(childJoint);
    return pose;
}

static void TestSkeletonPoseBuildsHierarchicalPalette(TestContext& context){
    NWB::Impl::SkeletonPoseComponent pose = MakeTwoJointSkeletonPose(
        MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f),
        MakeTranslationJointMatrix(0.0f, 2.0f, 0.0f)
    );

    Vector<NWB::Impl::SkeletonJointMatrix> resolvedJoints;
    u32 skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinningMode == NWB::Impl::SkeletonSkinningMode::LinearBlend);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedJoints.size() == 2u);
    if(resolvedJoints.size() == 2u){
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[0u].rows[0].w, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[0u].rows[1].w, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[1u].rows[0].w, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[1u].rows[1].w, 2.0f));
    }

    pose.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinningMode == NWB::Impl::SkeletonSkinningMode::DualQuaternion);

    pose.parentJoints[1u] = 1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    pose.parentJoints[1u] = 0u;
    pose.parentJoints.pop_back();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
}
static void TestMeshSkinningPayloadValidatesSkeletonAndPalette(TestContext& context){
    NWB::Impl::MeshSkinningRuntimeInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);
    instance.handle.value = 517u;

    NWB::Impl::SkeletonJointPaletteComponent joints(NWB::Tests::TestDetail::Arena());
    joints.joints.push_back(MakeIdentityJointMatrix());

    Vector<NWB::Impl::MeshSkinningInfluenceGpu> skinInfluences;
    Vector<NWB::Impl::SkeletonJointMatrix> jointMatrices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::MeshSkinningPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences.size() == instance.skin.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences[0u].joint[0u] == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(skinInfluences[0u].weight.x, 1.0f));

    instance.inverseBindMatrices.push_back(MakeTranslationJointMatrix(-0.25f, 0.0f, 0.0f));
    joints.joints[0u] = MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::MeshSkinningPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[0].w, 0.75f));
    joints.joints[0u] = MakeIdentityJointMatrix();

    NWB::Impl::MeshSkinningRuntimeInstance dualQuaternionInstance = MakeTriangleInstance();
    AssignSingleJointSkin(dualQuaternionInstance, 0u);
    dualQuaternionInstance.handle.value = instance.handle.value;
    joints.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeTranslationJointMatrix(2.0f, 4.0f, 6.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::MeshSkinningPayload::BuildSkinPayload(dualQuaternionInstance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[0].x, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[0].y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[0].z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[0].w, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[1].x, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[1].y, 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[1].z, 3.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[1].w, 0.0f));
    joints.skinningMode = NWB::Impl::SkeletonSkinningMode::LinearBlend;
    joints.joints[0u] = MakeIdentityJointMatrix();

#if defined(NWB_FINAL)
    CapturingLogger runtimeValidationLogger;
    NWB::Core::Common::LoggerRegistrationGuard runtimeValidationLoggerRegistrationGuard(runtimeValidationLogger);

    NWB::Impl::MeshSkinningRuntimeInstance outsidePalette = instance;
    outsidePalette.skin[0u] = MakeSingleJointSkin(1u);
    outsidePalette.skeletonJointCount = 2u;
    outsidePalette.inverseBindMatrices.clear();
    joints.joints.resize(1u, NWB::Impl::MakeIdentitySkeletonJointMatrix());
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::MeshSkinningPayload::BuildSkinPayload(outsidePalette, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::MeshSkinningRuntimeInstance nonAffineJoint = instance;
    joints.joints[0u] = MakeIdentityJointMatrix();
    joints.joints[0u].rows[0] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::MeshSkinningPayload::BuildSkinPayload(nonAffineJoint, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::MeshSkinningRuntimeInstance scaledDualQuaternionJoint = instance;
    scaledDualQuaternionJoint.inverseBindMatrices.clear();
    joints.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeNonUniformScaleJointMatrix();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::MeshSkinningPayload::BuildSkinPayload(
            scaledDualQuaternionJoint,
            &joints,
            skinInfluences,
            jointMatrices
        )
    );

    NWB_ECS_GRAPHICS_TEST_CHECK(context, runtimeValidationLogger.errorCount() == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette count")));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette entry 0 is not a finite invertible affine matrix"))
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        runtimeValidationLogger.sawErrorContaining(NWB_TEXT("failed dual-quaternion payload build"))
    );
#endif
}



#undef NWB_ECS_GRAPHICS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, RuntimeResourceNameBuilderMatchesFormattedSuffix){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestRuntimeResourceNameBuilderMatchesFormattedSuffix(nwbTestContext);
    EXPECT_EQ(nwbTestContext.failed, 0u);
}

TEST(EcsGraphics, LightComponents){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestLightComponents(nwbTestContext);
    EXPECT_EQ(nwbTestContext.failed, 0u);
}

TEST(EcsGraphics, MeshSystemResolvesMeshComponent){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestMeshSystemResolvesMeshComponent(nwbTestContext);
    EXPECT_EQ(nwbTestContext.failed, 0u);
}

TEST(EcsGraphics, MaterialInstanceComponentSetters){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestMaterialInstanceComponentSetters(nwbTestContext);
    EXPECT_EQ(nwbTestContext.failed, 0u);
}

TEST(EcsGraphics, MaterialTypedByteRangeDeduplicatesContent){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestMaterialTypedByteRangeDeduplicatesContent(nwbTestContext);
    EXPECT_EQ(nwbTestContext.failed, 0u);
}

TEST(EcsGraphics, JointRotationQuaternionBuildsColumnVectorRotations){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestJointRotationQuaternionBuildsColumnVectorRotations(nwbTestContext);
    EXPECT_EQ(nwbTestContext.failed, 0u);
}

TEST(EcsGraphics, SkeletonPoseBuildsHierarchicalPalette){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestSkeletonPoseBuildsHierarchicalPalette(nwbTestContext);
    EXPECT_EQ(nwbTestContext.failed, 0u);
}

TEST(EcsGraphics, MeshSkinningPayloadValidatesSkeletonAndPalette){
    NWB::Tests::TestContext nwbTestContext;
    __hidden_tests::TestMeshSkinningPayloadValidatesSkeletonAndPalette(nwbTestContext);
    EXPECT_EQ(nwbTestContext.failed, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

