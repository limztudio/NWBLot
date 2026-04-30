// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_graphics/deformable_debug_draw.h>
#include <impl/ecs_graphics/deformable_picking.h>
#include <impl/ecs_graphics/deformable_runtime_names.h>
#include <impl/ecs_graphics/deformable_surface_edit.h>
#include <impl/ecs_graphics/deformer_morph_payload.h>
#include <impl/ecs_graphics/deformer_skin_payload.h>

#include <tests/assets_graphics/deformable_test_helpers.h>
#include <tests/capturing_logger.h>
#include <tests/ecs_test_world.h>
#include <tests/test_context.h>

#include <core/assets/asset_manager.h>
#include <core/common/common.h>
#include <core/ecs/ecs.h>
#include <core/scene/transform_component.h>
#include <impl/assets_graphics/deformable_geometry_asset.h>
#include <impl/assets_graphics/geometry_asset.h>
#include <impl/assets_graphics/material_asset.h>

#include <logger/client/logger.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::MakeSourceSample;


#define NWB_ECS_GRAPHICS_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


static constexpr AStringView s_MockAccessoryGeometryPath = "project/meshes/mock_earring";
static constexpr AStringView s_MockAccessoryMaterialPath = "project/materials/mat_test";


static void TestRuntimeResourceNameBuilderMatchesFormattedSuffix(TestContext& context){
    const Name sourceName("project/meshes/deformable_source");
    const AString suffix = NWB::Impl::BuildRuntimeResourceSuffix(42u, 17u, "deformer_ranges");
    NWB_ECS_GRAPHICS_TEST_CHECK(context, suffix == AStringView(":runtime_42_revision_17_deformer_ranges"));

    const Name builtName = NWB::Impl::DeriveRuntimeResourceName(sourceName, 42u, 17u, "deformer_ranges");
    const Name formattedName = DeriveName(sourceName, AStringView(":runtime_42_revision_17_deformer_ranges"));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, builtName == formattedName);
}


static void* ECSTestAlloc(usize size){
    return NWB::Core::Alloc::CoreAlloc(size, "NWB::Tests::ECSGraphics::Alloc");
}

static void ECSTestFree(void* ptr){
    NWB::Core::Alloc::CoreFree(ptr, "NWB::Tests::ECSGraphics::Free");
}

static void* ECSTestAllocAligned(usize size, usize align){
    return NWB::Core::Alloc::CoreAllocAligned(size, align, "NWB::Tests::ECSGraphics::AllocAligned");
}

static void ECSTestFreeAligned(void* ptr){
    NWB::Core::Alloc::CoreFreeAligned(ptr, "NWB::Tests::ECSGraphics::FreeAligned");
}

using TestWorld = NWB::Tests::EcsTestWorld<&ECSTestAlloc, &ECSTestFree, &ECSTestAllocAligned, &ECSTestFreeAligned>;

template<typename AssetT>
class TestAssetCodec final : public NWB::Core::Assets::TypedAssetCodec<AssetT>{
public:
    virtual bool deserialize(
        const Name& virtualPath,
        const NWB::Core::Assets::AssetBytes& binary,
        UniquePtr<NWB::Core::Assets::IAsset>& outAsset
    )const override{
        static_cast<void>(binary);
        outAsset = MakeUnique<AssetT>(virtualPath);
        return true;
    }
};

class TestDisplacementTextureAssetCodec final
    : public NWB::Core::Assets::TypedAssetCodec<NWB::Impl::DeformableDisplacementTexture>
{
public:
    virtual bool deserialize(
        const Name& virtualPath,
        const NWB::Core::Assets::AssetBytes& binary,
        UniquePtr<NWB::Core::Assets::IAsset>& outAsset
    )const override{
        static_cast<void>(binary);

        auto texture = MakeUnique<NWB::Impl::DeformableDisplacementTexture>(virtualPath);
        texture->setSize(3u, 1u);

        Vector<Float4U> texels;
        texels.push_back(Float4U(0.25f, 0.0f, 0.0f, 0.0f));
        texels.push_back(Float4U(0.5f, 0.0f, 0.0f, 0.0f));
        texels.push_back(Float4U(1.0f, 0.0f, 0.0f, 0.0f));
        texture->setTexels(Move(texels));

        outAsset = Move(texture);
        return true;
    }
};

class TestWrongTypeGeometryAssetCodec final : public NWB::Core::Assets::IAssetCodec{
public:
    TestWrongTypeGeometryAssetCodec()
        : NWB::Core::Assets::IAssetCodec(NWB::Impl::Geometry::AssetTypeName())
    {}

    virtual bool deserialize(
        const Name& virtualPath,
        const NWB::Core::Assets::AssetBytes& binary,
        UniquePtr<NWB::Core::Assets::IAsset>& outAsset
    )const override{
        static_cast<void>(binary);
        outAsset = MakeUnique<NWB::Impl::Material>(virtualPath);
        return true;
    }
};

class TestAssetBinarySource final : public NWB::Core::Assets::IAssetBinarySource{
public:
    void addAvailablePath(const AStringView virtualPath){
        m_availableVirtualPaths.push_back(Name(virtualPath));
    }

    virtual bool readAssetBinary(const Name& virtualPath, NWB::Core::Assets::AssetBytes& outBinary)const override{
        outBinary.clear();
        for(const Name& availableVirtualPath : m_availableVirtualPaths){
            if(availableVirtualPath == virtualPath){
                outBinary.push_back(0u);
                return true;
            }
        }
        return false;
    }


private:
    Vector<Name> m_availableVirtualPaths;
};

struct TestAssetManager{
    NWB::Core::Alloc::CustomArena arena;
    NWB::Core::Assets::AssetRegistry registry;
    TestAssetBinarySource binarySource;
    NWB::Core::Assets::AssetManager manager;

    TestAssetManager()
        : arena(&ECSTestAlloc, &ECSTestFree, &ECSTestAllocAligned, &ECSTestFreeAligned)
        , registry(arena)
        , manager(arena, registry, binarySource)
    {
        const bool registeredGeometry =
            registry.registerCodec(MakeUnique<TestAssetCodec<NWB::Impl::Geometry>>());
        const bool registeredMaterial =
            registry.registerCodec(MakeUnique<TestAssetCodec<NWB::Impl::Material>>());
        const bool registeredDisplacementTexture =
            registry.registerCodec(MakeUnique<TestDisplacementTextureAssetCodec>());
        NWB_ASSERT(registeredGeometry && registeredMaterial && registeredDisplacementTexture);
        static_cast<void>(registeredGeometry);
        static_cast<void>(registeredMaterial);
        static_cast<void>(registeredDisplacementTexture);
    }
};

static bool SetAccessoryRecordAssetPaths(
    NWB::Impl::DeformableAccessoryAttachmentRecord& record,
    const AStringView geometryPath,
    const AStringView materialPath){
    record.geometry.virtualPath = Name(geometryPath);
    record.material.virtualPath = Name(materialPath);
    return
        record.geometryVirtualPathText.assign(geometryPath)
        && record.materialVirtualPathText.assign(materialPath)
    ;
}

static bool AppendAccessoryRecord(
    NWB::Impl::DeformableSurfaceEditState& state,
    const NWB::Impl::DeformableAccessoryAttachmentComponent& attachment,
    const NWB::Impl::DeformableSurfaceEditId anchorEditId,
    const AStringView geometryPath,
    const AStringView materialPath){
    NWB::Impl::DeformableAccessoryAttachmentRecord record;
    if(!SetAccessoryRecordAssetPaths(record, geometryPath, materialPath))
        return false;

    record.anchorEditId = anchorEditId;
    record.firstWallVertex = attachment.firstWallVertex;
    record.wallVertexCount = attachment.wallVertexCount;
    record.normalOffset = attachment.normalOffset();
    record.uniformScale = attachment.uniformScale();
    record.wallLoopParameter = attachment.wallLoopParameter();
    state.accessories.push_back(record);
    return true;
}

static bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = 0.00001f){
    const f32 difference = lhs > rhs ? lhs - rhs : rhs - lhs;
    return difference <= epsilon;
}

static u32 CountDebugLineKind(
    const NWB::Impl::DeformableSurfaceEditDebugSnapshot& snapshot,
    const NWB::Impl::DeformableDebugPrimitiveKind::Enum kind){
    u32 count = 0u;
    for(const NWB::Impl::DeformableDebugLine& line : snapshot.lines){
        if(line.kind == kind)
            ++count;
    }
    return count;
}

static u32 CountDebugPointKind(
    const NWB::Impl::DeformableSurfaceEditDebugSnapshot& snapshot,
    const NWB::Impl::DeformableDebugPrimitiveKind::Enum kind){
    u32 count = 0u;
    for(const NWB::Impl::DeformableDebugPoint& point : snapshot.points){
        if(point.kind == kind)
            ++count;
    }
    return count;
}

static bool FiniteFloat3(const Float3U& value){
    return
        IsFinite(value.x)
        && IsFinite(value.y)
        && IsFinite(value.z)
    ;
}

static bool FiniteFloat4(const Float4U& value){
    return
        IsFinite(value.x)
        && IsFinite(value.y)
        && IsFinite(value.z)
        && IsFinite(value.w)
    ;
}

static NWB::Impl::DeformableVertexRest MakeVertex(const f32 x, const f32 y, const f32 z, const f32 u = 0.0f){
    NWB::Impl::DeformableVertexRest vertex;
    vertex.position = Float3U(x, y, z);
    vertex.normal = Float3U(0.0f, 0.0f, 1.0f);
    vertex.tangent = Float4U(1.0f, 0.0f, 0.0f, 1.0f);
    vertex.uv0 = Float2U(u, 0.0f);
    vertex.color0 = Float4U(1.0f, 1.0f, 1.0f, 1.0f);
    return vertex;
}

static NWB::Impl::DeformableJointMatrix MakeTranslationJointMatrix(const f32 x, const f32 y, const f32 z){
    NWB::Impl::DeformableJointMatrix joint = NWB::Impl::MakeIdentityDeformableJointMatrix();
    joint.rows[3] = Float4(x, y, z, 1.0f);
    return joint;
}

static NWB::Impl::DeformableJointMatrix MakeZHalfTurnJointMatrix(){
    NWB::Impl::DeformableJointMatrix joint = NWB::Impl::MakeIdentityDeformableJointMatrix();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::DeformableJointMatrix MakeXHalfTurnJointMatrix(){
    NWB::Impl::DeformableJointMatrix joint = NWB::Impl::MakeIdentityDeformableJointMatrix();
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::DeformableJointMatrix MakeYHalfTurnJointMatrix(){
    NWB::Impl::DeformableJointMatrix joint = NWB::Impl::MakeIdentityDeformableJointMatrix();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::DeformableJointMatrix MakeZQuarterTurnJointMatrix(){
    NWB::Impl::DeformableJointMatrix joint = NWB::Impl::MakeIdentityDeformableJointMatrix();
    joint.rows[0] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::DeformableJointMatrix MakeNonUniformScaleJointMatrix(){
    NWB::Impl::DeformableJointMatrix joint = NWB::Impl::MakeIdentityDeformableJointMatrix();
    joint.rows[0] = Float4(2.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinInfluence4 MakeSingleJointSkin(const u16 joint){
    NWB::Impl::SkinInfluence4 skin{};
    skin.joint[0] = joint;
    skin.weight[0] = 1.0f;
    return skin;
}

static NWB::Impl::SkinInfluence4 MakeTwoJointSkin(const u16 joint0, const f32 weight0, const u16 joint1, const f32 weight1){
    NWB::Impl::SkinInfluence4 skin{};
    skin.joint[0] = joint0;
    skin.weight[0] = weight0;
    skin.joint[1] = joint1;
    skin.weight[1] = weight1;
    return skin;
}

static void AssignSingleJointSkin(NWB::Impl::DeformableRuntimeMeshInstance& instance, const u16 joint){
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin)
        skin = MakeSingleJointSkin(joint);
    instance.skeletonJointCount = Max(instance.skeletonJointCount, static_cast<u32>(joint) + 1u);
}

static f32 SkinWeightForJoint(const NWB::Impl::SkinInfluence4& skin, const u16 joint){
    f32 weight = 0.0f;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        if(skin.joint[influenceIndex] == joint)
            weight += skin.weight[influenceIndex];
    }
    return weight;
}

static f32 SkinWeightSum(const NWB::Impl::SkinInfluence4& skin){
    return skin.weight[0] + skin.weight[1] + skin.weight[2] + skin.weight[3];
}

static bool MorphDeltaPositionZForVertex(const NWB::Impl::DeformableMorph& morph, const u32 vertexId, f32& outDeltaZ){
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

static NWB::Impl::DeformableRuntimeMeshInstance MakeTetrahedronHoleInstance(){
    NWB::Impl::DeformableRuntimeMeshInstance instance;
    instance.entity = NWB::Core::ECS::EntityID(21u, 0u);
    instance.handle.value = 46u;
    instance.editRevision = 2u;
    instance.sourceTriangleCount = 1u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;
    instance.restVertices.push_back(MakeVertex(0.0f, 0.0f, 1.0f));
    instance.restVertices.push_back(MakeVertex(-0.942809f, 0.0f, -0.333333f));
    instance.restVertices.push_back(MakeVertex(0.471405f, 0.816497f, -0.333333f));
    instance.restVertices.push_back(MakeVertex(0.471405f, -0.816497f, -0.333333f));
    instance.indices.push_back(0u);
    instance.indices.push_back(1u);
    instance.indices.push_back(2u);
    instance.indices.push_back(0u);
    instance.indices.push_back(3u);
    instance.indices.push_back(1u);
    instance.indices.push_back(1u);
    instance.indices.push_back(3u);
    instance.indices.push_back(2u);
    instance.indices.push_back(2u);
    instance.indices.push_back(3u);
    instance.indices.push_back(0u);
    instance.sourceSamples.resize(instance.restVertices.size());
    for(NWB::Impl::SourceSample& sample : instance.sourceSamples)
        sample = MakeSourceSample(0u, 1.0f, 0.0f, 0.0f);
    return instance;
}

static NWB::Impl::DeformableDisplacementTexture MakeTestDisplacementTexture(
    const AStringView virtualPath,
    const Float4U& left,
    const Float4U& center,
    const Float4U& right){
    NWB::Impl::DeformableDisplacementTexture texture{ Name(virtualPath) };
    texture.setSize(3u, 1u);

    Vector<Float4U> texels;
    texels.push_back(left);
    texels.push_back(center);
    texels.push_back(right);
    texture.setTexels(Move(texels));
    return texture;
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

static NWB::Impl::DeformableRuntimeMeshInstance MakeGridHoleInstance(const u32 width, const u32 height){
    NWB::Impl::DeformableRuntimeMeshInstance instance;
    instance.entity = NWB::Core::ECS::EntityID(3u, 0u);
    instance.handle.value = 44u;
    instance.editRevision = 3u;
    instance.sourceTriangleCount = 1u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;

    const f32 originX = -0.5f * static_cast<f32>(width - 1u);
    const f32 originY = -0.5f * static_cast<f32>(height - 1u);
    for(u32 y = 0; y < height; ++y){
        for(u32 x = 0; x < width; ++x)
            instance.restVertices.push_back(MakeVertex(originX + static_cast<f32>(x), originY + static_cast<f32>(y), 0.0f));
    }

    auto vertexIndex = [width](const u32 x, const u32 y) -> u32{
        return (y * width) + x;
    };

    for(u32 y = 0; y < height - 1u; ++y){
        for(u32 x = 0; x < width - 1u; ++x){
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

static NWB::Impl::DeformableRuntimeMeshInstance MakeGridHoleInstance(){
    return MakeGridHoleInstance(4u, 4u);
}

static Float2U ExpectedPlanarSurfaceUv0(const Float3U& position){
    return Float2U((position.x * 0.37f) + 0.13f, (position.y * -0.21f) + 0.77f);
}

static void AssignPlanarSurfaceUv0(NWB::Impl::DeformableRuntimeMeshInstance& instance){
    for(NWB::Impl::DeformableVertexRest& vertex : instance.restVertices)
        vertex.uv0 = ExpectedPlanarSurfaceUv0(vertex.position);
}

static NWB::Impl::DeformableRuntimeMeshInstance MakeCurvedDepthGateHoleInstance(){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance(9u, 7u);
    for(NWB::Impl::DeformableVertexRest& vertex : instance.restVertices){
        const f32 x = vertex.position.x;
        vertex.position.z = -0.03f * x * x;
    }
    return instance;
}

static NWB::Impl::DeformableRuntimeMeshInstance MakeDepthSeparatedGridHoleInstance(){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance(6u, 6u);
    const usize frontVertexCount = instance.restVertices.size();
    const usize frontIndexCount = instance.indices.size();

    for(usize vertexIndex = 0u; vertexIndex < frontVertexCount; ++vertexIndex){
        NWB::Impl::DeformableVertexRest vertex = instance.restVertices[vertexIndex];
        vertex.position.z = -2.0f;
        instance.restVertices.push_back(vertex);
        instance.sourceSamples.push_back(instance.sourceSamples[vertexIndex]);
    }

    for(usize index = 0u; index < frontIndexCount; ++index)
        instance.indices.push_back(instance.indices[index] + static_cast<u32>(frontVertexCount));

    AssignSingleJointSkin(instance, 0u);
    return instance;
}

static void ConfigureMinimalMilestonePayload(NWB::Impl::DeformableRuntimeMeshInstance& instance){
    instance.skeletonJointCount = 2u;
    instance.skin.resize(instance.restVertices.size());
    for(usize vertexIndex = 0u; vertexIndex < instance.skin.size(); ++vertexIndex){
        instance.skin[vertexIndex] = (vertexIndex % 2u) == 0u
            ? MakeTwoJointSkin(0u, 0.75f, 1u, 0.25f)
            : MakeTwoJointSkin(0u, 0.25f, 1u, 0.75f)
        ;
    }

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("minimal_milestone_lift");
    const u32 morphVertices[] = { 7u, 8u, 13u, 14u };
    for(const u32 vertexId : morphVertices){
        NWB::Impl::DeformableMorphDelta delta{};
        delta.vertexId = vertexId;
        delta.deltaPosition = Float3U(0.0f, 0.0f, 0.1f);
        morph.deltas.push_back(delta);
    }
    instance.morphs.clear();
    instance.morphs.push_back(morph);

    instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::VectorObjectTexture;
    instance.displacement.texture.virtualPath = Name("tests/textures/minimal_milestone_vector_displacement");
    instance.displacement.amplitude = 0.2f;
    instance.displacement.bias = 0.0f;
}

static Float3U BarycentricPosition(
    const Float3U& a,
    const Float3U& b,
    const Float3U& c,
    const NWB::Impl::DeformableHitBarycentric& bary){
    SIMDVector position = VectorScale(LoadFloat(a), bary[0]);
    position = VectorMultiplyAdd(LoadFloat(b), VectorReplicate(bary[1]), position);
    position = VectorMultiplyAdd(LoadFloat(c), VectorReplicate(bary[2]), position);

    Float3U result;
    StoreFloat(position, &result);
    return result;
}

static NWB::Impl::DeformableHoleEditParams MakeHoleEditParams(
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const f32 bary0,
    const f32 bary1,
    const f32 bary2,
    const f32 radius,
    const f32 depth){
    NWB::Impl::DeformableHoleEditParams params;
    params.posedHit.entity = instance.entity;
    params.posedHit.runtimeMesh = instance.handle;
    params.posedHit.editRevision = instance.editRevision;
    params.posedHit.triangle = triangle;
    params.posedHit.bary[0] = bary0;
    params.posedHit.bary[1] = bary1;
    params.posedHit.bary[2] = bary2;
    const usize indexBase = static_cast<usize>(triangle) * 3u;
    const Float3U& a = instance.restVertices[instance.indices[indexBase + 0u]].position;
    const Float3U& b = instance.restVertices[instance.indices[indexBase + 1u]].position;
    const Float3U& c = instance.restVertices[instance.indices[indexBase + 2u]].position;
    params.posedHit.setPosition(BarycentricPosition(a, b, c, params.posedHit.bary));
    params.posedHit.setNormal(Float3U(0.0f, 0.0f, 1.0f));
    params.posedHit.setDistance(1.0f);
    static_cast<void>(NWB::Impl::ResolveDeformableRestSurfaceSample(
        instance,
        triangle,
        params.posedHit.bary,
        params.posedHit.restSample
    ));
    params.posedHit.editMaskFlags = NWB::Impl::ResolveDeformableTriangleEditMask(instance, triangle);
    params.radius = radius;
    params.ellipseRatio = 1.0f;
    params.depth = depth;
    const f32 footprintScale = IsFinite(radius) && Abs(radius) > 0.000001f ? (0.5f / Abs(radius)) : 1.0f;
    params.operatorFootprint.vertexCount = 4u;
    params.operatorFootprint.vertices[0u] = Float2U(-footprintScale, -footprintScale);
    params.operatorFootprint.vertices[1u] = Float2U(footprintScale, -footprintScale);
    params.operatorFootprint.vertices[2u] = Float2U(footprintScale, footprintScale);
    params.operatorFootprint.vertices[3u] = Float2U(-footprintScale, footprintScale);
    params.operatorProfile.sampleCount = 2u;
    params.operatorProfile.samples[0u].depth = 0.0f;
    params.operatorProfile.samples[0u].scale = 1.0f;
    params.operatorProfile.samples[0u].center = Float2U(0.0f, 0.0f);
    params.operatorProfile.samples[1u].depth = 1.0f;
    params.operatorProfile.samples[1u].scale = 1.0f;
    params.operatorProfile.samples[1u].center = Float2U(0.0f, 0.0f);
    return params;
}

static NWB::Impl::DeformableHoleEditParams MakeHoleEditParams(
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const f32 radius,
    const f32 depth){
    return MakeHoleEditParams(instance, triangle, 0.25f, 0.25f, 0.5f, radius, depth);
}

static NWB::Impl::DeformableHoleEditParams MakeGridHoleEditParams(const NWB::Impl::DeformableRuntimeMeshInstance& instance){
    return MakeHoleEditParams(instance, 8u, 0.48f, 0.25f);
}

static NWB::Impl::DeformableOperatorFootprint MakeBoxOperatorFootprint(){
    NWB::Impl::DeformableOperatorFootprint footprint;
    footprint.vertexCount = 4u;
    footprint.vertices[0u] = Float2U(-1.0f, -1.0f);
    footprint.vertices[1u] = Float2U(1.0f, -1.0f);
    footprint.vertices[2u] = Float2U(1.0f, 1.0f);
    footprint.vertices[3u] = Float2U(-1.0f, 1.0f);
    return footprint;
}

static NWB::Impl::DeformableOperatorFootprint MakeTriangleOperatorFootprint(){
    NWB::Impl::DeformableOperatorFootprint footprint;
    footprint.vertexCount = 3u;
    footprint.vertices[0u] = Float2U(1.0f, 0.0f);
    footprint.vertices[1u] = Float2U(-0.5f, 0.866025f);
    footprint.vertices[2u] = Float2U(-0.5f, -0.866025f);
    return footprint;
}

static NWB::Impl::DeformableOperatorProfile MakeBoxOperatorProfile(){
    NWB::Impl::DeformableOperatorProfile profile;
    profile.sampleCount = 2u;
    profile.samples[0u].depth = 0.0f;
    profile.samples[0u].scale = 1.0f;
    profile.samples[0u].center = Float2U(0.0f, 0.0f);
    profile.samples[1u].depth = 1.0f;
    profile.samples[1u].scale = 1.0f;
    profile.samples[1u].center = Float2U(0.0f, 0.0f);
    return profile;
}

static NWB::Impl::DeformableOperatorProfile MakeConeOperatorProfile(){
    NWB::Impl::DeformableOperatorProfile profile;
    profile.sampleCount = 2u;
    profile.samples[0u].depth = 0.0f;
    profile.samples[0u].scale = 1.0f;
    profile.samples[0u].center = Float2U(0.0f, 0.0f);
    profile.samples[1u].depth = 1.0f;
    profile.samples[1u].scale = 0.0f;
    profile.samples[1u].center = Float2U(0.0f, 0.0f);
    return profile;
}

static NWB::Impl::GeometryVertex MakeOperatorGeometryVertex(const f32 x, const f32 y, const f32 z){
    NWB::Impl::GeometryVertex vertex;
    vertex.position = Float3U(x, y, z);
    vertex.normal = Float3U(0.0f, 0.0f, 1.0f);
    vertex.color0 = Float4U(1.0f, 1.0f, 1.0f, 1.0f);
    return vertex;
}

static bool OperatorFootprintHasPoint(
    const NWB::Impl::DeformableOperatorFootprint& footprint,
    const f32 x,
    const f32 y
){
    constexpr f32 epsilon = 0.00001f;
    for(u32 i = 0u; i < footprint.vertexCount; ++i){
        if(Abs(footprint.vertices[i].x - x) <= epsilon && Abs(footprint.vertices[i].y - y) <= epsilon)
            return true;
    }
    return false;
}

static f32 DistanceXYFrom(const Float3U& point, const Float3U& center){
    const f32 dx = point.x - center.x;
    const f32 dy = point.y - center.y;
    return Sqrt((dx * dx) + (dy * dy));
}

struct CountedMeshEdge{
    u32 a = 0u;
    u32 b = 0u;
    u32 count = 0u;
};

static void RegisterCountedMeshEdge(Vector<CountedMeshEdge>& edges, const u32 a, const u32 b){
    if(a == b)
        return;

    const u32 lo = a < b ? a : b;
    const u32 hi = a < b ? b : a;
    for(CountedMeshEdge& edge : edges){
        if(edge.a == lo && edge.b == hi){
            ++edge.count;
            return;
        }
    }

    CountedMeshEdge edge;
    edge.a = lo;
    edge.b = hi;
    edge.count = 1u;
    edges.push_back(edge);
}

static bool VertexInRange(const u32 vertex, const u32 firstVertex, const u32 endVertex){
    return vertex >= firstVertex && vertex < endVertex;
}

static u32 CountUnweldedEdgesTouchingVertexRange(
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const u32 firstVertex,
    const u32 endVertex
){
    if(firstVertex > endVertex)
        return Limit<u32>::s_Max;
    if((instance.indices.size() % 3u) != 0u)
        return Limit<u32>::s_Max;

    Vector<CountedMeshEdge> edges;
    edges.reserve(instance.indices.size());
    for(usize indexBase = 0u; indexBase < instance.indices.size(); indexBase += 3u){
        const u32 i0 = instance.indices[indexBase + 0u];
        const u32 i1 = instance.indices[indexBase + 1u];
        const u32 i2 = instance.indices[indexBase + 2u];
        if(i0 >= instance.restVertices.size() || i1 >= instance.restVertices.size() || i2 >= instance.restVertices.size())
            return Limit<u32>::s_Max;

        RegisterCountedMeshEdge(edges, i0, i1);
        RegisterCountedMeshEdge(edges, i1, i2);
        RegisterCountedMeshEdge(edges, i2, i0);
    }

    u32 unweldedEdgeCount = 0u;
    for(const CountedMeshEdge& edge : edges){
        if(
            edge.count == 1u
            && (
                VertexInRange(edge.a, firstVertex, endVertex)
                || VertexInRange(edge.b, firstVertex, endVertex)
            )
        ){
            ++unweldedEdgeCount;
        }
    }
    return unweldedEdgeCount;
}

static void SimulateRuntimeMeshUpload(NWB::Impl::DeformableRuntimeMeshInstance& instance){
    instance.dirtyFlags = static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty
    );
}

static bool CommitPreviewedHole(
    NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const NWB::Impl::DeformableHoleEditParams& params,
    NWB::Impl::DeformableHoleEditResult* outResult = nullptr,
    NWB::Impl::DeformableSurfaceEditRecord* outRecord = nullptr){
    if(outResult)
        *outResult = NWB::Impl::DeformableHoleEditResult{};
    if(outRecord)
        *outRecord = NWB::Impl::DeformableSurfaceEditRecord{};

    NWB::Impl::DeformableSurfaceEditSession session;
    NWB::Impl::DeformableHolePreview preview;
    return
        NWB::Impl::BeginSurfaceEdit(instance, params.posedHit, session)
        && NWB::Impl::PreviewHole(instance, session, params, preview)
        && NWB::Impl::CommitHole(instance, session, params, outResult, outRecord)
    ;
}

static bool CommitRecordedHole(
    NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const f32 bary0,
    const f32 bary1,
    const f32 bary2,
    const f32 radius,
    const f32 depth,
    NWB::Impl::DeformableSurfaceEditState& state,
    NWB::Impl::DeformableHoleEditResult* outResult = nullptr){
    const NWB::Impl::DeformableHoleEditParams params = MakeHoleEditParams(instance, triangle, bary0, bary1, bary2, radius, depth);
    NWB::Impl::DeformableSurfaceEditSession session;
    NWB::Impl::DeformableHolePreview preview;
    NWB::Impl::DeformableHoleEditResult result;
    NWB::Impl::DeformableSurfaceEditRecord record;
    if(
        !NWB::Impl::BeginSurfaceEdit(instance, params.posedHit, session)
        || !NWB::Impl::PreviewHole(instance, session, params, preview)
        || !NWB::Impl::CommitHole(instance, session, params, &result, &record)
    )
        return false;

    state.edits.push_back(record);
    if(outResult)
        *outResult = result;
    SimulateRuntimeMeshUpload(instance);
    return true;
}

static bool CommitRecordedHole(
    NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const f32 radius,
    const f32 depth,
    NWB::Impl::DeformableSurfaceEditState& state,
    NWB::Impl::DeformableHoleEditResult* outResult = nullptr){
    return CommitRecordedHole(instance, triangle, 0.25f, 0.25f, 0.5f, radius, depth, state, outResult);
}

struct RecordedHoleAccessory{
    NWB::Impl::DeformableHoleEditResult hole;
    NWB::Impl::DeformableAccessoryAttachmentComponent attachment;
    NWB::Impl::DeformableSurfaceEditId editId = 0u;
};

static bool CommitRecordedHoleAccessory(
    NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const u32 triangle,
    const f32 radius,
    const f32 depth,
    const f32 normalOffset,
    const f32 uniformScale,
    NWB::Impl::DeformableSurfaceEditState& state,
    RecordedHoleAccessory& outRecorded){
    outRecorded = RecordedHoleAccessory{};
    const usize oldEditCount = state.edits.size();
    if(!CommitRecordedHole(instance, triangle, radius, depth, state, &outRecorded.hole))
        return false;
    if(state.edits.size() != oldEditCount + 1u)
        return false;

    outRecorded.editId = state.edits[oldEditCount].editId;
    if(!NWB::Impl::AttachAccessory(instance, outRecorded.hole, normalOffset, uniformScale, outRecorded.attachment))
        return false;

    return AppendAccessoryRecord(
        state,
        outRecorded.attachment,
        outRecorded.editId,
        s_MockAccessoryGeometryPath,
        s_MockAccessoryMaterialPath
    );
}

struct SurfaceEditReplayFixture{
    NWB::Impl::DeformableSurfaceEditState loadedState;
    TestAssetManager assets;
    TestWorld world;
    NWB::Impl::DeformableSurfaceEditReplayContext replayContext;
};

static bool PrepareSurfaceEditReplayFixture(
    TestContext& context,
    const NWB::Impl::DeformableSurfaceEditState& state,
    NWB::Impl::DeformableRuntimeMeshInstance& replayInstance,
    SurfaceEditReplayFixture& outFixture){
    NWB::Core::Assets::AssetBytes binary;
    const bool serialized = NWB::Impl::SerializeSurfaceEditState(state, binary);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, serialized);
    if(!serialized)
        return false;

    const bool deserialized = NWB::Impl::DeserializeSurfaceEditState(binary, outFixture.loadedState);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, deserialized);
    if(!deserialized)
        return false;

    outFixture.assets.binarySource.addAvailablePath(s_MockAccessoryGeometryPath);
    outFixture.assets.binarySource.addAvailablePath(s_MockAccessoryMaterialPath);

    auto targetEntity = outFixture.world.world.createEntity();
    replayInstance.entity = targetEntity.id();
    outFixture.replayContext.assetManager = &outFixture.assets.manager;
    outFixture.replayContext.world = &outFixture.world.world;
    outFixture.replayContext.targetEntity = replayInstance.entity;
    return true;
}

static bool ApplySurfaceEditReplayFixture(
    TestContext& context,
    const NWB::Impl::DeformableSurfaceEditState& state,
    NWB::Impl::DeformableRuntimeMeshInstance& replayInstance,
    SurfaceEditReplayFixture& outFixture,
    NWB::Impl::DeformableSurfaceEditReplayResult& outResult){
    if(!PrepareSurfaceEditReplayFixture(context, state, replayInstance, outFixture))
        return false;

    const bool applied = NWB::Impl::ApplySurfaceEditState(
        replayInstance,
        outFixture.loadedState,
        outFixture.replayContext,
        &outResult
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, applied);
    return applied;
}

static Float3U RestHitPosition(
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const NWB::Impl::DeformableHoleEditParams& params){
    const usize indexBase = static_cast<usize>(params.posedHit.triangle) * 3u;
    const Float3U& a = instance.restVertices[instance.indices[indexBase + 0u]].position;
    const Float3U& b = instance.restVertices[instance.indices[indexBase + 1u]].position;
    const Float3U& c = instance.restVertices[instance.indices[indexBase + 2u]].position;
    return BarycentricPosition(a, b, c, params.posedHit.bary);
}

static bool FindNearestUpFacingRestTriangle(
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const Float3U& target,
    u32& outTriangle){
    outTriangle = Limit<u32>::s_Max;
    f32 bestDistanceSquared = Limit<f32>::s_Max;
    const usize triangleCount = instance.indices.size() / 3u;
    if(triangleCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        const u32 i0 = instance.indices[indexBase + 0u];
        const u32 i1 = instance.indices[indexBase + 1u];
        const u32 i2 = instance.indices[indexBase + 2u];
        if(i0 >= instance.restVertices.size() || i1 >= instance.restVertices.size() || i2 >= instance.restVertices.size())
            return false;

        const SIMDVector p0 = LoadFloat(instance.restVertices[i0].position);
        const SIMDVector p1 = LoadFloat(instance.restVertices[i1].position);
        const SIMDVector p2 = LoadFloat(instance.restVertices[i2].position);
        const SIMDVector areaVector = Vector3Cross(VectorSubtract(p1, p0), VectorSubtract(p2, p0));
        if(VectorGetZ(areaVector) <= 0.0001f)
            continue;

        const SIMDVector centroid = VectorScale(VectorAdd(VectorAdd(p0, p1), p2), 1.0f / 3.0f);
        const SIMDVector delta = VectorSubtract(centroid, LoadFloat(target));
        const f32 distanceSquared = VectorGetX(Vector3LengthSq(delta));
        if(!IsFinite(distanceSquared) || distanceSquared >= bestDistanceSquared)
            continue;

        bestDistanceSquared = distanceSquared;
        outTriangle = static_cast<u32>(triangle);
    }

    return outTriangle != Limit<u32>::s_Max;
}

static bool FindSecondGridHoleTriangle(const NWB::Impl::DeformableRuntimeMeshInstance& instance, u32& outTriangle){
    return FindNearestUpFacingRestTriangle(instance, Float3U(1.0f, 0.0f, 0.0f), outTriangle);
}

struct RecordedTwoHoleAccessories{
    RecordedHoleAccessory first;
    RecordedHoleAccessory second;
    u32 secondTriangle = Limit<u32>::s_Max;
};

static bool CommitTwoRecordedHoleAccessories(
    TestContext& context,
    NWB::Impl::DeformableRuntimeMeshInstance& instance,
    NWB::Impl::DeformableSurfaceEditState& state,
    RecordedTwoHoleAccessories& outRecorded){
    outRecorded = RecordedTwoHoleAccessories{};
    const usize oldEditCount = state.edits.size();
    const usize oldAccessoryCount = state.accessories.size();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHoleAccessory(instance, 12u, 0.48f, 0.25f, 0.08f, 0.12f, state, outRecorded.first)
    );
    if(state.edits.size() != oldEditCount + 1u || state.accessories.size() != oldAccessoryCount + 1u)
        return false;

    NWB_ECS_GRAPHICS_TEST_CHECK(context, FindSecondGridHoleTriangle(instance, outRecorded.secondTriangle));
    if(outRecorded.secondTriangle == Limit<u32>::s_Max)
        return false;

    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHoleAccessory(instance, outRecorded.secondTriangle, 0.48f, 0.25f, 0.16f, 0.18f, state, outRecorded.second)
    );
    return state.edits.size() == oldEditCount + 2u && state.accessories.size() == oldAccessoryCount + 2u;
}

static void CheckHoleEditUnchanged(
    TestContext& context,
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const usize oldVertexCount,
    const usize oldIndexCount,
    const u32 oldRevision){
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.restVertices.size() == oldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.indices.size() == oldIndexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == oldRevision);
}

static void ResolveRestoredAccessoryAttachmentTransform(
    TestContext& context,
    NWB::Impl::DeformableRuntimeMeshInstance& editedInstance,
    const NWB::Impl::DeformableSurfaceEditState& state,
    const usize accessoryIndex,
    NWB::Core::Scene::TransformComponent& outTransform){
    NWB_ECS_GRAPHICS_TEST_CHECK(context, accessoryIndex < state.accessories.size());
    if(accessoryIndex >= state.accessories.size())
        return;

    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        (editedInstance.dirtyFlags & NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u
    );
    SimulateRuntimeMeshUpload(editedInstance);

    const NWB::Impl::DeformableAccessoryAttachmentRecord& savedAttachment = state.accessories[accessoryIndex];
    NWB::Impl::DeformableAccessoryAttachmentComponent restoredAttachment;
    restoredAttachment.targetEntity = editedInstance.entity;
    restoredAttachment.runtimeMesh = editedInstance.handle;
    restoredAttachment.anchorEditId = savedAttachment.anchorEditId;
    restoredAttachment.firstWallVertex = savedAttachment.firstWallVertex;
    restoredAttachment.wallVertexCount = savedAttachment.wallVertexCount;
    restoredAttachment.setNormalOffset(savedAttachment.normalOffset);
    restoredAttachment.setUniformScale(savedAttachment.uniformScale);
    restoredAttachment.setWallLoopParameter(savedAttachment.wallLoopParameter);

    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            editedInstance,
            NWB::Impl::DeformablePickingInputs{},
            restoredAttachment,
            outTransform
        )
    );
}

static void LeaveGridHoleInstanceUnchanged(NWB::Impl::DeformableRuntimeMeshInstance&){
}

static void LeaveGridHoleEditParamsUnchanged(NWB::Impl::DeformableHoleEditParams&){
}

template<typename MutateInstance, typename MutateParams>
static void CheckGridHoleEditRejectsMutation(TestContext& context, MutateInstance mutateInstance, MutateParams mutateParams){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    mutateInstance(instance);
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();
    const u32 oldRevision = instance.editRevision;
    NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
    mutateParams(params);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !CommitPreviewedHole(instance, params, &result));
    CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
}

template<typename MutateInstance>
static void CheckGridHoleEditRejectsInstanceMutation(TestContext& context, MutateInstance mutate){
    CheckGridHoleEditRejectsMutation(context, mutate, &LeaveGridHoleEditParamsUnchanged);
}

template<typename MutateParams>
static void CheckGridHoleEditRejectsParamMutation(TestContext& context, MutateParams mutate){
    CheckGridHoleEditRejectsMutation(context, &LeaveGridHoleInstanceUnchanged, mutate);
}

static void CheckAddedTrianglesResolveToSample(
    TestContext& context,
    const NWB::Impl::DeformableRuntimeMeshInstance& instance,
    const usize firstAddedTriangle,
    const u32 addedTriangleCount,
    const NWB::Impl::SourceSample& expectedSample){
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

static u32 ExpectedCappedHoleAddedVertexCount(const u32 wallVertexCount, const u32 wallLoopCutCount){
    return wallVertexCount * (wallLoopCutCount + 1u);
}

static u32 ExpectedCappedHoleAddedTriangleCount(const u32 wallVertexCount, const u32 wallLoopCutCount){
    return (wallVertexCount * 2u * (wallLoopCutCount + 1u)) + (wallVertexCount - 2u);
}

static void CheckRuntimeMeshPayloadValid(TestContext& context, const NWB::Impl::DeformableRuntimeMeshInstance& instance){
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !instance.restVertices.empty());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !instance.indices.empty());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, (instance.indices.size() % 3u) == 0u);
    if(instance.restVertices.empty() || instance.indices.empty() || (instance.indices.size() % 3u) != 0u)
        return;

    for(const NWB::Impl::DeformableVertexRest& vertex : instance.restVertices){
        NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat3(vertex.position));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat3(vertex.normal));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat4(vertex.tangent));
    }

    for(const u32 index : instance.indices)
        NWB_ECS_GRAPHICS_TEST_CHECK(context, index < instance.restVertices.size());

    const f32 bary[3] = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f };
    const usize triangleCount = instance.indices.size() / 3u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        const u32 i0 = instance.indices[indexBase + 0u];
        const u32 i1 = instance.indices[indexBase + 1u];
        const u32 i2 = instance.indices[indexBase + 2u];
        if(i0 >= instance.restVertices.size() || i1 >= instance.restVertices.size() || i2 >= instance.restVertices.size())
            continue;

        NWB_ECS_GRAPHICS_TEST_CHECK(context, i0 != i1);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, i0 != i2);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, i1 != i2);

        const SIMDVector p0 = LoadFloat(instance.restVertices[i0].position);
        const SIMDVector p1 = LoadFloat(instance.restVertices[i1].position);
        const SIMDVector p2 = LoadFloat(instance.restVertices[i2].position);
        const SIMDVector areaVector = Vector3Cross(VectorSubtract(p1, p0), VectorSubtract(p2, p0));
        const f32 areaLengthSquared = VectorGetX(Vector3LengthSq(areaVector));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(areaLengthSquared));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, areaLengthSquared > 0.00000001f);

        if(!instance.sourceSamples.empty()){
            NWB::Impl::SourceSample sample;
            const bool resolvedSample = NWB::Impl::ResolveDeformableRestSurfaceSample(
                instance,
                static_cast<u32>(triangle),
                bary,
                sample
            );
            NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedSample);
            if(!resolvedSample)
                continue;

            NWB_ECS_GRAPHICS_TEST_CHECK(context, sample.sourceTri < instance.sourceTriangleCount);
            NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(sample.bary[0]));
            NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(sample.bary[1]));
            NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(sample.bary[2]));
            NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[0] + sample.bary[1] + sample.bary[2], 1.0f));
        }
    }

    if(!instance.skin.empty()){
        NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.skin.size() == instance.restVertices.size());
        NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.skeletonJointCount != 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::DeformableValidation::ValidInverseBindMatrices(
                instance.inverseBindMatrices,
                instance.skeletonJointCount
            )
        );
        for(const NWB::Impl::SkinInfluence4& skin : instance.skin){
            NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightSum(skin), 1.0f));
            for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex)
                NWB_ECS_GRAPHICS_TEST_CHECK(context, skin.joint[influenceIndex] < instance.skeletonJointCount);
        }
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

static void TestMixedProvenanceRejectsAmbiguousRestTriangle(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeQuadMixedProvenanceInstance();
    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };

    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 1u, bary, sample));

    NWB::Impl::DeformablePickingRay ray;
    ray.setOrigin(Float3U(-0.5f, 0.5f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::RaycastDeformableRuntimeMesh(instance, NWB::Impl::DeformablePickingInputs{}, ray, hit)
    );
}

static void TestMixedProvenanceRejectsRuntimeTriangleOutsideSourceRange(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeOutOfRangeMixedProvenanceInstance();
    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };

    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 2u, bary, sample));

    NWB::Impl::DeformablePickingRay ray;
    ray.setOrigin(Float3U(4.0f, 0.0f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::RaycastDeformableRuntimeMesh(instance, NWB::Impl::DeformablePickingInputs{}, ray, hit)
    );
}

static void TestMixedProvenanceRejectsAfterTopologyChange(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeQuadMixedProvenanceInstance();
    instance.editRevision = 1u;

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 1u, bary, sample));
}

static void TestMissingProvenanceRejectsAfterTopologyChange(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.sourceTriangleCount = static_cast<u32>(instance.indices.size() / 3u);
    instance.sourceSamples.clear();
    instance.editRevision = 1u;

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
}

static void TestMissingProvenanceRejectsWhenSourceCountDiffers(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.sourceTriangleCount = static_cast<u32>((instance.indices.size() / 3u) + 1u);
    instance.sourceSamples.clear();
    instance.editRevision = 0u;

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
}

static void TestMissingProvenanceRejectsWithoutSourceCount(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.sourceTriangleCount = 0u;
    instance.sourceSamples.clear();
    instance.editRevision = 0u;

    const f32 bary[3] = { 0.25f, 0.25f, 0.5f };
    NWB::Impl::SourceSample sample;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::ResolveDeformableRestSurfaceSample(instance, 0u, bary, sample));
}

static void TestMixedProvenanceRejectsWhenEditedTriangleCountDiffers(TestContext& context){
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
    const f32 sampleBarySum = VectorGetX(Vector3Dot(VectorSet(sample.bary[0], sample.bary[1], sample.bary[2], 0.0f), s_SIMDOne));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sampleBarySum, 1.0f));
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
        instance.restVertices[1].position.x = Limit<f32>::s_QuietNaN;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.restVertices[1].normal = Float3U(0.0f, 0.0f, 0.0f);

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.restVertices[1].tangent = Float4U(0.0f, 0.0f, 1.0f, 1.0f);

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
    ray.setOrigin(Float3U(0.0f, 0.0f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

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
    const f32 hitBarySum = VectorGetX(Vector3Dot(LoadFloat(hit.bary.values), s_SIMDOne));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hitBarySum, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.distance(), 1.0f));
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
    ray.setOrigin(Float3U(0.0f, 0.0f, -1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, 1.0f));
    ray.setMinDistance(-2.0f);
    ray.setMaxDistance(1.0f);

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
    ray.setOrigin(Float3U(0.0f, 0.0f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

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
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0].rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].rows[3] = Float4(2.0f, 0.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.setOrigin(Float3U(2.0f, 0.0f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, hit));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.position.x, 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[0], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[1], 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(hit.restSample.bary[2], 0.5f));

    joints.joints[0].rows[0] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].rows[1] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].rows[3] = Float4(1.25f, -0.5f, 0.0f, 1.0f);

    ray.setOrigin(Float3U(1.25f, -0.4f, 1.0f));

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

static void TestPickingSkinAppliesInverseBindMatrix(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);
    instance.inverseBindMatrices.push_back(MakeTranslationJointMatrix(-0.25f, 0.0f, 0.0f));

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.push_back(MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f));

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == instance.restVertices.size());
    if(vertices.size() != instance.restVertices.size())
        return;

    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0u].position.x, -0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1u].position.x, 1.75f));
}

static void TestPickingSkinUsesNormalMatrixForNonUniformScale(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);

    static constexpr f32 s_InvSqrt2 = 0.7071067811865476f;
    for(NWB::Impl::DeformableVertexRest& vertex : instance.restVertices){
        vertex.normal = Float3U(s_InvSqrt2, s_InvSqrt2, 0.0f);
        vertex.tangent = Float4U(s_InvSqrt2, -s_InvSqrt2, 0.0f, 1.0f);
    }

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0].rows[0] = Float4(2.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].rows[3] = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == instance.restVertices.size());

    static constexpr f32 s_InvSqrt5 = 0.4472135954999579f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.x, -2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.x, 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].normal.x, s_InvSqrt5));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].normal.y, 2.0f * s_InvSqrt5));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].normal.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.x, 2.0f * s_InvSqrt5));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.y, -s_InvSqrt5));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.w, 1.0f));
}

static void TestPickingSkinBlendsTwoJoints(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.skeletonJointCount = 2u;
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin)
        skin = MakeTwoJointSkin(0u, 0.25f, 1u, 0.75f);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(2u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0].rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].rows[3] = Float4(2.0f, 0.0f, 0.0f, 1.0f);
    joints.joints[1].rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[1].rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[1].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[1].rows[3] = Float4(0.0f, 4.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.x, -0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.y, 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.x, 1.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.y, 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[2].position.x, 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[2].position.y, 4.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].normal.z, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.x, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.w, 1.0f));
}

static void TestPickingDualQuaternionSkinPreservesTwist(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.skeletonJointCount = 2u;
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin)
        skin = MakeTwoJointSkin(0u, 0.5f, 1u, 0.5f);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.push_back(MakeTranslationJointMatrix(0.0f, 0.0f, 0.0f));
    joints.joints.push_back(MakeZHalfTurnJointMatrix());

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    Vector<NWB::Impl::DeformableVertexRest> linearVertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, linearVertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, linearVertices.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(linearVertices[0u].position.x, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(linearVertices[0u].position.y, 0.0f));

    joints.skinningMode = NWB::Impl::DeformableSkinningMode::DualQuaternion;

    Vector<NWB::Impl::DeformableVertexRest> dualQuaternionVertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::BuildDeformablePickingVertices(instance, inputs, dualQuaternionVertices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dualQuaternionVertices.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(dualQuaternionVertices[0u].position.x, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(dualQuaternionVertices[0u].position.y, -1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(dualQuaternionVertices[1u].position.x, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(dualQuaternionVertices[1u].position.y, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(dualQuaternionVertices[0u].normal.z, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(dualQuaternionVertices[0u].tangent.x, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(dualQuaternionVertices[0u].tangent.y, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(dualQuaternionVertices[0u].tangent.w, 1.0f));
}

static void TestPickingDualQuaternionSkinRejectsScaledPalette(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.skinningMode = NWB::Impl::DeformableSkinningMode::DualQuaternion;
    joints.joints.push_back(MakeNonUniformScaleJointMatrix());

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
}

static void TestPickingRejectsSkinJointOutsidePalette(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 1u);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0].rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].rows[3] = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
}

static void TestPickingRejectsSkinJointOutsideSkeleton(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 1u);
    instance.skeletonJointCount = 1u;

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(2u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0].rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].rows[3] = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    joints.joints[1] = joints.joints[0];

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
}

static void TestPickingUsesEntityTransform(TestContext& context){
    const NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Core::Scene::TransformComponent transform;
    transform.position = Float4(3.0f, 0.0f, 0.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.transform = &transform;

    NWB::Impl::DeformablePickingRay ray;
    ray.setOrigin(Float3U(3.0f, 0.0f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

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
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0].rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.25f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.setOrigin(Float3U(0.0f, 0.0f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

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
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0].rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.25f);
    joints.joints[0].rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].rows[3] = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.setOrigin(Float3U(0.0f, 0.0f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, hit));
}

static void TestPickingRejectsSingularJointPalette(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0].rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].rows[1] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0].rows[3] = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
}

static void TestPickingRejectsUnusedNonAffineJointPalette(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(2u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0] = NWB::Impl::MakeIdentityDeformableJointMatrix();
    joints.joints[1] = NWB::Impl::MakeIdentityDeformableJointMatrix();
    joints.joints[1].rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.25f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.setOrigin(Float3U(0.0f, 0.0f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

    NWB::Impl::DeformablePosedHit hit;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::RaycastDeformableRuntimeMesh(instance, inputs, ray, hit));
}

static void TestPickingRejectsInvalidSkinWeights(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);
    instance.skin[0].weight[0] = Limit<f32>::s_QuietNaN;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
    );

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.jointPalette = &joints;

    NWB::Impl::DeformablePickingRay ray;
    ray.setOrigin(Float3U(0.0f, 0.0f, 1.0f));
    ray.setDirection(Float3U(0.0f, 0.0f, -1.0f));

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
    delta.deltaPosition = Float3U(0.0f, 0.0f, 1.0f);
    delta.deltaNormal = Float3U(0.0f, 0.0f, 0.0f);
    delta.deltaTangent = Float4U(0.0f, 0.0f, 0.0f, 0.0f);
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

static void TestPickingVerticesIncludeTextureDisplacement(TestContext& context){
    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
        instance.displacement.texture.virtualPath = Name("tests/textures/scalar_displacement");
        instance.displacement.amplitude = 2.0f;

        NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
            "tests/textures/scalar_displacement",
            Float4U(0.25f, 0.0f, 0.0f, 0.0f),
            Float4U(0.5f, 0.0f, 0.0f, 0.0f),
            Float4U(1.0f, 0.0f, 0.0f, 0.0f)
        );

        NWB::Impl::DeformablePickingInputs inputs;
        inputs.displacementTexture = &texture;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.z, 0.5f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.z, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[2].position.z, 2.0f));
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::VectorTangentTexture;
        instance.displacement.texture.virtualPath = Name("tests/textures/vector_displacement");
        instance.displacement.amplitude = 2.0f;

        NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
            "tests/textures/vector_displacement",
            Float4U(0.5f, 0.25f, 0.0f, 0.0f),
            Float4U(0.5f, 0.25f, 0.0f, 0.0f),
            Float4U(0.5f, 0.25f, 0.0f, 0.0f)
        );

        NWB::Impl::DeformablePickingInputs inputs;
        inputs.displacementTexture = &texture;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.x, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.y, -0.5f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.x, 2.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[2].position.y, 1.5f));
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::VectorObjectTexture;
        instance.displacement.texture.virtualPath = Name("tests/textures/object_vector_displacement");
        instance.displacement.amplitude = 2.0f;

        NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
            "tests/textures/object_vector_displacement",
            Float4U(0.25f, -0.125f, 0.5f, 0.0f),
            Float4U(0.25f, -0.125f, 0.5f, 0.0f),
            Float4U(0.25f, -0.125f, 0.5f, 0.0f)
        );

        NWB::Impl::DeformablePickingInputs inputs;
        inputs.displacementTexture = &texture;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.x, -0.5f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.y, -1.25f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.z, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.x, 1.5f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[2].position.y, 0.75f));
    }
}

static void TestPickingScalarTextureDisplacementUpdatesNormal(TestContext& context){
    static constexpr f32 s_InvSqrt2 = 0.7071067811865476f;

    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
    instance.displacement.texture.virtualPath = Name("tests/textures/scalar_displacement");
    instance.displacement.amplitude = 2.0f;

    NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
        "tests/textures/scalar_displacement",
        Float4U(0.0f, 0.0f, 0.0f, 0.0f),
        Float4U(0.5f, 0.0f, 0.0f, 0.0f),
        Float4U(1.0f, 0.0f, 0.0f, 0.0f)
    );

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.displacementTexture = &texture;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.z, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].normal.x, -s_InvSqrt2));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].normal.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].normal.z, s_InvSqrt2));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.x, s_InvSqrt2));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.z, s_InvSqrt2));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.w, 1.0f));

    instance = MakeTriangleInstance();
    instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
    instance.displacement.texture.virtualPath = Name("tests/textures/scalar_zero_center_slope");
    instance.displacement.amplitude = 2.0f;

    texture = MakeTestDisplacementTexture(
        "tests/textures/scalar_zero_center_slope",
        Float4U(0.0f, 0.0f, 0.0f, 0.0f),
        Float4U(0.0f, 0.0f, 0.0f, 0.0f),
        Float4U(1.0f, 0.0f, 0.0f, 0.0f)
    );

    inputs.displacementTexture = &texture;
    vertices.clear();
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].normal.x, -s_InvSqrt2));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].normal.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].normal.z, s_InvSqrt2));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.x, s_InvSqrt2));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.z, s_InvSqrt2));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.w, 1.0f));
}

static void TestPickingVectorTextureDisplacementUpdatesNormal(TestContext& context){
    static constexpr f32 s_InvSqrt2 = 0.7071067811865476f;

    const auto checkMode = [&](const u32 mode, const AStringView texturePath){
        const Name textureName(texturePath);
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.displacement.mode = mode;
        instance.displacement.texture.virtualPath = textureName;
        instance.displacement.amplitude = 2.0f;

        NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
            texturePath,
            Float4U(0.0f, 0.0f, 0.0f, 0.0f),
            Float4U(0.0f, 0.0f, 0.0f, 0.0f),
            Float4U(0.0f, 0.0f, 1.0f, 0.0f)
        );

        NWB::Impl::DeformablePickingInputs inputs;
        inputs.displacementTexture = &texture;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.z, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].normal.x, -s_InvSqrt2));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].normal.y, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].normal.z, s_InvSqrt2));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.x, s_InvSqrt2));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.y, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.z, s_InvSqrt2));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].tangent.w, 1.0f));
    };

    checkMode(NWB::Impl::DeformableDisplacementMode::VectorTangentTexture, "tests/textures/vector_tangent_normal");
    checkMode(NWB::Impl::DeformableDisplacementMode::VectorObjectTexture, "tests/textures/vector_object_normal");
}

static void TestDeformerCpuReferenceEvaluationModes(TestContext& context){
    const auto expectPosition = [&](
        const Vector<NWB::Impl::DeformableVertexRest>& vertices,
        const u32 vertexIndex,
        const f32 x,
        const f32 y,
        const f32 z){
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertexIndex < vertices.size());
        if(vertexIndex >= vertices.size())
            return;

        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[vertexIndex].position.x, x));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[vertexIndex].position.y, y));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[vertexIndex].position.z, z));
    };
    const auto addRaiseMorph = [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
        NWB::Impl::DeformableMorph morph;
        morph.name = Name("raise");
        NWB::Impl::DeformableMorphDelta delta{};
        delta.vertexId = 0u;
        delta.deltaPosition = Float3U(0.0f, 0.0f, 1.0f);
        morph.deltas.push_back(delta);
        instance.morphs.push_back(morph);
    };
    const auto makeScalarTexture = [](){
        return MakeTestDisplacementTexture(
            "tests/textures/cpu_reference_scalar_displacement",
            Float4U(0.25f, 0.0f, 0.0f, 0.0f),
            Float4U(0.5f, 0.0f, 0.0f, 0.0f),
            Float4U(1.0f, 0.0f, 0.0f, 0.0f)
        );
    };

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
        expectPosition(vertices, 0u, -1.0f, -1.0f, 0.0f);
        expectPosition(vertices, 1u, 1.0f, -1.0f, 0.0f);
        expectPosition(vertices, 2u, 0.0f, 1.0f, 0.0f);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        addRaiseMorph(instance);

        NWB::Impl::DeformableMorphWeightsComponent weights;
        weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("raise"), 0.5f });

        NWB::Impl::DeformablePickingInputs inputs;
        inputs.morphWeights = &weights;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
        expectPosition(vertices, 0u, -1.0f, -1.0f, 0.5f);
        expectPosition(vertices, 1u, 1.0f, -1.0f, 0.0f);
        expectPosition(vertices, 2u, 0.0f, 1.0f, 0.0f);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        AssignSingleJointSkin(instance, 0u);

        NWB::Impl::DeformableJointPaletteComponent joints;
        joints.joints.push_back(MakeTranslationJointMatrix(2.0f, 0.25f, 0.0f));

        NWB::Impl::DeformablePickingInputs inputs;
        inputs.jointPalette = &joints;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
        expectPosition(vertices, 0u, 1.0f, -0.75f, 0.0f);
        expectPosition(vertices, 1u, 3.0f, -0.75f, 0.0f);
        expectPosition(vertices, 2u, 2.0f, 1.25f, 0.0f);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
        instance.displacement.texture.virtualPath = Name("tests/textures/cpu_reference_scalar_displacement");
        instance.displacement.amplitude = 2.0f;

        NWB::Impl::DeformableDisplacementTexture texture = makeScalarTexture();
        NWB::Impl::DeformablePickingInputs inputs;
        inputs.displacementTexture = &texture;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
        expectPosition(vertices, 0u, -1.0f, -1.0f, 0.5f);
        expectPosition(vertices, 1u, 1.0f, -1.0f, 1.0f);
        expectPosition(vertices, 2u, 0.0f, 1.0f, 2.0f);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        AssignSingleJointSkin(instance, 0u);
        addRaiseMorph(instance);
        instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
        instance.displacement.texture.virtualPath = Name("tests/textures/cpu_reference_scalar_displacement");
        instance.displacement.amplitude = 2.0f;

        NWB::Impl::DeformableMorphWeightsComponent weights;
        weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("raise"), 0.5f });

        NWB::Impl::DeformableJointPaletteComponent joints;
        joints.joints.push_back(MakeTranslationJointMatrix(2.0f, 0.25f, 0.0f));

        NWB::Impl::DeformableDisplacementTexture texture = makeScalarTexture();
        NWB::Impl::DeformablePickingInputs inputs;
        inputs.morphWeights = &weights;
        inputs.jointPalette = &joints;
        inputs.displacementTexture = &texture;

        Vector<NWB::Impl::DeformableVertexRest> vertices;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
        expectPosition(vertices, 0u, 1.0f, -0.75f, 1.0f);
        expectPosition(vertices, 1u, 3.0f, -0.75f, 1.0f);
        expectPosition(vertices, 2u, 2.0f, 1.25f, 2.0f);
    }
}

static void TestPickingVerticesLoadTextureDisplacementFromAssetManager(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
    instance.displacement.texture.virtualPath = Name("tests/textures/scalar_displacement");
    instance.displacement.amplitude = 2.0f;

    TestAssetManager testAssets;
    testAssets.binarySource.addAvailablePath("tests/textures/scalar_displacement");

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.assetManager = &testAssets.manager;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.z, 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.z, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[2].position.z, 2.0f));
}

static void TestPickingTextureDisplacementCanBeDisabled(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
    instance.displacement.texture.virtualPath = Name("tests/textures/scalar_displacement");
    instance.displacement.amplitude = 4.0f;

    NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
        "tests/textures/scalar_displacement",
        Float4U(1.0f, 0.0f, 0.0f, 0.0f),
        Float4U(1.0f, 0.0f, 0.0f, 0.0f),
        Float4U(1.0f, 0.0f, 0.0f, 0.0f)
    );

    NWB::Impl::DeformableDisplacementComponent displacement;
    displacement.enabled = false;

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.displacement = &displacement;
    inputs.displacementTexture = &texture;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].position.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1].position.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[2].position.z, 0.0f));
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
        instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
        instance.displacement.texture.virtualPath = Name("tests/textures/missing_displacement");
        instance.displacement.amplitude = 1.0f;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, NWB::Impl::DeformablePickingInputs{}, vertices)
        );
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
        instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
        instance.displacement.texture.virtualPath = Name("tests/textures/scalar_displacement");
        instance.displacement.amplitude = 1.0f;

        NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
            "tests/textures/other_displacement",
            Float4U(1.0f, 0.0f, 0.0f, 0.0f),
            Float4U(1.0f, 0.0f, 0.0f, 0.0f),
            Float4U(1.0f, 0.0f, 0.0f, 0.0f)
        );

        NWB::Impl::DeformablePickingInputs inputs;
        inputs.displacementTexture = &texture;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices)
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
    instance.morphs[0].deltas[0].deltaPosition.x = Limit<f32>::s_QuietNaN;
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
    delta.deltaPosition = Float3U(Limit<f32>::s_Max, 0.0f, 0.0f);
    morph.deltas.push_back(delta);
    instance.morphs.push_back(morph);

    NWB::Impl::DeformableMorphWeightsComponent weights;
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("overflow"), 2.0f });

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.morphWeights = &weights;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
}

static void TestPickingRepairsOverflowedMorphFrame(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("frame_overflow");
    NWB::Impl::DeformableMorphDelta delta{};
    delta.vertexId = 0u;
    delta.deltaPosition = Float3U(0.0f, 0.0f, 0.0f);
    delta.deltaNormal = Float3U(Limit<f32>::s_Max, 0.0f, 0.0f);
    delta.deltaTangent = Float4U(Limit<f32>::s_Max, 0.0f, 0.0f, Limit<f32>::s_Max);
    morph.deltas.push_back(delta);
    instance.morphs.push_back(morph);

    NWB::Impl::DeformableMorphWeightsComponent weights;
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("frame_overflow"), 2.0f });

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.morphWeights = &weights;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat3(vertices[0].normal));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat4(vertices[0].tangent));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].normal.x, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].normal.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].normal.z, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.x, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0].tangent.w, 1.0f));
}

static void TestDeformerMorphPayloadPreblendsDuplicateWeightsAndMorphs(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformableMorph raiseMorph;
    raiseMorph.name = Name("raise");
    NWB::Impl::DeformableMorphDelta raiseDelta{};
    raiseDelta.vertexId = 1u;
    raiseDelta.deltaPosition = Float3U(2.0f, 0.0f, 0.0f);
    raiseDelta.deltaNormal = Float3U(0.0f, 0.0f, 1.0f);
    raiseDelta.deltaTangent = Float4U(0.0f, 0.5f, 0.0f, 0.0f);
    raiseMorph.deltas.push_back(raiseDelta);
    instance.morphs.push_back(raiseMorph);

    NWB::Impl::DeformableMorph liftMorph;
    liftMorph.name = Name("lift");
    NWB::Impl::DeformableMorphDelta liftDelta{};
    liftDelta.vertexId = 1u;
    liftDelta.deltaPosition = Float3U(-1.0f, 0.0f, 3.0f);
    liftDelta.deltaNormal = Float3U(0.25f, 0.0f, 0.0f);
    liftMorph.deltas.push_back(liftDelta);
    instance.morphs.push_back(liftMorph);

    NWB::Impl::DeformableMorphWeightsComponent weights;
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("raise"), 0.25f });
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("raise"), 0.5f });
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("lift"), 0.5f });

    Vector<NWB::Impl::DeformerSystem::DeformerVertexMorphRangeGpu> ranges;
    Vector<NWB::Impl::DeformerSystem::DeformerBlendedMorphDeltaGpu> deltas;
    usize signature = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerMorphPayload::BuildBlendedMorphPayload(instance, &weights, ranges, deltas, signature)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, deltas.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges[0u].deltaCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges[1u].firstDelta == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges[1u].deltaCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges[2u].deltaCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(deltas[0u].deltaPosition.x, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(deltas[0u].deltaPosition.z, 1.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(deltas[0u].deltaNormal.x, 0.125f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(deltas[0u].deltaNormal.z, 0.75f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(deltas[0u].deltaTangent.y, 0.375f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, signature != 0u);
}

static void TestDeformerMorphPayloadSignatureChangesWithWeights(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("raise");
    NWB::Impl::DeformableMorphDelta delta{};
    delta.vertexId = 1u;
    delta.deltaPosition = Float3U(2.0f, 0.0f, 0.0f);
    morph.deltas.push_back(delta);
    instance.morphs.push_back(morph);

    NWB::Impl::DeformableMorphWeightsComponent lightWeight;
    lightWeight.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("raise"), 0.25f });

    Vector<NWB::Impl::DeformerSystem::DeformerVertexMorphRangeGpu> lightRanges;
    Vector<NWB::Impl::DeformerSystem::DeformerBlendedMorphDeltaGpu> lightDeltas;
    usize lightSignature = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerMorphPayload::BuildBlendedMorphPayload(
            instance,
            &lightWeight,
            lightRanges,
            lightDeltas,
            lightSignature
        )
    );

    NWB::Impl::DeformableMorphWeightsComponent heavyWeight;
    heavyWeight.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("raise"), 0.75f });

    Vector<NWB::Impl::DeformerSystem::DeformerVertexMorphRangeGpu> heavyRanges;
    Vector<NWB::Impl::DeformerSystem::DeformerBlendedMorphDeltaGpu> heavyDeltas;
    usize heavySignature = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerMorphPayload::BuildBlendedMorphPayload(
            instance,
            &heavyWeight,
            heavyRanges,
            heavyDeltas,
            heavySignature
        )
    );

    NWB_ECS_GRAPHICS_TEST_CHECK(context, lightDeltas.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, heavyDeltas.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(lightDeltas[0u].deltaPosition.x, 0.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(heavyDeltas[0u].deltaPosition.x, 1.5f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, lightSignature != 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, heavySignature != 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, lightSignature != heavySignature);
}

static void TestDeformerMorphPayloadSignatureChangesWithEditRevision(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("raise");
    NWB::Impl::DeformableMorphDelta delta{};
    delta.vertexId = 1u;
    delta.deltaPosition = Float3U(2.0f, 0.0f, 0.0f);
    morph.deltas.push_back(delta);
    instance.morphs.push_back(morph);

    NWB::Impl::DeformableMorphWeightsComponent weight;
    weight.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("raise"), 0.5f });

    Vector<NWB::Impl::DeformerSystem::DeformerVertexMorphRangeGpu> baseRanges;
    Vector<NWB::Impl::DeformerSystem::DeformerBlendedMorphDeltaGpu> baseDeltas;
    usize baseSignature = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerMorphPayload::BuildBlendedMorphPayload(
            instance,
            &weight,
            baseRanges,
            baseDeltas,
            baseSignature
        )
    );

    instance.editRevision += 1u;

    Vector<NWB::Impl::DeformerSystem::DeformerVertexMorphRangeGpu> editedRanges;
    Vector<NWB::Impl::DeformerSystem::DeformerBlendedMorphDeltaGpu> editedDeltas;
    usize editedSignature = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerMorphPayload::BuildBlendedMorphPayload(
            instance,
            &weight,
            editedRanges,
            editedDeltas,
            editedSignature
        )
    );

    NWB_ECS_GRAPHICS_TEST_CHECK(context, baseDeltas.size() == editedDeltas.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, baseRanges.size() == editedRanges.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, baseSignature != 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedSignature != 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, baseSignature != editedSignature);
}

static void TestDeformerMorphPayloadBuildsSparseVertexRanges(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("sparse");
    NWB::Impl::DeformableMorphDelta firstDelta{};
    firstDelta.vertexId = 0u;
    firstDelta.deltaPosition = Float3U(0.0f, 0.0f, 1.0f);
    morph.deltas.push_back(firstDelta);
    NWB::Impl::DeformableMorphDelta secondDelta{};
    secondDelta.vertexId = 2u;
    secondDelta.deltaPosition = Float3U(0.0f, 2.0f, 0.0f);
    morph.deltas.push_back(secondDelta);
    instance.morphs.push_back(morph);

    NWB::Impl::DeformableMorphWeightsComponent weights;
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("sparse"), 1.0f });

    Vector<NWB::Impl::DeformerSystem::DeformerVertexMorphRangeGpu> ranges;
    Vector<NWB::Impl::DeformerSystem::DeformerBlendedMorphDeltaGpu> deltas;
    usize signature = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerMorphPayload::BuildBlendedMorphPayload(instance, &weights, ranges, deltas, signature)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, deltas.size() == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges[0u].firstDelta == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges[0u].deltaCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges[1u].deltaCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges[2u].firstDelta == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, ranges[2u].deltaCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(deltas[ranges[0u].firstDelta].deltaPosition.z, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(deltas[ranges[2u].firstDelta].deltaPosition.y, 2.0f));
}

static NWB::Impl::DeformableJointMatrix MakeIdentityJointMatrix(){
    return MakeTranslationJointMatrix(0.0f, 0.0f, 0.0f);
}

static void CheckJointRotationQuaternion(
    TestContext& context,
    const NWB::Impl::DeformableJointMatrix& joint,
    const f32 x,
    const f32 y,
    const f32 z,
    const f32 w){
    SIMDVector quaternion = QuaternionIdentity();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformableRuntime::TryBuildJointRotationQuaternion(
            NWB::Impl::DeformableRuntime::LoadJointMatrix(joint),
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
        !NWB::Impl::DeformableRuntime::TryBuildJointRotationQuaternion(
            NWB::Impl::DeformableRuntime::LoadJointMatrix(MakeNonUniformScaleJointMatrix()),
            quaternion
        )
    );
}

static NWB::Impl::DeformableSkeletonPoseComponent MakeTwoJointSkeletonPose(
    const NWB::Impl::DeformableJointMatrix& rootJoint,
    const NWB::Impl::DeformableJointMatrix& childJoint){
    NWB::Impl::DeformableSkeletonPoseComponent pose;
    pose.parentJoints.push_back(NWB::Impl::s_DeformableSkeletonRootParent);
    pose.parentJoints.push_back(0u);
    pose.localJoints.push_back(rootJoint);
    pose.localJoints.push_back(childJoint);
    return pose;
}

static void TestSkeletonPoseBuildsHierarchicalPalette(TestContext& context){
    NWB::Impl::DeformableSkeletonPoseComponent pose = MakeTwoJointSkeletonPose(
        MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f),
        MakeTranslationJointMatrix(0.0f, 2.0f, 0.0f)
    );

    Vector<NWB::Impl::DeformableJointMatrix> resolvedJoints;
    u32 skinningMode = NWB::Impl::DeformableSkinningMode::DualQuaternion;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformableRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinningMode == NWB::Impl::DeformableSkinningMode::LinearBlend);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedJoints.size() == 2u);
    if(resolvedJoints.size() == 2u){
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[0u].rows[3].x, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[0u].rows[3].y, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[1u].rows[3].x, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[1u].rows[3].y, 2.0f));
    }

    pose.skinningMode = NWB::Impl::DeformableSkinningMode::DualQuaternion;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformableRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinningMode == NWB::Impl::DeformableSkinningMode::DualQuaternion);

    pose.parentJoints[1u] = 1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::DeformableRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    pose.parentJoints[1u] = 0u;
    pose.parentJoints.pop_back();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::DeformableRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
}

static void TestPickingSkeletonPoseAppliesHierarchicalPalette(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 1u);
    instance.skeletonJointCount = 2u;

    NWB::Impl::DeformableSkeletonPoseComponent pose = MakeTwoJointSkeletonPose(
        MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f),
        MakeTranslationJointMatrix(0.0f, 2.0f, 0.0f)
    );

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.skeletonPose = &pose;

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformablePickingVertices(instance, inputs, vertices));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, vertices.size() == instance.restVertices.size());
    if(vertices.size() != instance.restVertices.size())
        return;

    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0u].position.x, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[0u].position.y, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1u].position.x, 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertices[1u].position.y, 1.0f));
}

static void TestDeformerSkinPayloadValidatesSkeletonAndPalette(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);
    instance.handle.value = 517u;

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.push_back(MakeIdentityJointMatrix());

    Vector<NWB::Impl::DeformerSystem::DeformerSkinInfluenceGpu> skinInfluences;
    Vector<NWB::Impl::DeformableJointMatrix> jointMatrices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerSkinPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences[0u].joint[0u] == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(skinInfluences[0u].weight.x, 1.0f));

    instance.inverseBindMatrices.push_back(MakeTranslationJointMatrix(-0.25f, 0.0f, 0.0f));
    joints.joints[0u] = MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerSkinPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[3].x, 0.75f));
    joints.joints[0u] = MakeIdentityJointMatrix();

    NWB::Impl::DeformableRuntimeMeshInstance dualQuaternionInstance = MakeTriangleInstance();
    AssignSingleJointSkin(dualQuaternionInstance, 0u);
    dualQuaternionInstance.handle.value = instance.handle.value;
    joints.skinningMode = NWB::Impl::DeformableSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeTranslationJointMatrix(2.0f, 4.0f, 6.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerSkinPayload::BuildSkinPayload(dualQuaternionInstance, &joints, skinInfluences, jointMatrices)
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
    joints.skinningMode = NWB::Impl::DeformableSkinningMode::LinearBlend;
    joints.joints[0u] = MakeIdentityJointMatrix();

#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::DeformableRuntimeMeshInstance missingSkeleton = instance;
    missingSkeleton.skeletonJointCount = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::DeformerSkinPayload::BuildSkinPayload(missingSkeleton, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences.empty());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.empty());

    NWB::Impl::DeformableRuntimeMeshInstance outsideSkeleton = instance;
    outsideSkeleton.skin[0u] = MakeSingleJointSkin(1u);
    outsideSkeleton.skeletonJointCount = 1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::DeformerSkinPayload::BuildSkinPayload(outsideSkeleton, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::DeformableRuntimeMeshInstance outsidePalette = instance;
    outsidePalette.skin[0u] = MakeSingleJointSkin(1u);
    outsidePalette.skeletonJointCount = 2u;
    outsidePalette.inverseBindMatrices.clear();
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::DeformerSkinPayload::BuildSkinPayload(outsidePalette, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::DeformableRuntimeMeshInstance nonAffineJoint = instance;
    joints.joints[0u] = MakeIdentityJointMatrix();
    joints.joints[0u].rows[3].w = 0.0f;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::DeformerSkinPayload::BuildSkinPayload(nonAffineJoint, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::DeformableRuntimeMeshInstance invalidInverseBind = instance;
    invalidInverseBind.inverseBindMatrices[0u].rows[3].w = 0.0f;
    joints.joints[0u] = MakeIdentityJointMatrix();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::DeformerSkinPayload::BuildSkinPayload(invalidInverseBind, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::DeformableRuntimeMeshInstance scaledDualQuaternionJoint = instance;
    scaledDualQuaternionJoint.inverseBindMatrices.clear();
    joints.skinningMode = NWB::Impl::DeformableSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeNonUniformScaleJointMatrix();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::DeformerSkinPayload::BuildSkinPayload(
            scaledDualQuaternionJoint,
            &joints,
            skinInfluences,
            jointMatrices
        )
    );

    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 6u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("has skin but no skeleton joint count")));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("outside skeleton joint count")));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("outside palette size")));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        logger.sawErrorContaining(NWB_TEXT("joint palette entry 0 is not a finite invertible affine matrix"))
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("inverse bind matrices are invalid")));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        logger.sawErrorContaining(NWB_TEXT("not rigid for dual-quaternion skinning"))
    );
#endif
}

static void TestRestSpaceHoleEditCreatesPerInstancePatch(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();

    NWB::Impl::DeformableMorph morph;
    morph.name = Name("boundary_lift");
    NWB::Impl::DeformableMorphDelta delta{};
    delta.vertexId = 5u;
    delta.deltaPosition = Float3U(0.0f, 0.0f, 0.2f);
    delta.deltaNormal = Float3U(0.0f, 0.0f, 0.0f);
    delta.deltaTangent = Float4U(0.0f, 0.0f, 0.0f, 0.0f);
    morph.deltas.push_back(delta);
    instance.morphs.push_back(morph);

    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
    const Float3U holeCenter = RestHitPosition(instance, params);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitPreviewedHole(instance, params, &result));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.removedTriangleCount != 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.wallVertexCount >= 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        result.addedVertexCount == ExpectedCappedHoleAddedVertexCount(result.wallVertexCount, result.wallLoopCutCount)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        result.addedTriangleCount == ExpectedCappedHoleAddedTriangleCount(result.wallVertexCount, result.wallLoopCutCount)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.editRevision == 4u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == 4u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        (instance.dirtyFlags & NWB::Impl::RuntimeMeshDirtyFlag::All) == NWB::Impl::RuntimeMeshDirtyFlag::All
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.restVertices.size() > oldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.indices.size() > oldIndexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.skin.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.sourceSamples.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.morphs.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.morphs[0].deltas.size() > 1u);

    u32 bottomDepthVertexCount = 0u;
    for(usize vertexIndex = oldVertexCount; vertexIndex < instance.restVertices.size(); ++vertexIndex){
        const NWB::Impl::DeformableVertexRest& vertex = instance.restVertices[vertexIndex];
        if(NearlyEqual(vertex.position.z, -0.25f))
            ++bottomDepthVertexCount;

        NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat4(vertex.tangent));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, Abs(Abs(vertex.tangent.w) - 1.0f) <= 0.001f);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(vertex.uv0.x));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(vertex.uv0.y));
        const f32 inwardDot = VectorGetX(
            Vector3Dot(
                LoadFloat(vertex.normal),
                VectorSubtract(LoadFloat(holeCenter), LoadFloat(vertex.position))
            )
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(inwardDot));
    }
    NWB_ECS_GRAPHICS_TEST_CHECK(context, bottomDepthVertexCount >= result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountUnweldedEdgesTouchingVertexRange(
            instance,
            result.firstWallVertex,
            static_cast<u32>(instance.restVertices.size())
        ) == 0u
    );
    for(usize vertexOffset = 0u; vertexOffset < static_cast<usize>(result.wallVertexCount); ++vertexOffset){
        const f32 weldedCapNormalZ = instance.restVertices[result.firstWallVertex + vertexOffset].normal.z;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, weldedCapNormalZ > 0.0f && weldedCapNormalZ < 1.0f);
    }
    for(const u32 index : instance.indices)
        NWB_ECS_GRAPHICS_TEST_CHECK(context, index < instance.restVertices.size());

    const usize wallIndexBase = instance.indices.size() - (static_cast<usize>(result.addedTriangleCount) * 3u);
    const usize wallTriangleCount = static_cast<usize>(result.wallVertexCount) * 2u * (result.wallLoopCutCount + 1u);
    for(usize edgeIndex = 0u; edgeIndex < static_cast<usize>(result.wallVertexCount); ++edgeIndex){
        const usize nextEdgeIndex = (edgeIndex + 1u) % static_cast<usize>(result.wallVertexCount);
        const usize indexBase = wallIndexBase + (edgeIndex * 6u);
        const u32 innerA = result.firstWallVertex + static_cast<u32>(edgeIndex);
        const u32 innerB = result.firstWallVertex + static_cast<u32>(nextEdgeIndex);
        const u32 rimA = instance.indices[indexBase + 0u];
        const u32 rimB = instance.indices[indexBase + 1u];
        NWB_ECS_GRAPHICS_TEST_CHECK(context, rimA < result.firstWallVertex);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, rimB < result.firstWallVertex);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.indices[indexBase + 2u] == innerB);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.indices[indexBase + 3u] == rimA);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.indices[indexBase + 4u] == innerB);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.indices[indexBase + 5u] == innerA);

        const NWB::Impl::DeformableVertexRest& rimVertex = instance.restVertices[rimA];
        const NWB::Impl::DeformableVertexRest& innerVertex = instance.restVertices[innerA];
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimVertex.position.x, innerVertex.position.x));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimVertex.position.y, innerVertex.position.y));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimVertex.position.z, innerVertex.position.z + params.depth));
    }

    const usize capIndexBase = wallIndexBase + (wallTriangleCount * 3u);
    const u32 firstCapVertex = result.firstWallVertex;
    const usize capTriangleCount = static_cast<usize>(result.addedTriangleCount) - wallTriangleCount;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, capTriangleCount == result.wallVertexCount - 2u);
    for(usize capTriangle = 0u; capTriangle < capTriangleCount; ++capTriangle){
        const usize indexBase = capIndexBase + (capTriangle * 3u);
        const u32 i0 = instance.indices[indexBase + 0u];
        const u32 i1 = instance.indices[indexBase + 1u];
        const u32 i2 = instance.indices[indexBase + 2u];
        NWB_ECS_GRAPHICS_TEST_CHECK(context, i0 >= firstCapVertex && i0 < firstCapVertex + result.wallVertexCount);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, i1 >= firstCapVertex && i1 < firstCapVertex + result.wallVertexCount);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, i2 >= firstCapVertex && i2 < firstCapVertex + result.wallVertexCount);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, i0 != i1 && i0 != i2 && i1 != i2);

        const SIMDVector p0 = LoadFloat(instance.restVertices[i0].position);
        const SIMDVector p1 = LoadFloat(instance.restVertices[i1].position);
        const SIMDVector p2 = LoadFloat(instance.restVertices[i2].position);
        const SIMDVector faceNormal = Vector3Cross(VectorSubtract(p1, p0), VectorSubtract(p2, p0));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, VectorGetX(faceNormal) == VectorGetX(faceNormal));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, VectorGetZ(faceNormal) > 0.0f);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[i0].position.z, -params.depth));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[i1].position.z, -params.depth));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(instance.restVertices[i2].position.z, -params.depth));
    }

    f32 rimDeltaZ = 0.0f;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        MorphDeltaPositionZForVertex(instance.morphs[0], 5u, rimDeltaZ)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimDeltaZ, 0.2f));
}

static void TestRestSpaceHoleEditTransfersAndInpaintsWallAttributes(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
    const u16 joint0 = 3u;
    const u16 joint1 = 5u;
    const u16 joint2 = 7u;
    const u16 joint3 = 11u;

    instance.skeletonJointCount = 12u;
    instance.skin[5u] = MakeSingleJointSkin(joint0);
    instance.skin[6u] = MakeSingleJointSkin(joint1);
    instance.skin[10u] = MakeSingleJointSkin(joint2);
    instance.skin[9u] = MakeSingleJointSkin(joint3);
    instance.restVertices[5u].color0 = Float4U(1.0f, 0.0f, 0.0f, 1.0f);
    instance.restVertices[6u].color0 = Float4U(0.0f, 1.0f, 0.0f, 1.0f);
    instance.restVertices[10u].color0 = Float4U(1.0f, 1.0f, 0.0f, 1.0f);
    instance.restVertices[9u].color0 = Float4U(0.0f, 0.0f, 1.0f, 1.0f);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitPreviewedHole(instance, params, &result));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.addedVertexCount != 0u);

    const NWB::Impl::SkinInfluence4& rimSkin0 = instance.skin[5u];
    const NWB::Impl::SkinInfluence4& innerSkin0 = instance.skin[result.firstWallVertex];
    const NWB::Impl::SkinInfluence4& rimSkin1 = instance.skin[6u];
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(rimSkin0, joint0), 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightSum(innerSkin0), 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightForJoint(rimSkin1, joint1), 1.0f));
    const Float4U& rimColor0 = instance.restVertices[5u].color0;
    const Float4U& innerColor0 = instance.restVertices[result.firstWallVertex].color0;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimColor0.x, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimColor0.y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rimColor0.z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(innerColor0.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(innerColor0.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(innerColor0.z));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(innerColor0.w, 1.0f));
}

static void TestRestSpaceHoleEditWallTrianglesKeepRecoverableProvenance(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    instance.sourceTriangleCount = 2u;
    instance.sourceSamples[9u] = MakeSourceSample(1u, 1.0f, 0.0f, 0.0f);
    const usize oldIndexCount = instance.indices.size();

    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitPreviewedHole(instance, params, &result));

    static_cast<void>(oldIndexCount);
    const usize firstWallTriangle = (instance.indices.size() / 3u) - result.addedTriangleCount;
    CheckAddedTrianglesResolveToSample(
        context,
        instance,
        firstWallTriangle,
        result.addedTriangleCount,
        params.posedHit.restSample
    );
}

static void TestSurfaceEditMutatesOnlyTargetRuntimeInstance(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanSource = MakeGridHoleInstance();
    cleanSource.editRevision = 0u;
    cleanSource.source.virtualPath = Name("tests/deformable/shared_source");

    NWB::Impl::DeformableRuntimeMeshInstance firstInstance = cleanSource;
    firstInstance.entity = NWB::Core::ECS::EntityID(31u, 0u);
    firstInstance.handle.value = 631u;

    NWB::Impl::DeformableRuntimeMeshInstance secondInstance = cleanSource;
    secondInstance.entity = NWB::Core::ECS::EntityID(32u, 0u);
    secondInstance.handle.value = 632u;

    const usize secondOldVertexCount = secondInstance.restVertices.size();
    const usize secondOldIndexCount = secondInstance.indices.size();
    const u32 secondOldRevision = secondInstance.editRevision;
    const NWB::Impl::RuntimeMeshHandle secondOldHandle = secondInstance.handle;

    NWB::Impl::DeformableSurfaceEditState firstState;
    NWB::Impl::DeformableHoleEditResult firstResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHole(firstInstance, 8u, 0.48f, 0.25f, firstState, &firstResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstResult.editRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstState.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstInstance.editRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstInstance.restVertices.size() > secondOldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstInstance.indices.size() > secondOldIndexCount);

    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstInstance.source == secondInstance.source);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondInstance.handle == secondOldHandle);
    CheckHoleEditUnchanged(context, secondInstance, secondOldVertexCount, secondOldIndexCount, secondOldRevision);

    NWB::Impl::DeformableSurfaceEditState secondState;
    NWB::Impl::DeformableHoleEditResult secondResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHole(secondInstance, 8u, 0.48f, 0.25f, secondState, &secondResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondResult.editRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondState.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondInstance.editRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondInstance.handle == secondOldHandle);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondInstance.restVertices.size() == firstInstance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondInstance.indices.size() == firstInstance.indices.size());
}

static void TestSurfaceEditMasksPreviewAndCommit(TestContext& context){
    auto assignEditableMasks = [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
        const usize triangleCount = instance.indices.size() / 3u;
        instance.editMaskPerTriangle.resize(
            triangleCount,
            NWB::Impl::DeformableEditMaskFlag::Editable
        );
    };

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        assignEditableMasks(instance);
        const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        instance.editMaskPerTriangle[params.posedHit.triangle] = NWB::Impl::DeformableEditMaskFlag::Restricted;
        const NWB::Impl::DeformableHoleEditParams maskedParams = MakeGridHoleEditParams(instance);

        NWB::Impl::DeformableSurfaceEditSession session;
        NWB::Impl::DeformableHolePreview preview;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(instance, maskedParams.posedHit, session));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(instance, session, maskedParams, preview));
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            preview.editPermission == NWB::Impl::DeformableSurfaceEditPermission::Restricted
        );
        NWB::Impl::DeformableSurfaceEditDebugSnapshot restrictedMaskSnapshot;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot(
                instance,
                &session,
                &preview,
                nullptr,
                restrictedMaskSnapshot
            )
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, restrictedMaskSnapshot.restrictedTriangleCount == 1u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, restrictedMaskSnapshot.forbiddenTriangleCount == 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, restrictedMaskSnapshot.restrictedMaskPointCount == 1u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, restrictedMaskSnapshot.repairMaskPointCount == 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, restrictedMaskSnapshot.forbiddenMaskPointCount == 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, restrictedMaskSnapshot.points.size() == 2u);
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            CountDebugPointKind(restrictedMaskSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::RestrictedMask) == 1u
        );
        AString restrictedMaskDump;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::BuildDeformableSurfaceEditDebugDump(restrictedMaskSnapshot, restrictedMaskDump)
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            restrictedMaskDump.find("mask_markers(restricted=1 repair=0 forbidden=0)") != AString::npos
        );

        NWB::Impl::DeformableHoleEditResult result;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(instance, session, maskedParams, &result));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editMaskPerTriangle.size() == instance.indices.size() / 3u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, result.addedTriangleCount != 0u);
        for(u32 triangleOffset = 0u; triangleOffset < result.addedTriangleCount; ++triangleOffset){
            const usize triangleIndex = instance.editMaskPerTriangle.size() - result.addedTriangleCount + triangleOffset;
            NWB_ECS_GRAPHICS_TEST_CHECK(
                context,
                (instance.editMaskPerTriangle[triangleIndex] & NWB::Impl::DeformableEditMaskFlag::Restricted) != 0u
            );
        }
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        assignEditableMasks(instance);
        const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        instance.editMaskPerTriangle[params.posedHit.triangle] = NWB::Impl::DeformableEditMaskFlag::Forbidden;
        const usize oldVertexCount = instance.restVertices.size();
        const usize oldIndexCount = instance.indices.size();
        const u32 oldRevision = instance.editRevision;
        const NWB::Impl::DeformableHoleEditParams maskedParams = MakeGridHoleEditParams(instance);

        NWB::Impl::DeformableSurfaceEditSession session;
        NWB::Impl::DeformableHolePreview preview;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(instance, maskedParams.posedHit, session));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(instance, session, maskedParams, preview));
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            preview.editPermission == NWB::Impl::DeformableSurfaceEditPermission::Forbidden
        );
        NWB::Impl::DeformableSurfaceEditDebugSnapshot forbiddenMaskSnapshot;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot(
                instance,
                &session,
                &preview,
                nullptr,
                forbiddenMaskSnapshot
            )
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, forbiddenMaskSnapshot.restrictedTriangleCount == 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, forbiddenMaskSnapshot.forbiddenTriangleCount == 1u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, forbiddenMaskSnapshot.restrictedMaskPointCount == 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, forbiddenMaskSnapshot.repairMaskPointCount == 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, forbiddenMaskSnapshot.forbiddenMaskPointCount == 1u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, forbiddenMaskSnapshot.points.size() == 2u);
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            CountDebugPointKind(forbiddenMaskSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::ForbiddenMask) == 1u
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitHole(instance, session, maskedParams));
        CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    }

    {
        NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
        assignEditableMasks(instance);
        const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
        instance.editMaskPerTriangle[params.posedHit.triangle] = static_cast<NWB::Impl::DeformableEditMaskFlags>(
            NWB::Impl::DeformableEditMaskFlag::Editable | NWB::Impl::DeformableEditMaskFlag::RequiresRepair
        );
        const NWB::Impl::DeformableHoleEditParams maskedParams = MakeGridHoleEditParams(instance);

        NWB::Impl::DeformableSurfaceEditSession session;
        NWB::Impl::DeformableHolePreview preview;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(instance, maskedParams.posedHit, session));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(instance, session, maskedParams, preview));

        NWB::Impl::DeformableSurfaceEditDebugSnapshot repairMaskSnapshot;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot(
                instance,
                &session,
                &preview,
                nullptr,
                repairMaskSnapshot
            )
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, repairMaskSnapshot.restrictedTriangleCount == 1u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, repairMaskSnapshot.forbiddenTriangleCount == 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, repairMaskSnapshot.restrictedMaskPointCount == 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, repairMaskSnapshot.repairMaskPointCount == 1u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, repairMaskSnapshot.forbiddenMaskPointCount == 0u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, repairMaskSnapshot.points.size() == 2u);
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            CountDebugPointKind(repairMaskSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::RepairMask) == 1u
        );
    }
}

static void TestSurfaceEditPreviewIsReadOnlyAndCommitMutatesTopology(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();
    const u32 oldRevision = instance.editRevision;
    const NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);

    NWB::Impl::DeformableSurfaceEditSession session;
    NWB::Impl::DeformableHolePreview preview;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(instance, params.posedHit, session));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(instance, session, params, preview));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, preview.valid);
    CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.skin.size() == oldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.sourceSamples.size() == oldVertexCount);

    NWB::Impl::DeformableHolePreviewMesh previewMesh;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildHolePreviewMesh(instance, params, previewMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewMesh.valid);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewMesh.vertices.size() >= 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewMesh.indices.size() >= 3u);

    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(instance, session, params, &result));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.restVertices.size() > oldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.indices.size() > oldIndexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == oldRevision + 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.firstWallVertex >= oldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewMesh.removedTriangleCount == result.removedTriangleCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewMesh.wallVertexCount == result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        result.addedVertexCount == ExpectedCappedHoleAddedVertexCount(result.wallVertexCount, result.wallLoopCutCount)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.wallVertexCount >= 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        result.addedTriangleCount == ExpectedCappedHoleAddedTriangleCount(result.wallVertexCount, result.wallLoopCutCount)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.skin.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.sourceSamples.size() == instance.restVertices.size());

    for(usize vertexIndex = 0u; vertexIndex < instance.skin.size(); ++vertexIndex)
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightSum(instance.skin[vertexIndex]), 1.0f));
    for(usize vertexIndex = oldVertexCount; vertexIndex < instance.sourceSamples.size(); ++vertexIndex){
        const NWB::Impl::SourceSample& sample = instance.sourceSamples[vertexIndex];
        NWB_ECS_GRAPHICS_TEST_CHECK(context, sample.sourceTri < instance.sourceTriangleCount);
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NearlyEqual(sample.bary[0] + sample.bary[1] + sample.bary[2], 1.0f)
        );
    }
}

static void TestSurfaceEditOperatorFootprintIsDerivedFromGeometry(TestContext& context){
    Vector<NWB::Impl::GeometryVertex> vertices;
    vertices.push_back(MakeOperatorGeometryVertex(-1.0f, -1.0f, 0.0f));
    vertices.push_back(MakeOperatorGeometryVertex(1.0f, -1.0f, 0.0f));
    vertices.push_back(MakeOperatorGeometryVertex(1.0f, 1.0f, 0.0f));
    vertices.push_back(MakeOperatorGeometryVertex(-1.0f, 1.0f, 0.0f));
    vertices.push_back(MakeOperatorGeometryVertex(0.0f, 0.0f, 0.0f));
    vertices.push_back(MakeOperatorGeometryVertex(-1.0f, -1.0f, -1.0f));
    vertices.push_back(MakeOperatorGeometryVertex(1.0f, -1.0f, -1.0f));
    vertices.push_back(MakeOperatorGeometryVertex(1.0f, 1.0f, -1.0f));
    vertices.push_back(MakeOperatorGeometryVertex(-1.0f, 1.0f, -1.0f));

    NWB::Impl::DeformableOperatorFootprint footprint;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildOperatorFootprintFromGeometry(vertices, footprint));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, footprint.vertexCount == 4u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, OperatorFootprintHasPoint(footprint, -1.0f, -1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, OperatorFootprintHasPoint(footprint, 1.0f, -1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, OperatorFootprintHasPoint(footprint, 1.0f, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, OperatorFootprintHasPoint(footprint, -1.0f, 1.0f));

    NWB::Impl::DeformableOperatorProfile boxProfile;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildOperatorProfileFromGeometry(vertices, boxProfile));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, boxProfile.sampleCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(boxProfile.samples[0u].scale, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(boxProfile.samples[1u].scale, 1.0f));

    Vector<NWB::Impl::GeometryVertex> coneVertices;
    coneVertices.push_back(MakeOperatorGeometryVertex(1.0f, 0.0f, 0.0f));
    coneVertices.push_back(MakeOperatorGeometryVertex(0.0f, 1.0f, 0.0f));
    coneVertices.push_back(MakeOperatorGeometryVertex(-1.0f, 0.0f, 0.0f));
    coneVertices.push_back(MakeOperatorGeometryVertex(0.0f, -1.0f, 0.0f));
    coneVertices.push_back(MakeOperatorGeometryVertex(0.0f, 0.0f, -1.0f));

    NWB::Impl::DeformableOperatorProfile coneProfile;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildOperatorProfileFromGeometry(coneVertices, coneProfile));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, coneProfile.sampleCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(coneProfile.samples[0u].scale, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(coneProfile.samples[1u].scale, 0.0f));
}

static void TestSurfaceEditOperatorFootprintDrivesPreviewAndCommit(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance boxInstance = MakeGridHoleInstance(6u, 6u);
    NWB::Impl::DeformableHoleEditParams boxParams =
        MakeHoleEditParams(boxInstance, 24u, 0.25f, 0.25f, 0.5f, 1.25f, 0.25f)
    ;
    boxParams.operatorFootprint = MakeBoxOperatorFootprint();
    boxParams.operatorProfile = MakeBoxOperatorProfile();
    NWB::Impl::DeformableHoleEditParams emptyOperatorParams = boxParams;
    emptyOperatorParams.operatorFootprint = NWB::Impl::DeformableOperatorFootprint{};
    emptyOperatorParams.operatorProfile = NWB::Impl::DeformableOperatorProfile{};
    NWB::Impl::DeformableHolePreviewMesh emptyOperatorPreviewMesh;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::BuildHolePreviewMesh(boxInstance, emptyOperatorParams, emptyOperatorPreviewMesh)
    );
    NWB::Impl::DeformableHoleEditParams footprintOnlyParams = boxParams;
    footprintOnlyParams.operatorProfile = NWB::Impl::DeformableOperatorProfile{};
    NWB::Impl::DeformableHolePreviewMesh footprintOnlyPreviewMesh;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::BuildHolePreviewMesh(boxInstance, footprintOnlyParams, footprintOnlyPreviewMesh)
    );

    NWB::Impl::DeformableRuntimeMeshInstance triangleInstance = MakeGridHoleInstance(6u, 6u);
    NWB::Impl::DeformableHoleEditParams triangleParams =
        MakeHoleEditParams(triangleInstance, 24u, 0.25f, 0.25f, 0.5f, 1.25f, 0.25f)
    ;
    triangleParams.operatorFootprint = MakeTriangleOperatorFootprint();
    triangleParams.operatorProfile = MakeConeOperatorProfile();

    NWB::Impl::DeformableHolePreviewMesh boxPreviewMesh;
    NWB::Impl::DeformableHolePreviewMesh trianglePreviewMesh;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildHolePreviewMesh(boxInstance, boxParams, boxPreviewMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildHolePreviewMesh(triangleInstance, triangleParams, trianglePreviewMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, boxPreviewMesh.valid);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, trianglePreviewMesh.valid);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, boxPreviewMesh.removedTriangleCount != trianglePreviewMesh.removedTriangleCount);

    NWB::Impl::DeformableSurfaceEditSession boxSession;
    NWB::Impl::DeformableHolePreview boxPreview;
    NWB::Impl::DeformableHoleEditResult boxResult;
    NWB::Impl::DeformableSurfaceEditRecord boxRecord;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(boxInstance, boxParams.posedHit, boxSession));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(boxInstance, boxSession, boxParams, boxPreview));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(boxInstance, boxSession, boxParams, &boxResult, &boxRecord));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, boxResult.removedTriangleCount == boxPreviewMesh.removedTriangleCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, boxResult.wallVertexCount == boxPreviewMesh.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, boxRecord.hole.operatorFootprint.vertexCount == boxParams.operatorFootprint.vertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(boxRecord.hole.operatorUp.y, 1.0f));

    NWB::Impl::DeformableSurfaceEditSession triangleSession;
    NWB::Impl::DeformableHolePreview trianglePreview;
    NWB::Impl::DeformableHoleEditResult triangleResult;
    NWB::Impl::DeformableSurfaceEditRecord triangleRecord;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(triangleInstance, triangleParams.posedHit, triangleSession));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(triangleInstance, triangleSession, triangleParams, trianglePreview));
    NWB::Impl::DeformableHoleEditParams mismatchedTriangleParams = triangleParams;
    mismatchedTriangleParams.operatorFootprint = boxParams.operatorFootprint;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::CommitHole(triangleInstance, triangleSession, mismatchedTriangleParams, &triangleResult, &triangleRecord)
    );
    mismatchedTriangleParams = triangleParams;
    mismatchedTriangleParams.operatorProfile = NWB::Impl::DeformableOperatorProfile{};
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::CommitHole(triangleInstance, triangleSession, mismatchedTriangleParams, &triangleResult, &triangleRecord)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(triangleInstance, triangleSession, triangleParams, &triangleResult, &triangleRecord));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, triangleResult.removedTriangleCount == trianglePreviewMesh.removedTriangleCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, triangleResult.wallVertexCount == trianglePreviewMesh.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        triangleRecord.hole.operatorFootprint.vertexCount == triangleParams.operatorFootprint.vertexCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        triangleRecord.hole.operatorProfile.sampleCount == triangleParams.operatorProfile.sampleCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(triangleRecord.hole.operatorUp.y, 1.0f));
}

static void TestSurfaceEditOperatorFootprintRemeshesIntersectedTriangles(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance(6u, 6u);
    AssignPlanarSurfaceUv0(instance);
    for(usize vertexIndex = 0u; vertexIndex < instance.sourceSamples.size(); ++vertexIndex){
        const f32 sourceMix = static_cast<f32>(vertexIndex + 1u) / static_cast<f32>(instance.sourceSamples.size() + 1u);
        instance.sourceSamples[vertexIndex] = MakeSourceSample(0u, sourceMix, 1.0f - sourceMix, 0.0f);
    }
    instance.restVertices[0u].tangent = Float4U(0.0f, 1.0f, 0.0f, 1.0f);
    const Vector<NWB::Impl::DeformableVertexRest> oldRestVertices = instance.restVertices;
    const Vector<NWB::Impl::SourceSample> oldSourceSamples = instance.sourceSamples;
    const usize oldVertexCount = instance.restVertices.size();
    NWB::Impl::DeformableHoleEditParams params =
        MakeHoleEditParams(instance, 24u, 0.25f, 0.25f, 0.5f, 0.55f, 0.25f)
    ;
    params.operatorFootprint = MakeBoxOperatorFootprint();
    params.operatorProfile = MakeBoxOperatorProfile();
    params.operatorUp = Float3U(1.0f, 0.0f, 0.0f);
    const Float3U holeCenter = RestHitPosition(instance, params);

    NWB::Impl::DeformableHolePreviewMesh previewMesh;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildHolePreviewMesh(instance, params, previewMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewMesh.valid);

    NWB::Impl::DeformableSurfaceEditSession session;
    NWB::Impl::DeformableHolePreview preview;
    NWB::Impl::DeformableHoleEditResult result;
    NWB::Impl::DeformableSurfaceEditRecord record;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(instance, params.posedHit, session));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(instance, session, params, preview));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(instance, session, params, &result, &record));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.operatorUp.x, 1.0f));

    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewMesh.removedTriangleCount == result.removedTriangleCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewMesh.wallVertexCount == result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.firstWallVertex > oldVertexCount);
    const usize generatedSurfaceVertexCount = static_cast<usize>(result.firstWallVertex) - oldVertexCount;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, generatedSurfaceVertexCount > 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        instance.restVertices.size() == oldVertexCount + generatedSurfaceVertexCount + result.addedVertexCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountUnweldedEdgesTouchingVertexRange(
            instance,
            result.firstWallVertex,
            static_cast<u32>(instance.restVertices.size())
        ) == 0u
    );

    const usize wallIndexBase = instance.indices.size() - (static_cast<usize>(result.addedTriangleCount) * 3u);
    bool foundGeneratedRimVertex = false;
    for(usize edgeIndex = 0u; edgeIndex < static_cast<usize>(result.wallVertexCount); ++edgeIndex){
        const usize indexBase = wallIndexBase + (edgeIndex * 6u);
        const u32 rimA = instance.indices[indexBase + 0u];
        const u32 rimB = instance.indices[indexBase + 1u];
        foundGeneratedRimVertex = foundGeneratedRimVertex
            || (rimA >= oldVertexCount && rimA < result.firstWallVertex)
            || (rimB >= oldVertexCount && rimB < result.firstWallVertex)
        ;
    }
    NWB_ECS_GRAPHICS_TEST_CHECK(context, foundGeneratedRimVertex);

    for(usize vertexIndex = 0u; vertexIndex < oldVertexCount; ++vertexIndex){
        const NWB::Impl::DeformableVertexRest& vertex = instance.restVertices[vertexIndex];
        const NWB::Impl::DeformableVertexRest& oldVertex = oldRestVertices[vertexIndex];
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.position.x, oldVertex.position.x));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.position.y, oldVertex.position.y));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.position.z, oldVertex.position.z));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.uv0.x, oldVertex.uv0.x));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.uv0.y, oldVertex.uv0.y));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.normal.x, oldVertex.normal.x));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.normal.y, oldVertex.normal.y));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.normal.z, oldVertex.normal.z));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.x, oldVertex.tangent.x));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.y, oldVertex.tangent.y));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.z, oldVertex.tangent.z));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.w, oldVertex.tangent.w));

        const NWB::Impl::SourceSample& sample = instance.sourceSamples[vertexIndex];
        const NWB::Impl::SourceSample& oldSample = oldSourceSamples[vertexIndex];
        NWB_ECS_GRAPHICS_TEST_CHECK(context, sample.sourceTri == oldSample.sourceTri);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[0], oldSample.bary[0]));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[1], oldSample.bary[1]));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(sample.bary[2], oldSample.bary[2]));
    }

    for(usize vertexIndex = oldVertexCount; vertexIndex < static_cast<usize>(result.firstWallVertex); ++vertexIndex){
        const NWB::Impl::DeformableVertexRest& vertex = instance.restVertices[vertexIndex];
        const Float2U expectedUv0 = ExpectedPlanarSurfaceUv0(vertex.position);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.uv0.x, expectedUv0.x));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.uv0.y, expectedUv0.y));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.normal.x, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.normal.y, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.normal.z, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.x, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.y, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.z, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.tangent.w, 1.0f));
    }

    for(usize vertexOffset = 0u; vertexOffset < static_cast<usize>(result.wallVertexCount); ++vertexOffset){
        const NWB::Impl::DeformableVertexRest& vertex =
            instance.restVertices[static_cast<usize>(result.firstWallVertex) + vertexOffset]
        ;
        const Float2U expectedUv0 = ExpectedPlanarSurfaceUv0(vertex.position);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.uv0.x, expectedUv0.x));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(vertex.uv0.y, expectedUv0.y));
    }

    for(usize indexBase = 0u; indexBase < wallIndexBase; indexBase += 3u){
        const u32 i0 = instance.indices[indexBase + 0u];
        const u32 i1 = instance.indices[indexBase + 1u];
        const u32 i2 = instance.indices[indexBase + 2u];
        const Float3U& p0 = instance.restVertices[i0].position;
        const Float3U& p1 = instance.restVertices[i1].position;
        const Float3U& p2 = instance.restVertices[i2].position;
        const f32 centroidX = (p0.x + p1.x + p2.x) / 3.0f;
        const f32 centroidY = (p0.y + p1.y + p2.y) / 3.0f;
        const f32 localX = (centroidX - holeCenter.x) / params.radius;
        const f32 localY = (centroidY - holeCenter.y) / (params.radius * params.ellipseRatio);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, Abs(localX) >= 1.0f - 0.0001f || Abs(localY) >= 1.0f - 0.0001f);
    }
}

static void TestSurfaceEditOperatorVolumeDepthGatesCurvedSurface(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance shallowInstance = MakeCurvedDepthGateHoleInstance();
    NWB::Impl::DeformableHoleEditParams shallowParams =
        MakeHoleEditParams(shallowInstance, 56u, 0.25f, 0.25f, 0.5f, 1.85f, 0.06f)
    ;
    shallowParams.operatorFootprint = MakeBoxOperatorFootprint();
    shallowParams.operatorProfile = MakeBoxOperatorProfile();

    NWB::Impl::DeformableRuntimeMeshInstance deepInstance = MakeCurvedDepthGateHoleInstance();
    NWB::Impl::DeformableHoleEditParams deepParams =
        MakeHoleEditParams(deepInstance, 56u, 0.25f, 0.25f, 0.5f, 1.85f, 0.35f)
    ;
    deepParams.operatorFootprint = MakeBoxOperatorFootprint();
    deepParams.operatorProfile = MakeBoxOperatorProfile();

    NWB::Impl::DeformableHolePreviewMesh shallowPreviewMesh;
    NWB::Impl::DeformableHolePreviewMesh deepPreviewMesh;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildHolePreviewMesh(shallowInstance, shallowParams, shallowPreviewMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildHolePreviewMesh(deepInstance, deepParams, deepPreviewMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, shallowPreviewMesh.valid);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, deepPreviewMesh.valid);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, shallowPreviewMesh.removedTriangleCount <= deepPreviewMesh.removedTriangleCount);

    NWB::Impl::DeformableSurfaceEditSession shallowSession;
    NWB::Impl::DeformableHolePreview shallowPreview;
    NWB::Impl::DeformableHoleEditResult shallowResult;
    NWB::Impl::DeformableSurfaceEditRecord shallowRecord;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(shallowInstance, shallowParams.posedHit, shallowSession));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(shallowInstance, shallowSession, shallowParams, shallowPreview));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::CommitHole(shallowInstance, shallowSession, shallowParams, &shallowResult, &shallowRecord)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, shallowResult.removedTriangleCount == shallowPreviewMesh.removedTriangleCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, shallowRecord.hole.depth == shallowParams.depth);
}

static void TestSurfaceEditOperatorRemeshIgnoresTrianglesOutsideDepth(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance frontInstance = MakeGridHoleInstance(6u, 6u);
    NWB::Impl::DeformableHoleEditParams frontParams =
        MakeHoleEditParams(frontInstance, 24u, 0.25f, 0.25f, 0.5f, 0.55f, 0.25f)
    ;
    frontParams.operatorFootprint = MakeBoxOperatorFootprint();
    frontParams.operatorProfile = MakeBoxOperatorProfile();

    NWB::Impl::DeformableRuntimeMeshInstance layeredInstance = MakeDepthSeparatedGridHoleInstance();
    NWB::Impl::DeformableHoleEditParams layeredParams =
        MakeHoleEditParams(layeredInstance, 24u, 0.25f, 0.25f, 0.5f, 0.55f, 0.25f)
    ;
    layeredParams.operatorFootprint = MakeBoxOperatorFootprint();
    layeredParams.operatorProfile = MakeBoxOperatorProfile();

    NWB::Impl::DeformableHolePreviewMesh frontPreviewMesh;
    NWB::Impl::DeformableHolePreviewMesh layeredPreviewMesh;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildHolePreviewMesh(frontInstance, frontParams, frontPreviewMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildHolePreviewMesh(layeredInstance, layeredParams, layeredPreviewMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, frontPreviewMesh.removedTriangleCount == layeredPreviewMesh.removedTriangleCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, frontPreviewMesh.wallVertexCount == layeredPreviewMesh.wallVertexCount);

    NWB::Impl::DeformableSurfaceEditSession session;
    NWB::Impl::DeformableHolePreview preview;
    NWB::Impl::DeformableHoleEditResult result;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(layeredInstance, layeredParams.posedHit, session));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(layeredInstance, session, layeredParams, preview));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(layeredInstance, session, layeredParams, &result));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.removedTriangleCount == frontPreviewMesh.removedTriangleCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.wallVertexCount == frontPreviewMesh.wallVertexCount);
}

static void TestSurfaceEditOperatorProfileTapersWallGeometry(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance straightInstance = MakeGridHoleInstance(6u, 6u);
    NWB::Impl::DeformableHoleEditParams straightParams =
        MakeHoleEditParams(straightInstance, 24u, 0.25f, 0.25f, 0.5f, 1.25f, 0.25f)
    ;
    straightParams.operatorFootprint = MakeBoxOperatorFootprint();
    straightParams.operatorProfile = MakeBoxOperatorProfile();
    const Float3U straightCenter = RestHitPosition(straightInstance, straightParams);

    NWB::Impl::DeformableSurfaceEditSession straightSession;
    NWB::Impl::DeformableHolePreview straightPreview;
    NWB::Impl::DeformableHoleEditResult straightResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(straightInstance, straightParams.posedHit, straightSession));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(straightInstance, straightSession, straightParams, straightPreview));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(straightInstance, straightSession, straightParams, &straightResult));

    NWB::Impl::DeformableRuntimeMeshInstance taperedInstance = MakeGridHoleInstance(6u, 6u);
    NWB::Impl::DeformableHoleEditParams taperedParams =
        MakeHoleEditParams(taperedInstance, 24u, 0.25f, 0.25f, 0.5f, 1.25f, 0.25f)
    ;
    taperedParams.operatorFootprint = MakeBoxOperatorFootprint();
    taperedParams.operatorProfile = MakeConeOperatorProfile();
    const Float3U taperedCenter = RestHitPosition(taperedInstance, taperedParams);

    NWB::Impl::DeformableSurfaceEditSession taperedSession;
    NWB::Impl::DeformableHolePreview taperedPreview;
    NWB::Impl::DeformableHoleEditResult taperedResult;
    NWB::Impl::DeformableSurfaceEditRecord taperedRecord;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(taperedInstance, taperedParams.posedHit, taperedSession));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(taperedInstance, taperedSession, taperedParams, taperedPreview));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::CommitHole(taperedInstance, taperedSession, taperedParams, &taperedResult, &taperedRecord)
    );

    NWB_ECS_GRAPHICS_TEST_CHECK(context, taperedResult.removedTriangleCount == straightResult.removedTriangleCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, taperedResult.wallVertexCount == straightResult.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, taperedRecord.hole.operatorProfile.sampleCount == taperedParams.operatorProfile.sampleCount);

    const f32 straightWallDistance = DistanceXYFrom(
        straightInstance.restVertices[straightResult.firstWallVertex].position,
        straightCenter
    );
    const f32 taperedWallDistance = DistanceXYFrom(
        taperedInstance.restVertices[taperedResult.firstWallVertex].position,
        taperedCenter
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, straightWallDistance > 0.1f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, taperedWallDistance < straightWallDistance * 0.25f);
}

static void TestSurfaceEditDebugSnapshotCapturesPreviewAndWallVertices(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeGridHoleInstance();
    instance.skeletonJointCount = 2u;
    instance.skin[0u] = MakeTwoJointSkin(0u, 0.75f, 1u, 0.25f);
    {
        NWB::Impl::DeformableMorph morph;
        morph.name = Name("debug_lift");
        NWB::Impl::DeformableMorphDelta delta{};
        delta.vertexId = 5u;
        delta.deltaPosition = Float3U(0.0f, 0.0f, 0.25f);
        morph.deltas.push_back(delta);
        instance.morphs.push_back(morph);
    }
    instance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
    instance.displacement.texture.virtualPath = Name("project/textures/debug_displacement");
    instance.displacement.amplitude = 0.2f;
    instance.displacement.bias = -0.05f;
    NWB::Impl::DeformableHoleEditParams params = MakeGridHoleEditParams(instance);
    const Float3U posedHitPoint = Float3U(2.0f, 3.0f, 4.0f);
    params.posedHit.setPosition(posedHitPoint);

    NWB::Impl::DeformableSurfaceEditSession session;
    NWB::Impl::DeformableHolePreview preview;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(instance, params.posedHit, session));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(instance, session, params, preview));

    NWB::Impl::DeformableSurfaceEditDebugSnapshot previewSnapshot;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot(
            instance,
            &session,
            &preview,
            nullptr,
            previewSnapshot
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.previewValid);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.previewTriangle == params.posedHit.triangle);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(previewSnapshot.previewPosedHitPoint.x, posedHitPoint.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(previewSnapshot.previewPosedHitPoint.y, posedHitPoint.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(previewSnapshot.previewPosedHitPoint.z, posedHitPoint.z));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NearlyEqual(previewSnapshot.previewRestHitPoint.x, posedHitPoint.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.editableTriangleCount == instance.indices.size() / 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.invalidFrameCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.skinnedVertexCount == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.maxSkinInfluenceCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.skinWeightLineCount == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(previewSnapshot.maxSkinWeight, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.morphCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.morphDeltaCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.morphDeltaLineCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(previewSnapshot.maxMorphPositionDelta, 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        previewSnapshot.displacementMode == NWB::Impl::DeformableDisplacementMode::ScalarTexture
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(previewSnapshot.displacementAmplitude, 0.2f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(previewSnapshot.displacementBias, -0.05f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.displacementTextureBound);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.displacementMagnitudeLineCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(previewSnapshot.maxDisplacementMagnitude, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        previewSnapshot.lines.size()
            == 3u + previewSnapshot.skinWeightLineCount + previewSnapshot.morphDeltaLineCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, previewSnapshot.points.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugLineKind(previewSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::SkinWeight)
            == previewSnapshot.skinWeightLineCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugLineKind(previewSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::MorphDelta)
            == previewSnapshot.morphDeltaLineCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugLineKind(previewSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Normal) == 1u
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugLineKind(previewSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Tangent) == 1u
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugLineKind(previewSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Bitangent) == 1u
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugPointKind(previewSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Hit) == 1u
    );

    {
        NWB::Impl::DeformableRuntimeMeshInstance rampInstance = MakeTriangleInstance();
        rampInstance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarUvRamp;
        rampInstance.displacement.amplitude = 0.2f;

        NWB::Impl::DeformableSurfaceEditDebugSnapshot rampSnapshot;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot(
                rampInstance,
                nullptr,
                nullptr,
                nullptr,
                rampSnapshot
            )
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, rampSnapshot.displacementMagnitudeLineCount == 2u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(rampSnapshot.maxDisplacementMagnitude, 0.2f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, rampSnapshot.lines.size() == 2u);
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            CountDebugLineKind(rampSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::DisplacementMagnitude) == 2u
        );
    }
    {
        NWB::Impl::DeformableRuntimeMeshInstance textureInstance = MakeTriangleInstance();
        textureInstance.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
        textureInstance.displacement.texture.virtualPath = Name("tests/textures/debug_scalar_displacement");
        textureInstance.displacement.amplitude = 2.0f;

        NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
            "tests/textures/debug_scalar_displacement",
            Float4U(0.25f, 0.0f, 0.0f, 0.0f),
            Float4U(0.5f, 0.0f, 0.0f, 0.0f),
            Float4U(1.0f, 0.0f, 0.0f, 0.0f)
        );

        NWB::Impl::DeformableSurfaceEditDebugSnapshot textureSnapshot;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot(
                textureInstance,
                nullptr,
                nullptr,
                nullptr,
                &texture,
                textureSnapshot
            )
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, textureSnapshot.displacementMagnitudeLineCount == 3u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(textureSnapshot.maxDisplacementMagnitude, 2.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, textureSnapshot.lines.size() == 3u);
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            CountDebugLineKind(textureSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::DisplacementMagnitude) == 3u
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(textureSnapshot.lines[1u].end.z, 1.0f));
    }
    {
        NWB::Impl::DeformableRuntimeMeshInstance textureInstance = MakeTriangleInstance();
        textureInstance.displacement.mode = NWB::Impl::DeformableDisplacementMode::VectorObjectTexture;
        textureInstance.displacement.texture.virtualPath = Name("tests/textures/debug_vector_displacement");
        textureInstance.displacement.amplitude = 2.0f;

        const Float4U vectorSample(0.25f, -0.125f, 0.5f, 0.0f);
        NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
            "tests/textures/debug_vector_displacement",
            vectorSample,
            vectorSample,
            vectorSample
        );

        NWB::Impl::DeformableSurfaceEditDebugSnapshot textureSnapshot;
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot(
                textureInstance,
                nullptr,
                nullptr,
                nullptr,
                &texture,
                textureSnapshot
            )
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, textureSnapshot.displacementMagnitudeLineCount == 3u);
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(textureSnapshot.maxDisplacementMagnitude, Sqrt(1.3125f)));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, textureSnapshot.lines.size() == 3u);
        NWB_ECS_GRAPHICS_TEST_CHECK(
            context,
            CountDebugLineKind(textureSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::DisplacementMagnitude) == 3u
        );
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(textureSnapshot.lines[0u].end.x, -0.5f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(textureSnapshot.lines[0u].end.y, -1.25f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(textureSnapshot.lines[0u].end.z, 1.0f));
    }

    NWB::Impl::DeformableHoleEditResult result;
    NWB::Impl::DeformableSurfaceEditRecord record;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(instance, session, params, &result, &record));

    NWB::Impl::DeformableSurfaceEditState state;
    state.edits.push_back(record);
    SimulateRuntimeMeshUpload(instance);
    NWB::Impl::DeformableAccessoryAttachmentComponent attachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::AttachAccessory(instance, result, 0.08f, 0.12f, attachment)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        AppendAccessoryRecord(
            state,
            attachment,
            record.editId,
            s_MockAccessoryGeometryPath,
            s_MockAccessoryMaterialPath
        )
    );

    NWB::Impl::DeformableSurfaceEditDebugSnapshot stateSnapshot;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::BuildDeformableSurfaceEditDebugSnapshot(
            instance,
            nullptr,
            nullptr,
            &state,
            stateSnapshot
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !stateSnapshot.previewValid);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateSnapshot.removedTriangleCount == result.removedTriangleCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateSnapshot.wallVertexCount == result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateSnapshot.accessoryAnchorCount == result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateSnapshot.wallNormalBasisLineCount == result.wallVertexCount * 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateSnapshot.wallTangentBasisLineCount == result.wallVertexCount * 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateSnapshot.skinWeightLineCount == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateSnapshot.morphDeltaLineCount != 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        stateSnapshot.lines.size()
            == result.wallVertexCount * 6u
                + stateSnapshot.skinWeightLineCount
                + stateSnapshot.morphDeltaLineCount
                + stateSnapshot.displacementMagnitudeLineCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateSnapshot.points.size() == result.wallVertexCount * 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugLineKind(stateSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Wall) == result.wallVertexCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugLineKind(stateSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Accessory)
            == result.wallVertexCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugLineKind(stateSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Normal)
            == stateSnapshot.wallNormalBasisLineCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugLineKind(stateSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Tangent)
            == stateSnapshot.wallTangentBasisLineCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugPointKind(stateSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Wall) == result.wallVertexCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CountDebugPointKind(stateSnapshot, NWB::Impl::DeformableDebugPrimitiveKind::Accessory)
            == result.wallVertexCount
    );

    AString dump;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildDeformableSurfaceEditDebugDump(stateSnapshot, dump));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("deformable_debug_snapshot") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("wall_vertices=") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("accessory_anchors=") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("wall_basis(normal=") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("skin_weight_lines=") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("max_skin_weight=1") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("morph_delta_lines=") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("points=") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("removed_triangles=") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("invalid_frames=0") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("skin_vertices=") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("max_skin_influences=2") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("morphs=1") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("displacement_mode=2") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, dump.find("displacement_texture=yes") != AString::npos);
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
        !CommitPreviewedHole(dirtyInstance, dirtyParams, &dirtyResult)
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

    NWB::Impl::DeformableHoleEditParams mismatchedEllipseParams = params;
    mismatchedEllipseParams.ellipseRatio = 1.25f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitHole(instance, session, mismatchedEllipseParams));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == 0u);

    NWB::Impl::DeformableHoleEditParams epsilonMismatchedParams = params;
    epsilonMismatchedParams.ellipseRatio += 0.00001f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitHole(instance, session, epsilonMismatchedParams));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == 0u);

    NWB::Impl::DeformableHoleEditParams epsilonMismatchedHitParams = params;
    epsilonMismatchedHitParams.posedHit.bary[0] += 0.00001f;
    epsilonMismatchedHitParams.posedHit.bary[1] -= 0.00001f;
    epsilonMismatchedHitParams.posedHit.restSample.bary[0] += 0.00001f;
    epsilonMismatchedHitParams.posedHit.restSample.bary[1] -= 0.00001f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitHole(instance, session, epsilonMismatchedHitParams));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == 0u);

    NWB::Impl::DeformableHoleEditParams signedZeroMismatchedHitParams = params;
    signedZeroMismatchedHitParams.posedHit.normal.x = -0.0f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitHole(instance, session, signedZeroMismatchedHitParams));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == 0u);

    NWB::Impl::DeformableHoleEditParams positionWMismatchedHitParams = params;
    positionWMismatchedHitParams.posedHit.position.w = 0.0f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitHole(instance, session, positionWMismatchedHitParams));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == 0u);

    NWB::Impl::DeformableHoleEditParams normalWMismatchedHitParams = params;
    normalWMismatchedHitParams.posedHit.normal.w = 1.0f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::CommitHole(instance, session, normalWMismatchedHitParams));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, instance.editRevision == 0u);

    NWB::Impl::DeformableHoleEditResult result;
    NWB::Impl::DeformableSurfaceEditRecord record;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::CommitHole(instance, session, params, &result, &record));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.editRevision == instance.editRevision);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        (instance.dirtyFlags & NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.firstWallVertex >= oldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, result.wallVertexCount >= 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, record.hole.baseEditRevision == params.posedHit.editRevision);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, record.hole.restSample.sourceTri == params.posedHit.restSample.sourceTri);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.restPosition.x, params.posedHit.position.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.restPosition.y, params.posedHit.position.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.restPosition.z, params.posedHit.position.z));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.restNormal.z, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.radius, params.radius));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.ellipseRatio, params.ellipseRatio));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(record.hole.depth, params.depth));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, record.result.firstWallVertex == result.firstWallVertex);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, record.editId == result.editRevision);

    const Name mockGeometry(s_MockAccessoryGeometryPath);
    const Name mockMaterial(s_MockAccessoryMaterialPath);
    NWB::Impl::DeformableAccessoryAttachmentComponent attachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::AttachAccessory(
            instance,
            result,
            0.08f,
            0.12f,
            attachment
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !attachment.targetEntity.valid());

    instance.dirtyFlags = static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        instance.dirtyFlags & ~NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty
    );
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
    NWB::Impl::DeformableRuntimeMeshInstance futureInstance = instance;
    ++futureInstance.editRevision;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::AttachAccessory(
            futureInstance,
            result,
            0.08f,
            0.12f,
            rejectedAttachment
        )
    );

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

    NWB::Core::Scene::TransformComponent baseTransform;
    instance.dirtyFlags = static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        instance.dirtyFlags | NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty
    );
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
    NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(baseTransform.position.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(baseTransform.position.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.position.z, 0.08f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.scale.x, 0.12f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.scale.y, 0.12f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(baseTransform.scale.z, 0.12f));

    NWB::Impl::DeformableAccessoryAttachmentComponent localAttachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::AttachAccessoryAtWallLoopParameter(
            instance,
            result,
            0.25f,
            0.08f,
            0.12f,
            localAttachment
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(localAttachment.wallLoopParameter(), 0.25f));

    NWB::Core::Scene::TransformComponent localTransform;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            instance,
            NWB::Impl::DeformablePickingInputs{},
            localAttachment,
            localTransform
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(localTransform.position.z, 0.08f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(localTransform.scale.x, 0.12f));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NearlyEqual(localTransform.position.x, baseTransform.position.x)
            || !NearlyEqual(localTransform.position.y, baseTransform.position.y)
    );

    NWB::Impl::DeformableAccessoryAttachmentComponent invalidLoopAttachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::AttachAccessoryAtWallLoopParameter(
            instance,
            result,
            1.0f,
            0.08f,
            0.12f,
            invalidLoopAttachment
        )
    );

    NWB::Impl::DeformableRuntimeMeshInstance strayWallTriangleInstance = instance;
    strayWallTriangleInstance.indices.push_back(0u);
    strayWallTriangleInstance.indices.push_back(1u);
    strayWallTriangleInstance.indices.push_back(5u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            strayWallTriangleInstance,
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

    forgedAttachment = attachment;
    forgedAttachment.anchorEditId = 0u;
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
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0] = MakeIdentityJointMatrix();
    joints.joints[0].rows[3] = Float4(2.0f, 0.0f, 0.0f, 1.0f);

    NWB::Impl::DeformablePickingInputs translatedInputs;
    translatedInputs.jointPalette = &joints;
    NWB::Core::Scene::TransformComponent translatedTransform;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            instance,
            translatedInputs,
            attachment,
            translatedTransform
        )
    );
    const SIMDVector translatedOffset =
        VectorSubtract(LoadFloat(translatedTransform.position), LoadFloat(baseTransform.position))
    ;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetX(translatedOffset), 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetY(translatedOffset), 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetZ(translatedOffset), 0.0f));

    NWB::Impl::DeformableSurfaceEditState state;
    NWB::Impl::DeformableAccessoryAttachmentRecord accessoryRecord;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        SetAccessoryRecordAssetPaths(accessoryRecord, s_MockAccessoryGeometryPath, s_MockAccessoryMaterialPath)
    );
    accessoryRecord.anchorEditId = record.editId;
    accessoryRecord.firstWallVertex = localAttachment.firstWallVertex;
    accessoryRecord.wallVertexCount = localAttachment.wallVertexCount;
    accessoryRecord.normalOffset = localAttachment.normalOffset();
    accessoryRecord.uniformScale = localAttachment.uniformScale();
    accessoryRecord.wallLoopParameter = localAttachment.wallLoopParameter();
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
        loadedState.accessories[0].firstWallVertex == localAttachment.firstWallVertex
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        loadedState.accessories[0].wallVertexCount == localAttachment.wallVertexCount
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(loadedState.accessories[0].wallLoopParameter, 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories[0].geometry.name() == mockGeometry);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories[0].material.name() == mockMaterial);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories[0].geometryVirtualPathText.view() == s_MockAccessoryGeometryPath);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories[0].materialVirtualPathText.view() == s_MockAccessoryMaterialPath);

    AString stateDump;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildSurfaceEditStateDebugDump(loadedState, stateDump));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateDump.find("project/meshes/mock_earring") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateDump.find("project/materials/mat_test") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateDump.find("source_tri=") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateDump.find("rest_position=(") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateDump.find("wall_span=(") != AString::npos);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, stateDump.find("wall_loop=0.25") != AString::npos);

    NWB::Impl::DeformableSurfaceEditState malformedState = state;
    malformedState.edits[0].result.wallVertexCount = 7u;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.accessories[0].normalOffset = -0.01f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.accessories[0].wallLoopParameter = 1.0f;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.accessories[0].geometryVirtualPathText.clear();
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        malformedState.accessories[0].materialVirtualPathText.assign("project/materials/other")
    );
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
    malformedState.accessories[0].anchorEditId = 6u;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    malformedState.edits[0].hole.restNormal = Float3U(0.0f, 0.0f, 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    malformedState = state;
    ++malformedState.accessories[0].anchorEditId;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, binary));

    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SerializeSurfaceEditState(state, binary));
    NWB::Core::Assets::AssetBytes corruptBinary = binary;
    corruptBinary[0u] ^= 0x1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::DeserializeSurfaceEditState(corruptBinary, loadedState));
    corruptBinary = binary;
    corruptBinary[4u] ^= 0x1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::DeserializeSurfaceEditState(corruptBinary, loadedState));
    corruptBinary = binary;
    corruptBinary.resize(corruptBinary.size() - 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::DeserializeSurfaceEditState(corruptBinary, loadedState));
    binary.push_back(0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::DeserializeSurfaceEditState(binary, loadedState));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.edits.empty());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories.empty());
}

static void TestSurfaceEditStateReplayOneHole(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = MakeGridHoleInstance();
    editedInstance.editRevision = 0u;
    NWB::Impl::DeformableSurfaceEditState state;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitRecordedHole(editedInstance, 8u, 0.48f, 0.25f, state));

    NWB::Core::Assets::AssetBytes binary;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SerializeSurfaceEditState(state, binary));
    NWB::Impl::DeformableSurfaceEditState loadedState;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::DeserializeSurfaceEditState(binary, loadedState));

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = MakeGridHoleInstance();
    replayInstance.editRevision = 0u;
    replayInstance.handle.value = 144u;
    const usize oldVertexCount = replayInstance.restVertices.size();
    const usize oldIndexCount = replayInstance.indices.size();

    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ApplySurfaceEditState(
            replayInstance,
            loadedState,
            NWB::Impl::DeformableSurfaceEditReplayContext{},
            &replayResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.restoredAccessoryCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.finalEditRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.topologyChanged);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.handle.value == 144u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.restVertices.size() == editedInstance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.indices.size() == editedInstance.indices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.restVertices.size() > oldVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.indices.size() > oldIndexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.editRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        (replayInstance.dirtyFlags & NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u
    );
}

static void TestSurfaceEditStateReplayEmptyStateIsNoOp(TestContext& context){
    NWB::Impl::DeformableSurfaceEditState state;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SerializeSurfaceEditState(state, binary));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    NWB::Impl::DeformableSurfaceEditState loadedState;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::DeserializeSurfaceEditState(binary, loadedState));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.edits.empty());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories.empty());

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = MakeGridHoleInstance();
    replayInstance.editRevision = 0u;
    replayInstance.handle.value = 114u;
    const usize oldVertexCount = replayInstance.restVertices.size();
    const usize oldIndexCount = replayInstance.indices.size();

    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ApplySurfaceEditState(
            replayInstance,
            loadedState,
            NWB::Impl::DeformableSurfaceEditReplayContext{},
            &replayResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.restoredAccessoryCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.finalEditRevision == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !replayResult.topologyChanged);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.handle.value == 114u);
    CheckHoleEditUnchanged(context, replayInstance, oldVertexCount, oldIndexCount, 0u);
}

static void TestSurfaceEditStateReplayRejectsMismatchedTargetEntity(TestContext& context){
    NWB::Impl::DeformableSurfaceEditState state;
    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = MakeGridHoleInstance();
    replayInstance.entity = NWB::Core::ECS::EntityID(8u, 0u);
    replayInstance.editRevision = 0u;
    replayInstance.handle.value = 116u;
    const usize oldVertexCount = replayInstance.restVertices.size();
    const usize oldIndexCount = replayInstance.indices.size();

    NWB::Impl::DeformableSurfaceEditReplayContext replayContext;
    replayContext.targetEntity = NWB::Core::ECS::EntityID(9u, 0u);
    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::ApplySurfaceEditState(replayInstance, state, replayContext, &replayResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.restoredAccessoryCount == 0u);
    CheckHoleEditUnchanged(context, replayInstance, oldVertexCount, oldIndexCount, 0u);
}

static void TestSurfaceEditStateReplayRestoresAccessory(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = MakeGridHoleInstance();
    editedInstance.editRevision = 0u;
    NWB::Impl::DeformableSurfaceEditState state;
    NWB::Impl::DeformableHoleEditResult holeResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitRecordedHole(editedInstance, 8u, 0.48f, 0.25f, state, &holeResult));

    NWB::Impl::DeformableAccessoryAttachmentComponent attachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::AttachAccessory(editedInstance, holeResult, 0.08f, 0.12f, attachment));

    const Name mockGeometry(s_MockAccessoryGeometryPath);
    const Name mockMaterial(s_MockAccessoryMaterialPath);
    NWB::Impl::DeformableAccessoryAttachmentRecord accessoryRecord;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        SetAccessoryRecordAssetPaths(accessoryRecord, s_MockAccessoryGeometryPath, s_MockAccessoryMaterialPath)
    );
    accessoryRecord.anchorEditId = state.edits.back().editId;
    accessoryRecord.firstWallVertex = attachment.firstWallVertex;
    accessoryRecord.wallVertexCount = attachment.wallVertexCount;
    accessoryRecord.normalOffset = attachment.normalOffset();
    accessoryRecord.uniformScale = attachment.uniformScale();
    accessoryRecord.wallLoopParameter = attachment.wallLoopParameter();
    state.accessories.push_back(accessoryRecord);

    NWB::Core::Assets::AssetBytes binary;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SerializeSurfaceEditState(state, binary));
    NWB::Impl::DeformableSurfaceEditState loadedState;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::DeserializeSurfaceEditState(binary, loadedState));

    TestAssetManager testAssets;
    testAssets.binarySource.addAvailablePath(s_MockAccessoryGeometryPath);
    testAssets.binarySource.addAvailablePath(s_MockAccessoryMaterialPath);

    TestWorld testWorld;
    auto targetEntity = testWorld.world.createEntity();
    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = MakeGridHoleInstance();
    replayInstance.entity = targetEntity.id();
    replayInstance.editRevision = 0u;
    replayInstance.handle.value = 244u;

    NWB::Impl::DeformableSurfaceEditReplayContext replayContext;
    replayContext.assetManager = &testAssets.manager;
    replayContext.world = &testWorld.world;
    replayContext.targetEntity = replayInstance.entity;
    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ApplySurfaceEditState(replayInstance, loadedState, replayContext, &replayResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.restoredAccessoryCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.editRevision == 1u);

    u32 accessoryCount = 0u;
    NWB::Impl::DeformableAccessoryAttachmentComponent restoredAttachment;
    testWorld.world.view<NWB::Impl::RendererComponent, NWB::Impl::DeformableAccessoryAttachmentComponent>().each(
        [&](NWB::Core::ECS::EntityID, NWB::Impl::RendererComponent& renderer, NWB::Impl::DeformableAccessoryAttachmentComponent& restored){
            ++accessoryCount;
            restoredAttachment = restored;
            NWB_ECS_GRAPHICS_TEST_CHECK(context, !renderer.visible);
            NWB_ECS_GRAPHICS_TEST_CHECK(context, renderer.geometry.name() == mockGeometry);
            NWB_ECS_GRAPHICS_TEST_CHECK(context, renderer.material.name() == mockMaterial);
        }
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, accessoryCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, restoredAttachment.targetEntity == replayInstance.entity);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, restoredAttachment.runtimeMesh == replayInstance.handle);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, restoredAttachment.anchorEditId == loadedState.edits[0].editId);

    SimulateRuntimeMeshUpload(replayInstance);
    NWB::Core::Scene::TransformComponent resolvedTransform;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            replayInstance,
            NWB::Impl::DeformablePickingInputs{},
            restoredAttachment,
            resolvedTransform
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.position.z, 0.08f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.scale.x, 0.12f));
}

static void TestSurfaceEditStateReplayRejectsInvalidAccessoryAsset(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = MakeGridHoleInstance();
    editedInstance.editRevision = 0u;
    NWB::Impl::DeformableSurfaceEditState state;
    NWB::Impl::DeformableHoleEditResult holeResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitRecordedHole(editedInstance, 8u, 0.48f, 0.25f, state, &holeResult));

    NWB::Impl::DeformableAccessoryAttachmentComponent attachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::AttachAccessory(editedInstance, holeResult, 0.08f, 0.12f, attachment));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        AppendAccessoryRecord(
            state,
            attachment,
            state.edits.back().editId,
            s_MockAccessoryGeometryPath,
            s_MockAccessoryMaterialPath
        )
    );

    NWB::Core::Assets::AssetBytes binary;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SerializeSurfaceEditState(state, binary));
    NWB::Impl::DeformableSurfaceEditState loadedState;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::DeserializeSurfaceEditState(binary, loadedState));

    TestAssetManager testAssets;
    testAssets.binarySource.addAvailablePath(s_MockAccessoryGeometryPath);
    testAssets.binarySource.addAvailablePath(s_MockAccessoryMaterialPath);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        testAssets.registry.registerCodec(MakeUnique<TestWrongTypeGeometryAssetCodec>(), true)
    );
    TestWorld testWorld;
    auto targetEntity = testWorld.world.createEntity();
    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = MakeGridHoleInstance();
    replayInstance.entity = targetEntity.id();
    replayInstance.editRevision = 0u;
    replayInstance.handle.value = 246u;

    const usize oldVertexCount = replayInstance.restVertices.size();
    const usize oldIndexCount = replayInstance.indices.size();

    NWB::Impl::DeformableSurfaceEditReplayContext replayContext;
    replayContext.assetManager = &testAssets.manager;
    replayContext.world = &testWorld.world;
    replayContext.targetEntity = replayInstance.entity;

    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::ApplySurfaceEditState(replayInstance, loadedState, replayContext, &replayResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.restoredAccessoryCount == 0u);
    CheckHoleEditUnchanged(context, replayInstance, oldVertexCount, oldIndexCount, 0u);

    u32 accessoryCount = 0u;
    testWorld.world.view<NWB::Impl::DeformableAccessoryAttachmentComponent>().each(
        [&](NWB::Core::ECS::EntityID, NWB::Impl::DeformableAccessoryAttachmentComponent&){
            ++accessoryCount;
        }
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, accessoryCount == 0u);
}

static void TestSurfaceEditStateReplayRestoresMultipleAccessories(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = MakeGridHoleInstance(6u, 4u);
    editedInstance.editRevision = 0u;
    NWB::Impl::DeformableSurfaceEditState state;

    RecordedHoleAccessory firstAccessory;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHoleAccessory(editedInstance, 12u, 0.48f, 0.25f, 0.08f, 0.12f, state, firstAccessory)
    );

    NWB::Impl::DeformableAccessoryAttachmentComponent secondFirstHoleAttachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::AttachAccessory(editedInstance, firstAccessory.hole, 0.11f, 0.14f, secondFirstHoleAttachment)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        AppendAccessoryRecord(
            state,
            secondFirstHoleAttachment,
            firstAccessory.editId,
            s_MockAccessoryGeometryPath,
            s_MockAccessoryMaterialPath
        )
    );

    u32 secondTriangle = Limit<u32>::s_Max;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        FindNearestUpFacingRestTriangle(editedInstance, Float3U(1.0f, 0.0f, 0.0f), secondTriangle)
    );

    RecordedHoleAccessory secondAccessory;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHoleAccessory(editedInstance, secondTriangle, 0.48f, 0.25f, 0.16f, 0.18f, state, secondAccessory)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories.size() == 3u);

    NWB::Core::Scene::TransformComponent oldHoleTransform;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            editedInstance,
            NWB::Impl::DeformablePickingInputs{},
            firstAccessory.attachment,
            oldHoleTransform
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(oldHoleTransform.position.z, 0.08f));

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = MakeGridHoleInstance(6u, 4u);
    replayInstance.editRevision = 0u;
    replayInstance.handle.value = 544u;
    SurfaceEditReplayFixture replayFixture;
    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    if(!ApplySurfaceEditReplayFixture(context, state, replayInstance, replayFixture, replayResult))
        return;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.restoredAccessoryCount == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.editRevision == 2u);

    SimulateRuntimeMeshUpload(replayInstance);
    u32 accessoryCount = 0u;
    u32 firstEditAccessoryCount = 0u;
    u32 secondEditAccessoryCount = 0u;
    replayFixture.world.world.view<NWB::Impl::DeformableAccessoryAttachmentComponent>().each(
        [&](NWB::Core::ECS::EntityID, NWB::Impl::DeformableAccessoryAttachmentComponent& restored){
            ++accessoryCount;
            if(restored.anchorEditId == replayFixture.loadedState.edits[0].editId)
                ++firstEditAccessoryCount;
            if(restored.anchorEditId == replayFixture.loadedState.edits[1].editId)
                ++secondEditAccessoryCount;

            NWB::Core::Scene::TransformComponent resolvedTransform;
            NWB_ECS_GRAPHICS_TEST_CHECK(
                context,
                NWB::Impl::ResolveAccessoryAttachmentTransform(
                    replayInstance,
                    NWB::Impl::DeformablePickingInputs{},
                    restored,
                    resolvedTransform
                )
            );
            NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedTransform.scale.x > 0.0f);
        }
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, accessoryCount == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstEditAccessoryCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondEditAccessoryCount == 1u);

    NWB::Impl::DeformableSurfaceEditState malformedState = state;
    malformedState.accessories[0].anchorEditId = 99u;
    NWB::Core::Assets::AssetBytes malformedBinary;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, malformedBinary));
    malformedState = state;
    malformedState.edits[1].editId = malformedState.edits[0].editId;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::SerializeSurfaceEditState(malformedState, malformedBinary));
}

static void TestMinimalMilestoneReplayPreservesAnimatedPayload(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = MakeGridHoleInstance(6u, 4u);
    editedInstance.editRevision = 0u;
    ConfigureMinimalMilestonePayload(editedInstance);
    NWB::Impl::DeformableSurfaceEditState state;

    RecordedTwoHoleAccessories accessories;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitTwoRecordedHoleAccessories(context, editedInstance, state, accessories));

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = MakeGridHoleInstance(6u, 4u);
    replayInstance.editRevision = 0u;
    replayInstance.handle.value = 644u;
    ConfigureMinimalMilestonePayload(replayInstance);
    SurfaceEditReplayFixture replayFixture;
    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    if(!ApplySurfaceEditReplayFixture(context, state, replayInstance, replayFixture, replayResult))
        return;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.restoredAccessoryCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.editRevision == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.skeletonJointCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.skin.size() == replayInstance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.sourceSamples.size() == replayInstance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.morphs.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        replayInstance.displacement.mode == NWB::Impl::DeformableDisplacementMode::VectorObjectTexture
    );
    for(const NWB::Impl::SkinInfluence4& skin : replayInstance.skin)
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(SkinWeightSum(skin), 1.0f));

    NWB::Impl::DeformableMorphWeightsComponent morphWeights;
    morphWeights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("minimal_milestone_lift"), 0.5f });
    Vector<NWB::Impl::DeformerSystem::DeformerVertexMorphRangeGpu> morphRanges;
    Vector<NWB::Impl::DeformerSystem::DeformerBlendedMorphDeltaGpu> morphDeltas;
    usize morphSignature = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::DeformerMorphPayload::BuildBlendedMorphPayload(
            replayInstance,
            &morphWeights,
            morphRanges,
            morphDeltas,
            morphSignature
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !morphDeltas.empty());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, morphSignature != 0u);

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(2u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0u].rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    joints.joints[0u].rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joints.joints[0u].rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    joints.joints[0u].rows[3] = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    joints.joints[1u] = joints.joints[0u];
    joints.joints[1u].rows[3] = Float4(0.0f, 0.25f, 0.0f, 1.0f);

    NWB::Impl::DeformableDisplacementTexture displacementTexture = MakeTestDisplacementTexture(
        "tests/textures/minimal_milestone_vector_displacement",
        Float4U(0.0f, 0.0f, 0.0f, 0.0f),
        Float4U(0.25f, 0.0f, 0.0f, 0.0f),
        Float4U(0.5f, 0.0f, 0.0f, 0.0f)
    );

    NWB::Impl::DeformablePickingInputs animatedInputs;
    animatedInputs.jointPalette = &joints;
    animatedInputs.morphWeights = &morphWeights;
    animatedInputs.displacementTexture = &displacementTexture;

    SimulateRuntimeMeshUpload(replayInstance);
    Vector<NWB::Impl::DeformableVertexRest> posedVertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::BuildDeformablePickingVertices(replayInstance, animatedInputs, posedVertices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, posedVertices.size() == replayInstance.restVertices.size());
    bool sawAnimatedOffset = false;
    for(usize vertexIndex = 0u; vertexIndex < posedVertices.size(); ++vertexIndex){
        const Float3U& posed = posedVertices[vertexIndex].position;
        const Float3U& rest = replayInstance.restVertices[vertexIndex].position;
        NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat3(posed));
        if(
            Abs(posed.x - rest.x) > 0.0001f
            || Abs(posed.y - rest.y) > 0.0001f
            || Abs(posed.z - rest.z) > 0.0001f
        )
            sawAnimatedOffset = true;
    }
    NWB_ECS_GRAPHICS_TEST_CHECK(context, sawAnimatedOffset);

    u32 accessoryCount = 0u;
    replayFixture.world.world.view<NWB::Impl::DeformableAccessoryAttachmentComponent>().each(
        [&](NWB::Core::ECS::EntityID, NWB::Impl::DeformableAccessoryAttachmentComponent& restored){
            ++accessoryCount;

            NWB::Core::Scene::TransformComponent resolvedTransform;
            NWB_ECS_GRAPHICS_TEST_CHECK(
                context,
                NWB::Impl::ResolveAccessoryAttachmentTransform(
                    replayInstance,
                    animatedInputs,
                    restored,
                    resolvedTransform
                )
            );
            NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(resolvedTransform.position.x));
            NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(resolvedTransform.position.y));
            NWB_ECS_GRAPHICS_TEST_CHECK(context, IsFinite(resolvedTransform.position.z));
            NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedTransform.scale.x > 0.0f);
        }
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, accessoryCount == 2u);
}

static void TestSurfaceEditStateReplayTwoHoles(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = MakeGridHoleInstance(6u, 4u);
    editedInstance.editRevision = 0u;
    NWB::Impl::DeformableSurfaceEditState state;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitRecordedHole(editedInstance, 12u, 0.48f, 0.25f, state));
    u32 secondTriangle = Limit<u32>::s_Max;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, FindSecondGridHoleTriangle(editedInstance, secondTriangle));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitRecordedHole(editedInstance, secondTriangle, 0.48f, 0.25f, state));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 2u);

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = MakeGridHoleInstance(6u, 4u);
    replayInstance.editRevision = 0u;
    replayInstance.handle.value = 344u;

    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ApplySurfaceEditState(
            replayInstance,
            state,
            NWB::Impl::DeformableSurfaceEditReplayContext{},
            &replayResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.finalEditRevision == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.editRevision == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.restVertices.size() == editedInstance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.indices.size() == editedInstance.indices.size());
}

static void TestSurfaceEditStateReplayOverlappingHoles(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(8u, 6u);
    cleanBase.editRevision = 0u;

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    u32 firstTriangle = Limit<u32>::s_Max;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        FindNearestUpFacingRestTriangle(editedInstance, Float3U(-0.75f, 0.0f, 0.0f), firstTriangle)
    );

    NWB::Impl::DeformableHoleEditResult firstHoleResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHole(
            editedInstance,
            firstTriangle,
            1.0f / 3.0f,
            1.0f / 3.0f,
            1.0f / 3.0f,
            0.55f,
            0.25f,
            state,
            &firstHoleResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, firstHoleResult.wallVertexCount >= 3u);

    u32 overlappingTriangle = Limit<u32>::s_Max;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        FindNearestUpFacingRestTriangle(editedInstance, Float3U(0.25f, 0.0f, 0.0f), overlappingTriangle)
    );

    NWB::Impl::DeformableHoleEditResult secondHoleResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHole(
            editedInstance,
            overlappingTriangle,
            1.0f / 3.0f,
            1.0f / 3.0f,
            1.0f / 3.0f,
            0.55f,
            0.25f,
            state,
            &secondHoleResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondHoleResult.removedTriangleCount != 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, secondHoleResult.wallVertexCount >= 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.editRevision == 2u);
    CheckRuntimeMeshPayloadValid(context, editedInstance);

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = cleanBase;
    replayInstance.handle.value = 364u;

    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ApplySurfaceEditState(
            replayInstance,
            state,
            NWB::Impl::DeformableSurfaceEditReplayContext{},
            &replayResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.finalEditRevision == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.restVertices.size() == editedInstance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.indices.size() == editedInstance.indices.size());
    CheckRuntimeMeshPayloadValid(context, replayInstance);
}

static void TestSurfaceEditUndoLastReplaysFromCleanBase(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(6u, 4u);
    cleanBase.editRevision = 0u;

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    NWB::Impl::DeformableHoleEditResult firstHoleResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHole(editedInstance, 12u, 0.48f, 0.25f, state, &firstHoleResult)
    );
    const usize firstVertexCount = editedInstance.restVertices.size();
    const usize firstIndexCount = editedInstance.indices.size();
    const NWB::Impl::DeformableSurfaceEditId firstEditId = state.edits[0].editId;

    NWB::Impl::DeformableAccessoryAttachmentComponent firstAttachment;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::AttachAccessory(editedInstance, firstHoleResult, 0.08f, 0.12f, firstAttachment)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        AppendAccessoryRecord(
            state,
            firstAttachment,
            firstEditId,
            s_MockAccessoryGeometryPath,
            s_MockAccessoryMaterialPath
        )
    );

    u32 secondTriangle = Limit<u32>::s_Max;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, FindSecondGridHoleTriangle(editedInstance, secondTriangle));
    RecordedHoleAccessory secondAccessory;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHoleAccessory(editedInstance, secondTriangle, 0.48f, 0.25f, 0.16f, 0.18f, state, secondAccessory)
    );
    const NWB::Impl::DeformableSurfaceEditId secondEditId = secondAccessory.editId;

    NWB::Impl::DeformableSurfaceEditUndoResult undoResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::UndoLastSurfaceEdit(editedInstance, cleanBase, state, &undoResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, undoResult.undoneEditId == secondEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, undoResult.removedAccessoryCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, undoResult.replay.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, undoResult.replay.finalEditRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].editId == firstEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].anchorEditId == firstEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.restVertices.size() == firstVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.indices.size() == firstIndexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.editRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        (editedInstance.dirtyFlags & NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u
    );

    NWB::Impl::DeformableSurfaceEditState emptyState;
    NWB::Impl::DeformableRuntimeMeshInstance unchangedInstance = cleanBase;
    const usize oldVertexCount = unchangedInstance.restVertices.size();
    const usize oldIndexCount = unchangedInstance.indices.size();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::UndoLastSurfaceEdit(unchangedInstance, cleanBase, emptyState)
    );
    CheckHoleEditUnchanged(context, unchangedInstance, oldVertexCount, oldIndexCount, 0u);
}

static void TestSurfaceEditRedoLastReplaysFromCleanBase(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(6u, 4u);
    cleanBase.editRevision = 0u;

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    RecordedTwoHoleAccessories accessories;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitTwoRecordedHoleAccessories(context, editedInstance, state, accessories));
    const NWB::Impl::DeformableSurfaceEditId firstEditId = accessories.first.editId;
    const NWB::Impl::DeformableSurfaceEditId secondEditId = accessories.second.editId;
    NWB::Core::Scene::TransformComponent expectedSecondTransform;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResolveAccessoryAttachmentTransform(
            editedInstance,
            NWB::Impl::DeformablePickingInputs{},
            accessories.second.attachment,
            expectedSecondTransform
        )
    );
    const usize fullVertexCount = editedInstance.restVertices.size();
    const usize fullIndexCount = editedInstance.indices.size();

    NWB::Impl::DeformableSurfaceEditHistory history;
    NWB::Impl::DeformableSurfaceEditUndoResult undoResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::UndoLastSurfaceEdit(editedInstance, cleanBase, state, &undoResult, &history)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, undoResult.undoneEditId == secondEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, history.redoStack.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, history.redoStack[0].edit.editId == secondEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, history.redoStack[0].accessories.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories.size() == 1u);

    NWB::Impl::DeformableSurfaceEditRedoResult redoResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::RedoLastSurfaceEdit(editedInstance, cleanBase, state, history, &redoResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, redoResult.redoneEditId == secondEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, redoResult.restoredAccessoryCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, redoResult.replay.appliedEditCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, redoResult.replay.finalEditRevision == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, redoResult.replay.topologyChanged);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, history.redoStack.empty());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].editId == firstEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[1].editId == secondEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories.size() == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[1].anchorEditId == secondEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[1].firstWallVertex == state.edits[1].result.firstWallVertex);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[1].wallVertexCount == state.edits[1].result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.restVertices.size() == fullVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.indices.size() == fullIndexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.editRevision == 2u);

    NWB::Core::Scene::TransformComponent resolvedTransform;
    ResolveRestoredAccessoryAttachmentTransform(context, editedInstance, state, 1u, resolvedTransform);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.position.x, expectedSecondTransform.position.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.position.y, expectedSecondTransform.position.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.position.z, expectedSecondTransform.position.z));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.scale.x, expectedSecondTransform.scale.x));

    const usize oldVertexCount = editedInstance.restVertices.size();
    const usize oldIndexCount = editedInstance.indices.size();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::RedoLastSurfaceEdit(editedInstance, cleanBase, state, history)
    );
    CheckHoleEditUnchanged(context, editedInstance, oldVertexCount, oldIndexCount, 2u);
}

static void TestSurfaceEditHealReplaysSurvivingEdits(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(6u, 4u);
    cleanBase.editRevision = 0u;

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    RecordedTwoHoleAccessories accessories;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitTwoRecordedHoleAccessories(context, editedInstance, state, accessories));
    const NWB::Impl::DeformableSurfaceEditId firstEditId = accessories.first.editId;
    const NWB::Impl::DeformableSurfaceEditId secondEditId = accessories.second.editId;
    const u32 oldSecondFirstWallVertex = state.accessories[1].firstWallVertex;

    NWB::Impl::DeformableSurfaceEditHealResult healResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::HealSurfaceEdit(editedInstance, cleanBase, state, firstEditId, &healResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, healResult.healedEditId == firstEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, healResult.removedAccessoryCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, healResult.replay.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, healResult.replay.finalEditRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].editId == secondEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].hole.baseEditRevision == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].result.editRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].anchorEditId == secondEditId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].firstWallVertex == state.edits[0].result.firstWallVertex);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].wallVertexCount == state.edits[0].result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].firstWallVertex != oldSecondFirstWallVertex);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.editRevision == 1u);

    NWB::Core::Scene::TransformComponent resolvedTransform;
    ResolveRestoredAccessoryAttachmentTransform(context, editedInstance, state, 0u, resolvedTransform);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedTransform.scale.x > 0.0f);

    NWB::Impl::DeformableRuntimeMeshInstance unchangedInstance = editedInstance;
    NWB::Impl::DeformableSurfaceEditState unchangedState = state;
    const usize oldVertexCount = unchangedInstance.restVertices.size();
    const usize oldIndexCount = unchangedInstance.indices.size();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::HealSurfaceEdit(unchangedInstance, cleanBase, unchangedState, 99u)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, unchangedState.edits.size() == state.edits.size());
    CheckHoleEditUnchanged(context, unchangedInstance, oldVertexCount, oldIndexCount, 1u);
}

static void TestSurfaceEditResizeReplaysFromCleanBase(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(6u, 4u);
    cleanBase.editRevision = 0u;

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    RecordedHoleAccessory accessory;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHoleAccessory(editedInstance, 12u, 0.38f, 0.25f, 0.09f, 0.13f, state, accessory)
    );
    const NWB::Impl::DeformableSurfaceEditId editId = accessory.editId;
    const f32 oldRadius = state.edits[0].hole.radius;
    const f32 oldDepth = state.edits[0].hole.depth;

    NWB::Impl::DeformableSurfaceEditResizeResult resizeResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResizeSurfaceEdit(
            editedInstance,
            cleanBase,
            state,
            editId,
            0.58f,
            1.0f,
            0.31f,
            &resizeResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resizeResult.resizedEditId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resizeResult.oldRadius, oldRadius));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resizeResult.oldDepth, oldDepth));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resizeResult.newRadius, 0.58f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resizeResult.newDepth, 0.31f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resizeResult.replay.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resizeResult.replay.finalEditRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].editId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.radius, 0.58f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.depth, 0.31f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].anchorEditId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].firstWallVertex == state.edits[0].result.firstWallVertex);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].wallVertexCount == state.edits[0].result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.editRevision == 1u);

    NWB::Core::Scene::TransformComponent resolvedTransform;
    ResolveRestoredAccessoryAttachmentTransform(context, editedInstance, state, 0u, resolvedTransform);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.position.z, 0.09f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.scale.x, 0.13f));

    NWB::Impl::DeformableRuntimeMeshInstance unchangedInstance = editedInstance;
    NWB::Impl::DeformableSurfaceEditState unchangedState = state;
    const usize oldVertexCount = unchangedInstance.restVertices.size();
    const usize oldIndexCount = unchangedInstance.indices.size();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::ResizeSurfaceEdit(unchangedInstance, cleanBase, unchangedState, editId, -0.1f, 1.0f, 0.31f)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(unchangedState.edits[0].hole.radius, 0.58f));
    CheckHoleEditUnchanged(context, unchangedInstance, oldVertexCount, oldIndexCount, 1u);
}

static void TestSurfaceEditMoveReplaysFromCleanBase(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(6u, 4u);
    cleanBase.editRevision = 0u;

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    RecordedHoleAccessory accessory;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHoleAccessory(editedInstance, 12u, 0.38f, 0.25f, 0.11f, 0.14f, state, accessory)
    );
    const NWB::Impl::DeformableSurfaceEditId editId = accessory.editId;
    const Float3U oldRestPosition = state.edits[0].hole.restPosition;

    u32 moveTriangle = Limit<u32>::s_Max;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, FindSecondGridHoleTriangle(editedInstance, moveTriangle));
    const NWB::Impl::DeformableHoleEditParams moveParams =
        MakeHoleEditParams(editedInstance, moveTriangle, 0.25f, 0.25f, 0.5f, 0.38f, 0.25f)
    ;
    const Float3U targetRestPosition = RestHitPosition(editedInstance, moveParams);
    NWB::Impl::DeformableSurfaceEditSession moveSession;
    NWB::Impl::DeformableHolePreview movePreview;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BeginSurfaceEdit(editedInstance, moveParams.posedHit, moveSession));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::PreviewHole(editedInstance, moveSession, moveParams, movePreview));

    NWB::Impl::DeformableSurfaceEditMoveResult moveResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::MoveSurfaceEdit(
            editedInstance,
            cleanBase,
            state,
            editId,
            moveParams.posedHit,
            &moveResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, moveResult.movedEditId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(moveResult.oldRestPosition.x, oldRestPosition.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(moveResult.oldRestPosition.y, oldRestPosition.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(moveResult.newRestPosition.x, targetRestPosition.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(moveResult.newRestPosition.y, targetRestPosition.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NearlyEqual(moveResult.newRestPosition.x, oldRestPosition.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, moveResult.replay.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, moveResult.replay.finalEditRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].editId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.restPosition.x, targetRestPosition.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.restPosition.y, targetRestPosition.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.radius, 0.38f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.depth, 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].anchorEditId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].firstWallVertex == state.edits[0].result.firstWallVertex);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].wallVertexCount == state.edits[0].result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.editRevision == 1u);

    NWB::Core::Scene::TransformComponent resolvedTransform;
    ResolveRestoredAccessoryAttachmentTransform(context, editedInstance, state, 0u, resolvedTransform);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.position.z, 0.11f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.scale.x, 0.14f));

    NWB::Impl::DeformableRuntimeMeshInstance unchangedInstance = editedInstance;
    NWB::Impl::DeformableSurfaceEditState unchangedState = state;
    NWB::Impl::DeformablePosedHit badHit = moveParams.posedHit;
    badHit.editRevision = 99u;
    const usize oldVertexCount = unchangedInstance.restVertices.size();
    const usize oldIndexCount = unchangedInstance.indices.size();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::MoveSurfaceEdit(unchangedInstance, cleanBase, unchangedState, editId, badHit)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(unchangedState.edits[0].hole.restPosition.x, targetRestPosition.x));
    CheckHoleEditUnchanged(context, unchangedInstance, oldVertexCount, oldIndexCount, 1u);
}

static void TestSurfaceEditPatchReplaysFromCleanBase(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(6u, 4u);
    cleanBase.editRevision = 0u;

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    RecordedHoleAccessory accessory;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHoleAccessory(editedInstance, 12u, 0.38f, 0.25f, 0.12f, 0.15f, state, accessory)
    );
    const NWB::Impl::DeformableSurfaceEditId editId = accessory.editId;
    const Float3U oldRestPosition = state.edits[0].hole.restPosition;
    const f32 oldRadius = state.edits[0].hole.radius;
    const f32 oldDepth = state.edits[0].hole.depth;

    u32 patchTriangle = Limit<u32>::s_Max;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, FindSecondGridHoleTriangle(editedInstance, patchTriangle));
    const NWB::Impl::DeformableHoleEditParams patchParams =
        MakeHoleEditParams(editedInstance, patchTriangle, 0.25f, 0.25f, 0.5f, 0.48f, 0.31f)
    ;
    const Float3U targetRestPosition = RestHitPosition(editedInstance, patchParams);

    NWB::Impl::DeformableSurfaceEditPatchResult patchResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::PatchSurfaceEdit(
            editedInstance,
            cleanBase,
            state,
            editId,
            patchParams.posedHit,
            0.48f,
            1.0f,
            0.31f,
            &patchResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, patchResult.patchedEditId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(patchResult.oldRestPosition.x, oldRestPosition.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(patchResult.oldRestPosition.y, oldRestPosition.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(patchResult.newRestPosition.x, targetRestPosition.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(patchResult.newRestPosition.y, targetRestPosition.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !NearlyEqual(patchResult.newRestPosition.x, oldRestPosition.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(patchResult.oldRadius, oldRadius));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(patchResult.oldDepth, oldDepth));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(patchResult.newRadius, 0.48f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(patchResult.newDepth, 0.31f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, patchResult.replay.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, patchResult.replay.finalEditRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, patchResult.replay.topologyChanged);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].editId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.restPosition.x, targetRestPosition.x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.restPosition.y, targetRestPosition.y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.radius, 0.48f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(state.edits[0].hole.depth, 0.31f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].anchorEditId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].firstWallVertex == state.edits[0].result.firstWallVertex);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].wallVertexCount == state.edits[0].result.wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.editRevision == 1u);

    NWB::Core::Scene::TransformComponent resolvedTransform;
    ResolveRestoredAccessoryAttachmentTransform(context, editedInstance, state, 0u, resolvedTransform);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.position.z, 0.12f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.scale.x, 0.15f));

    NWB::Impl::DeformableRuntimeMeshInstance unchangedInstance = editedInstance;
    NWB::Impl::DeformableSurfaceEditState unchangedState = state;
    NWB::Impl::DeformablePosedHit badHit = patchParams.posedHit;
    badHit.editRevision = 99u;
    const usize oldVertexCount = unchangedInstance.restVertices.size();
    const usize oldIndexCount = unchangedInstance.indices.size();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::PatchSurfaceEdit(unchangedInstance, cleanBase, unchangedState, editId, badHit, 0.48f, 1.0f, 0.31f)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(unchangedState.edits[0].hole.restPosition.x, targetRestPosition.x));
    CheckHoleEditUnchanged(context, unchangedInstance, oldVertexCount, oldIndexCount, 1u);
}

static void TestSurfaceEditLoopCutReplaysFromCleanBase(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(6u, 4u);
    cleanBase.editRevision = 0u;

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    RecordedHoleAccessory accessory;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHoleAccessory(editedInstance, 12u, 0.38f, 0.25f, 0.12f, 0.15f, state, accessory)
    );
    const NWB::Impl::DeformableSurfaceEditId editId = accessory.editId;
    const u32 wallVertexCount = state.edits[0].result.wallVertexCount;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, wallVertexCount >= 3u);

    NWB::Impl::DeformableSurfaceEditLoopCutResult loopCutResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::AddSurfaceEditLoopCut(
            editedInstance,
            cleanBase,
            state,
            editId,
            &loopCutResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loopCutResult.loopCutEditId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loopCutResult.oldLoopCutCount == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loopCutResult.newLoopCutCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loopCutResult.replay.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loopCutResult.replay.finalEditRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loopCutResult.replay.topologyChanged);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].editId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].hole.wallLoopCutCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].result.wallLoopCutCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].result.wallVertexCount == wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        state.edits[0].result.addedVertexCount == ExpectedCappedHoleAddedVertexCount(wallVertexCount, 1u)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        state.edits[0].result.addedTriangleCount == ExpectedCappedHoleAddedTriangleCount(wallVertexCount, 1u)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        state.edits[0].result.firstWallVertex >= cleanBase.restVertices.size()
            && state.edits[0].result.firstWallVertex < editedInstance.restVertices.size()
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].anchorEditId == editId);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].firstWallVertex == state.edits[0].result.firstWallVertex);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.accessories[0].wallVertexCount == wallVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, editedInstance.editRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        editedInstance.restVertices.size()
            >= cleanBase.restVertices.size() + state.edits[0].result.addedVertexCount
    );

    NWB::Core::Scene::TransformComponent resolvedTransform;
    ResolveRestoredAccessoryAttachmentTransform(context, editedInstance, state, 0u, resolvedTransform);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.position.z, 0.12f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedTransform.scale.x, 0.15f));

    NWB::Core::Assets::AssetBytes binary;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::SerializeSurfaceEditState(state, binary));
    NWB::Impl::DeformableSurfaceEditState loadedState;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NWB::Impl::DeserializeSurfaceEditState(binary, loadedState));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.edits[0].hole.wallLoopCutCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.edits[0].result.wallLoopCutCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loadedState.accessories[0].firstWallVertex == state.accessories[0].firstWallVertex);

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState replayState = loadedState;
    replayState.accessories.clear();
    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ApplySurfaceEditState(
            replayInstance,
            replayState,
            NWB::Impl::DeformableSurfaceEditReplayContext{},
            &replayResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.editRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.restVertices.size() == editedInstance.restVertices.size());

    NWB::Impl::DeformableRuntimeMeshInstance unchangedInstance = editedInstance;
    NWB::Impl::DeformableSurfaceEditState unchangedState = state;
    unchangedState.edits[0].hole.wallLoopCutCount = 8u;
    unchangedState.edits[0].result.wallLoopCutCount = 8u;
    unchangedState.edits[0].result.addedVertexCount = ExpectedCappedHoleAddedVertexCount(wallVertexCount, 8u);
    unchangedState.edits[0].result.addedTriangleCount = ExpectedCappedHoleAddedTriangleCount(wallVertexCount, 8u);
    unchangedState.edits[0].result.firstWallVertex =
        static_cast<u32>(cleanBase.restVertices.size() + (static_cast<usize>(wallVertexCount) * 8u))
    ;
    unchangedState.accessories[0].firstWallVertex = unchangedState.edits[0].result.firstWallVertex;
    const usize oldVertexCount = unchangedInstance.restVertices.size();
    const usize oldIndexCount = unchangedInstance.indices.size();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::AddSurfaceEditLoopCut(unchangedInstance, cleanBase, unchangedState, editId)
    );
    CheckHoleEditUnchanged(context, unchangedInstance, oldVertexCount, oldIndexCount, 1u);
}

static void TestSurfaceEditRepeatedOperationsKeepMeshPayloadValid(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(6u, 4u);
    cleanBase.editRevision = 0u;

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    NWB::Impl::DeformableHoleEditResult holeResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHole(editedInstance, 12u, 0.38f, 0.25f, state, &holeResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits.size() == 1u);
    const NWB::Impl::DeformableSurfaceEditId editId = state.edits[0].editId;
    CheckRuntimeMeshPayloadValid(context, editedInstance);

    NWB::Impl::DeformableSurfaceEditResizeResult resizeResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ResizeSurfaceEdit(
            editedInstance,
            cleanBase,
            state,
            editId,
            0.48f,
            1.0f,
            0.30f,
            &resizeResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resizeResult.replay.appliedEditCount == 1u);
    CheckRuntimeMeshPayloadValid(context, editedInstance);
    SimulateRuntimeMeshUpload(editedInstance);

    u32 moveTriangle = Limit<u32>::s_Max;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, FindSecondGridHoleTriangle(editedInstance, moveTriangle));
    const NWB::Impl::DeformableHoleEditParams moveParams =
        MakeHoleEditParams(editedInstance, moveTriangle, 0.25f, 0.25f, 0.5f, 0.48f, 0.30f)
    ;
    NWB::Impl::DeformableSurfaceEditMoveResult moveResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::MoveSurfaceEdit(
            editedInstance,
            cleanBase,
            state,
            editId,
            moveParams.posedHit,
            &moveResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, moveResult.replay.appliedEditCount == 1u);
    CheckRuntimeMeshPayloadValid(context, editedInstance);
    SimulateRuntimeMeshUpload(editedInstance);

    const NWB::Impl::DeformableHoleEditParams patchParams =
        MakeHoleEditParams(editedInstance, 12u, 0.25f, 0.25f, 0.5f, 0.42f, 0.22f)
    ;
    NWB::Impl::DeformableSurfaceEditPatchResult patchResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::PatchSurfaceEdit(
            editedInstance,
            cleanBase,
            state,
            editId,
            patchParams.posedHit,
            0.42f,
            1.0f,
            0.22f,
            &patchResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, patchResult.replay.appliedEditCount == 1u);
    CheckRuntimeMeshPayloadValid(context, editedInstance);
    SimulateRuntimeMeshUpload(editedInstance);

    NWB::Impl::DeformableSurfaceEditLoopCutResult loopCutResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::AddSurfaceEditLoopCut(
            editedInstance,
            cleanBase,
            state,
            editId,
            &loopCutResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, loopCutResult.replay.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, state.edits[0].hole.wallLoopCutCount == 1u);
    CheckRuntimeMeshPayloadValid(context, editedInstance);

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ApplySurfaceEditState(
            replayInstance,
            state,
            NWB::Impl::DeformableSurfaceEditReplayContext{},
            &replayResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.restVertices.size() == editedInstance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.indices.size() == editedInstance.indices.size());
    CheckRuntimeMeshPayloadValid(context, replayInstance);
}

static void TestSurfaceEditReplayKeepsMorphSkinDisplacementUsable(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance cleanBase = MakeGridHoleInstance(6u, 4u);
    cleanBase.editRevision = 0u;
    cleanBase.displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarTexture;
    cleanBase.displacement.texture.virtualPath = Name("tests/textures/replayed_deformable_displacement");
    cleanBase.displacement.amplitude = 0.1f;

    for(NWB::Impl::DeformableVertexRest& vertex : cleanBase.restVertices)
        vertex.uv0 = Float2U((vertex.position.x + 2.5f) / 5.0f, 0.0f);

    NWB::Impl::DeformableMorph liftMorph;
    liftMorph.name = Name("replay_lift");
    liftMorph.deltas.reserve(cleanBase.restVertices.size());
    for(u32 vertexId = 0u; vertexId < static_cast<u32>(cleanBase.restVertices.size()); ++vertexId){
        NWB::Impl::DeformableMorphDelta delta{};
        delta.vertexId = vertexId;
        delta.deltaPosition = Float3U(0.0f, 0.0f, 0.08f);
        liftMorph.deltas.push_back(delta);
    }
    cleanBase.morphs.push_back(liftMorph);

    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = cleanBase;
    NWB::Impl::DeformableSurfaceEditState state;

    RecordedTwoHoleAccessories accessories;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitTwoRecordedHoleAccessories(context, editedInstance, state, accessories));

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = cleanBase;
    replayInstance.handle.value = 744u;
    SurfaceEditReplayFixture replayFixture;
    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    if(!ApplySurfaceEditReplayFixture(context, state, replayInstance, replayFixture, replayResult))
        return;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.restoredAccessoryCount == 2u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.editRevision == 2u);
    CheckRuntimeMeshPayloadValid(context, replayInstance);

    SimulateRuntimeMeshUpload(replayInstance);
    NWB::Impl::DeformableDisplacementTexture texture = MakeTestDisplacementTexture(
        "tests/textures/replayed_deformable_displacement",
        Float4U(0.0f, 0.0f, 0.0f, 0.0f),
        Float4U(0.5f, 0.0f, 0.0f, 0.0f),
        Float4U(1.0f, 0.0f, 0.0f, 0.0f)
    );
    NWB::Impl::DeformablePickingInputs accessoryInputs;
    accessoryInputs.displacementTexture = &texture;

    u32 restoredAccessoryCount = 0u;
    replayFixture.world.world.view<NWB::Impl::DeformableAccessoryAttachmentComponent>().each(
        [&](NWB::Core::ECS::EntityID, NWB::Impl::DeformableAccessoryAttachmentComponent& restored){
            ++restoredAccessoryCount;
            NWB::Core::Scene::TransformComponent resolvedTransform;
            NWB_ECS_GRAPHICS_TEST_CHECK(
                context,
                NWB::Impl::ResolveAccessoryAttachmentTransform(
                    replayInstance,
                    accessoryInputs,
                    restored,
                    resolvedTransform
                )
            );
            NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedTransform.scale.x > 0.0f);
        }
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, restoredAccessoryCount == 2u);

    NWB::Impl::DeformableMorphWeightsComponent weights;
    weights.weights.push_back(NWB::Impl::DeformableMorphWeight{ Name("replay_lift"), 0.5f });

    NWB::Impl::DeformableJointPaletteComponent joints;
    joints.joints.resize(1u, NWB::Impl::MakeIdentityDeformableJointMatrix());
    joints.joints[0] = MakeIdentityJointMatrix();
    joints.joints[0].rows[3] = Float4(0.25f, -0.125f, 0.2f, 1.0f);

    NWB::Impl::DeformablePickingInputs inputs;
    inputs.morphWeights = &weights;
    inputs.jointPalette = &joints;
    inputs.displacementTexture = &texture;

    NWB::Impl::DeformableDisplacementComponent disabledDisplacement;
    disabledDisplacement.enabled = false;
    NWB::Impl::DeformablePickingInputs controlInputs = inputs;
    controlInputs.displacement = &disabledDisplacement;

    Vector<NWB::Impl::DeformableVertexRest> controlVertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::BuildDeformablePickingVertices(replayInstance, controlInputs, controlVertices)
    );
    Vector<NWB::Impl::DeformableVertexRest> posedVertices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::BuildDeformablePickingVertices(replayInstance, inputs, posedVertices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, controlVertices.size() == replayInstance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, posedVertices.size() == replayInstance.restVertices.size());
    if(controlVertices.size() != replayInstance.restVertices.size() || posedVertices.size() != replayInstance.restVertices.size())
        return;

    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(controlVertices[0].position.x, replayInstance.restVertices[0].position.x + 0.25f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(controlVertices[0].position.y, replayInstance.restVertices[0].position.y - 0.125f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(controlVertices[0].position.z, replayInstance.restVertices[0].position.z + 0.24f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, posedVertices[5u].position.z > controlVertices[5u].position.z + 0.05f);

    for(const NWB::Impl::DeformableVertexRest& vertex : posedVertices){
        NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat3(vertex.position));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat3(vertex.normal));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, FiniteFloat4(vertex.tangent));
    }
}

static void TestSurfaceEditStateReplayTriesLaterMatchingCandidate(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = MakeGridHoleInstance();
    editedInstance.editRevision = 0u;
    NWB::Impl::DeformableSurfaceEditState state;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitRecordedHole(editedInstance, 3u, 0.5f, 0.0f, 0.5f, 0.10f, 0.25f, state)
    );

    NWB::Impl::DeformableRuntimeMeshInstance replayInstance = MakeGridHoleInstance();
    replayInstance.editRevision = 0u;
    replayInstance.handle.value = 444u;

    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::ApplySurfaceEditState(
            replayInstance,
            state,
            NWB::Impl::DeformableSurfaceEditReplayContext{},
            &replayResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.finalEditRevision == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.restVertices.size() == editedInstance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayInstance.indices.size() == editedInstance.indices.size());
}

static void TestSurfaceEditStateReplayRejectsWrongSourceMesh(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance editedInstance = MakeGridHoleInstance();
    editedInstance.editRevision = 0u;
    NWB::Impl::DeformableSurfaceEditState state;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, CommitRecordedHole(editedInstance, 8u, 0.48f, 0.25f, state));

    NWB::Impl::DeformableRuntimeMeshInstance wrongInstance = MakeGridHoleInstance();
    wrongInstance.sourceTriangleCount = 2u;
    for(NWB::Impl::SourceSample& sample : wrongInstance.sourceSamples)
        sample = MakeSourceSample(1u, 1.0f, 0.0f, 0.0f);
    wrongInstance.editRevision = 0u;
    const usize oldVertexCount = wrongInstance.restVertices.size();
    const usize oldIndexCount = wrongInstance.indices.size();

    NWB::Impl::DeformableSurfaceEditReplayResult replayResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::ApplySurfaceEditState(
            wrongInstance,
            state,
            NWB::Impl::DeformableSurfaceEditReplayContext{},
            &replayResult
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, replayResult.appliedEditCount == 0u);
    CheckHoleEditUnchanged(context, wrongInstance, oldVertexCount, oldIndexCount, 0u);
}

static void TestRestSpaceHoleEditRejectsMissingProvenance(TestContext& context){
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.sourceTriangleCount = static_cast<u32>(instance.indices.size() / 3u);
            instance.sourceSamples.clear();
            instance.editRevision = 0u;
        }
    );
}

static void TestRestSpaceHoleEditRejectsMissingProvenanceWithoutSourceTriangleCount(TestContext& context){
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.sourceTriangleCount = 0u;
            instance.sourceSamples.clear();
            instance.editRevision = 0u;
        }
    );
}

static void TestRestSpaceHoleEditRejectsMixedProvenance(TestContext& context){
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            AssignFirstUseTriangleSourceSamples(instance);
            instance.editRevision = 0u;
        }
    );
}

static void TestRestSpaceHoleEditRejectsOpenBoundaryPatch(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTriangleInstance();
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();
    const u32 oldRevision = instance.editRevision;

    const NWB::Impl::DeformableHoleEditParams params = MakeHoleEditParams(instance, 0u, 0.25f, 0.25f);

    NWB_ECS_GRAPHICS_TEST_CHECK(context, !CommitPreviewedHole(instance, params));
    CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);

    NWB::Impl::DeformableRuntimeMeshInstance borderGrid = MakeGridHoleInstance();
    const usize oldGridVertexCount = borderGrid.restVertices.size();
    const usize oldGridIndexCount = borderGrid.indices.size();
    const u32 oldGridRevision = borderGrid.editRevision;
    const NWB::Impl::DeformableHoleEditParams borderParams = MakeHoleEditParams(borderGrid, 0u, 0.55f, 0.25f);

    NWB::Impl::DeformableHoleEditResult borderResult;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        CommitPreviewedHole(borderGrid, borderParams, &borderResult)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, borderResult.removedTriangleCount != 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, borderGrid.restVertices.size() > oldGridVertexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, borderGrid.indices.size() > oldGridIndexCount);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, borderGrid.editRevision == oldGridRevision + 1u);
}

static void TestRestSpaceHoleEditRejectsHardBoundaryPatch(TestContext& context){
    NWB::Impl::DeformableRuntimeMeshInstance instance = MakeTetrahedronHoleInstance();
    const usize oldVertexCount = instance.restVertices.size();
    const usize oldIndexCount = instance.indices.size();
    const u32 oldRevision = instance.editRevision;
    const NWB::Impl::DeformableHoleEditParams params = MakeHoleEditParams(instance, 0u, 0.08f, 0.25f);

    NWB_ECS_GRAPHICS_TEST_CHECK(context, !CommitPreviewedHole(instance, params));
    CheckHoleEditUnchanged(context, instance, oldVertexCount, oldIndexCount, oldRevision);
}

static void TestRestSpaceHoleEditRejectsDegenerateHitFrame(TestContext& context){
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.restVertices[10u].position = instance.restVertices[6u].position;
        }
    );
}

static void TestRestSpaceHoleEditRejectsNonFiniteWallVertices(TestContext& context){
    CheckGridHoleEditRejectsMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            for(NWB::Impl::DeformableVertexRest& vertex : instance.restVertices)
                vertex.position.z = -Limit<f32>::s_Max;
        },
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.depth = Limit<f32>::s_Max;
        }
    );
}

static void TestRestSpaceHoleEditRejectsInvalidAttributeStreams(TestContext& context){
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.skin[0].weight[0] = 0.5f;
        }
    );
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.skeletonJointCount = 0u;
        }
    );
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.skin[0u] = MakeSingleJointSkin(1u);
        }
    );
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.sourceSamples[0].bary[0] = -0.0000005f;
            instance.sourceSamples[0].bary[1] = 1.0000005f;
        }
    );
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.sourceSamples[0].sourceTri = 99u;
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.posedHit.restSample.bary[0] = 0.0f;
            params.posedHit.restSample.bary[1] = 1.0f;
            params.posedHit.restSample.bary[2] = 0.0f;
        }
    );
}

static void TestRestSpaceHoleEditRejectsMalformedRuntimePayload(TestContext& context){
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.restVertices[0u].normal = Float3U(0.0f, 0.0f, 0.0f);
        }
    );
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.indices[2u] = instance.indices[1u];
        }
    );
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            NWB::Impl::DeformableMorph duplicateMorph;
            duplicateMorph.name = Name("duplicate");
            NWB::Impl::DeformableMorphDelta delta{};
            delta.vertexId = 5u;
            duplicateMorph.deltas.push_back(delta);
            duplicateMorph.deltas.push_back(delta);
            instance.morphs.push_back(duplicateMorph);
        }
    );
}

static void TestRestSpaceHoleEditRejectsInvalidDisplacementDescriptor(TestContext& context){
    CheckGridHoleEditRejectsInstanceMutation(
        context,
        [](NWB::Impl::DeformableRuntimeMeshInstance& instance){
            instance.displacement.mode = 99u;
            instance.displacement.amplitude = 1.0f;
        }
    );
}

static void TestRestSpaceHoleEditRejectsStaleOrMismatchedHit(TestContext& context){
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.posedHit.entity = NWB::Core::ECS::ENTITY_ID_INVALID;
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.posedHit.entity = NWB::Core::ECS::EntityID(99u, 0u);
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.posedHit.runtimeMesh.reset();
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            ++params.posedHit.runtimeMesh.value;
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            ++params.posedHit.editRevision;
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.posedHit.position.x = Limit<f32>::s_QuietNaN;
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.posedHit.position.w = 0.0f;
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.posedHit.setNormal(Float3U(0.0f, 0.0f, 0.0f));
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.posedHit.normal.w = 1.0f;
        }
    );
    CheckGridHoleEditRejectsParamMutation(
        context,
        [](NWB::Impl::DeformableHoleEditParams& params){
            params.posedHit.setDistance(-1.0f);
        }
    );
}

#undef NWB_ECS_GRAPHICS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    return NWB::Tests::RunTestSuite("ecs graphics", [](NWB::Tests::TestContext& context){
        __hidden_ecs_graphics_tests::CapturingLogger logger;
        NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);

        __hidden_ecs_graphics_tests::TestRuntimeResourceNameBuilderMatchesFormattedSuffix(context);
        __hidden_ecs_graphics_tests::TestRestSampleInterpolation(context);
        __hidden_ecs_graphics_tests::TestMixedProvenanceRejectsAmbiguousRestTriangle(context);
        __hidden_ecs_graphics_tests::TestMixedProvenanceRejectsRuntimeTriangleOutsideSourceRange(context);
        __hidden_ecs_graphics_tests::TestMixedProvenanceRejectsAfterTopologyChange(context);
        __hidden_ecs_graphics_tests::TestMissingProvenanceRejectsAfterTopologyChange(context);
        __hidden_ecs_graphics_tests::TestMissingProvenanceRejectsWhenSourceCountDiffers(context);
        __hidden_ecs_graphics_tests::TestMissingProvenanceRejectsWithoutSourceCount(context);
        __hidden_ecs_graphics_tests::TestMixedProvenanceRejectsWhenEditedTriangleCountDiffers(context);
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
        __hidden_ecs_graphics_tests::TestPickingSkinAppliesInverseBindMatrix(context);
        __hidden_ecs_graphics_tests::TestPickingSkinUsesNormalMatrixForNonUniformScale(context);
        __hidden_ecs_graphics_tests::TestPickingSkinBlendsTwoJoints(context);
        __hidden_ecs_graphics_tests::TestJointRotationQuaternionBuildsColumnVectorRotations(context);
        __hidden_ecs_graphics_tests::TestPickingDualQuaternionSkinPreservesTwist(context);
        __hidden_ecs_graphics_tests::TestPickingDualQuaternionSkinRejectsScaledPalette(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsSkinJointOutsidePalette(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsSkinJointOutsideSkeleton(context);
        __hidden_ecs_graphics_tests::TestPickingUsesEntityTransform(context);
        __hidden_ecs_graphics_tests::TestPickingIgnoresJointPaletteForUnskinnedMesh(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsNonAffineJointPalette(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsSingularJointPalette(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsUnusedNonAffineJointPalette(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsInvalidSkinWeights(context);
        __hidden_ecs_graphics_tests::TestPickingVerticesIncludeMorphAndDisplacement(context);
        __hidden_ecs_graphics_tests::TestPickingVerticesIncludeTextureDisplacement(context);
        __hidden_ecs_graphics_tests::TestPickingScalarTextureDisplacementUpdatesNormal(context);
        __hidden_ecs_graphics_tests::TestPickingVectorTextureDisplacementUpdatesNormal(context);
        __hidden_ecs_graphics_tests::TestDeformerCpuReferenceEvaluationModes(context);
        __hidden_ecs_graphics_tests::TestPickingVerticesLoadTextureDisplacementFromAssetManager(context);
        __hidden_ecs_graphics_tests::TestPickingTextureDisplacementCanBeDisabled(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsInvalidDisplacementDescriptor(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsInvalidMorphDelta(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsActiveEmptyMorph(context);
        __hidden_ecs_graphics_tests::TestPickingRejectsNonFiniteEvaluatedVertices(context);
        __hidden_ecs_graphics_tests::TestPickingRepairsOverflowedMorphFrame(context);
        __hidden_ecs_graphics_tests::TestDeformerMorphPayloadPreblendsDuplicateWeightsAndMorphs(context);
        __hidden_ecs_graphics_tests::TestDeformerMorphPayloadSignatureChangesWithWeights(context);
        __hidden_ecs_graphics_tests::TestDeformerMorphPayloadSignatureChangesWithEditRevision(context);
        __hidden_ecs_graphics_tests::TestDeformerMorphPayloadBuildsSparseVertexRanges(context);
        __hidden_ecs_graphics_tests::TestSkeletonPoseBuildsHierarchicalPalette(context);
        __hidden_ecs_graphics_tests::TestPickingSkeletonPoseAppliesHierarchicalPalette(context);
        __hidden_ecs_graphics_tests::TestDeformerSkinPayloadValidatesSkeletonAndPalette(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditCreatesPerInstancePatch(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditTransfersAndInpaintsWallAttributes(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditWallTrianglesKeepRecoverableProvenance(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditMutatesOnlyTargetRuntimeInstance(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditMasksPreviewAndCommit(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditPreviewIsReadOnlyAndCommitMutatesTopology(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditOperatorFootprintIsDerivedFromGeometry(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditOperatorFootprintDrivesPreviewAndCommit(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditOperatorFootprintRemeshesIntersectedTriangles(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditOperatorVolumeDepthGatesCurvedSurface(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditOperatorRemeshIgnoresTrianglesOutsideDepth(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditOperatorProfileTapersWallGeometry(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditDebugSnapshotCapturesPreviewAndWallVertices(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditFlowAttachesAndPersistsAccessory(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayEmptyStateIsNoOp(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayRejectsMismatchedTargetEntity(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayOneHole(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayRestoresAccessory(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayRejectsInvalidAccessoryAsset(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayRestoresMultipleAccessories(context);
        __hidden_ecs_graphics_tests::TestMinimalMilestoneReplayPreservesAnimatedPayload(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayTwoHoles(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayOverlappingHoles(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditUndoLastReplaysFromCleanBase(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditRedoLastReplaysFromCleanBase(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditHealReplaysSurvivingEdits(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditResizeReplaysFromCleanBase(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditMoveReplaysFromCleanBase(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditPatchReplaysFromCleanBase(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditLoopCutReplaysFromCleanBase(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditRepeatedOperationsKeepMeshPayloadValid(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditReplayKeepsMorphSkinDisplacementUsable(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayTriesLaterMatchingCandidate(context);
        __hidden_ecs_graphics_tests::TestSurfaceEditStateReplayRejectsWrongSourceMesh(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsMissingProvenance(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsMissingProvenanceWithoutSourceTriangleCount(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsMixedProvenance(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsOpenBoundaryPatch(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsHardBoundaryPatch(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsDegenerateHitFrame(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsNonFiniteWallVertices(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsInvalidAttributeStreams(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsMalformedRuntimePayload(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsInvalidDisplacementDescriptor(context);
        __hidden_ecs_graphics_tests::TestRestSpaceHoleEditRejectsStaleOrMismatchedHit(context);
    });
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
