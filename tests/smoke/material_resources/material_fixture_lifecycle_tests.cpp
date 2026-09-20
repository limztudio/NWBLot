// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/smoke/descriptor_buffer/round_trip/round_trip_fixture.h>
#include <tests/common/ecs_test_world.h>

#include <impl/assets_material/binary_payload.h>
#include <impl/ecs_csg/shape_registry.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/csg/renderer_csg_state.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/shader/shader_system.h>

#include <core/assets/manager.h>
#include <global/binary.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_fixture_lifecycle_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;

inline constexpr Name s_MaterialName("project/tests/material_fixture_lifecycle");
inline constexpr u32 s_UnpatchedSlot = Limit<u32>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The production codec validates this small MTL9 payload; no cooker or authored shader compilation is needed to exercise material ownership.
[[nodiscard]] bool MakeFixtureMaterialBinary(Assets::AssetBytes& binary){
    const Array<MaterialTypedLayoutBlock, 1u> blocks = {{
        {Name("surface"), MaterialBlockClass::MaterialConstant, 0u, 2u, 8u},
    }};
    const Array<MaterialTypedLayoutField, 2u> fields = {{
        {Name("image"), MaterialLayoutFieldType::SampledImage2D, 0u, {{s_UnpatchedSlot, 0u, 0u, 0u}}},
        {Name("sampler"), MaterialLayoutFieldType::Sampler, 4u, {{s_UnpatchedSlot, 0u, 0u, 0u}}},
    }};
    binary.clear();
    AppendPOD(binary, MaterialBinaryPayload::s_MaterialMagic);
    if(!AppendString(binary, "default"))
        return false;
    AppendPOD(binary, Name("project/tests/material_fixture_interface").hash());
    AppendPOD(binary, MaterialBinaryPayload::ComputeMaterialTypedLayoutHash(blocks, fields));
    AppendPOD(binary, u32(1u));
    AppendPOD(binary, u32(2u));
    const MaterialBinaryPayload::MaterialTypedLayoutBlockBinary block = {
        blocks[0u].blockName.hash(), MaterialBlockClass::MaterialConstant, 0u, 2u, 8u,
    };
    AppendPOD(binary, block);
    for(const auto& field : fields){
        const MaterialBinaryPayload::MaterialTypedLayoutFieldBinary encoded = {
            field.fieldName.hash(), static_cast<u32>(field.fieldType), field.offset, field.defaultValue,
        };
        AppendPOD(binary, encoded);
    }
    AppendPOD(binary, u32(8u));
    AppendPOD(binary, s_UnpatchedSlot);
    AppendPOD(binary, s_UnpatchedSlot);
    AppendPOD(binary, u32(2u));
    const Array<MaterialBinaryPayload::MaterialResourceReferenceBinary, 2u> references = {{
        {
            blocks[0u].blockName.hash(), fields[0u].fieldName.hash(), {},
            Name(MaterialResourceFixture::s_CheckerRgba8).hash(), MaterialResourceKind::SampledImage2D,
            MaterialResourceSource::Asset, 0u, 0u,
        },
        {
            blocks[0u].blockName.hash(), fields[1u].fieldName.hash(), {},
            Name(MaterialResourceFixture::s_LinearClamp).hash(), MaterialResourceKind::Sampler,
            MaterialResourceSource::Asset, 4u, 0u,
        },
    }};
    for(const auto& reference : references)
        AppendPOD(binary, reference);
    AppendPOD(binary, u32(2u));
    AppendPOD(binary, ShaderType::MeshStage);
    AppendPOD(binary, Name("project/tests/material_fixture_mesh").hash());
    AppendPOD(binary, ShaderType::PixelStage);
    AppendPOD(binary, Name("project/tests/material_fixture_pixel").hash());
    AppendPOD(binary, u32(0u)); // Opaque flags.
    AppendPOD(binary, u32(0u)); // Shading model.
    AppendPOD(binary, u32(0u)); // Shadow transmittance model.
    for(u32 pass = 0u; pass < 3u; ++pass)
        AppendPOD(binary, u32(0u)); // No AVBOIT stage on an opaque material.
    return true;
}

class FixtureMaterialSource final : public Assets::IAssetBinarySource{
public:
    explicit FixtureMaterialSource(const Assets::AssetBytes& binary)
        : m_binary(binary)
    {}


public:
    virtual bool readAssetBinary(const Name& virtualPath, Assets::AssetBytes& outBinary)const override{
        if(virtualPath != s_MaterialName)
            return false;
        ++m_readCount;
        outBinary = m_binary;
        return true;
    }
    [[nodiscard]] u32 readCount()const noexcept{ return m_readCount; }

private:
    const Assets::AssetBytes& m_binary;
    mutable u32 m_readCount = 0u;
};

[[nodiscard]] u32 ReadSlot(const MaterialSurfaceInfo& info, const usize offset){
    u32 slot = 0u;
    NWB_MEMCPY(&slot, sizeof(slot), info.constantTypedBytes.data() + offset, sizeof(slot));
    return slot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialResourceLifecycleTest : public DescriptorBufferRoundTripTest{};

TEST_F(MaterialResourceLifecycleTest, InvalidationRetiresFixturesAndRepatchesCachedSurface){
    using namespace Impl;
    using namespace __hidden_material_fixture_lifecycle_tests;

    EcsTestWorld testWorld;
    auto& graphics = s_scope->graphics();
    auto& heap = device().getDescriptorHeap();
    Assets::AssetBytes binary(testWorld.arena);
    ASSERT_TRUE(MakeFixtureMaterialBinary(binary));
    FixtureMaterialSource source(binary);
    Assets::AssetRegistry registry(testWorld.arena);
    ASSERT_TRUE(registry.registerCodec(MakeUnique<MaterialAssetCodec>()));
    Assets::AssetManager assets(testWorld.arena, registry, source);
    RendererMeshState meshState(testWorld.arena);
    RendererMaterialState materialState(testWorld.arena);
    RendererCsgState csgState;
    CsgShapeRegistry shapes(testWorld.arena);
    RendererOpticalVolumeSelection opticalVolumes(testWorld.arena);
    RendererShaderPathResolveCallback shaderResolver;
    RendererShaderSystem shaders(graphics, assets, shaderResolver);
    RendererMeshSystem meshes(testWorld.arena, testWorld.world, graphics, assets, meshState);
    RendererCsgSystem csg(testWorld.arena, testWorld.world, graphics, shapes, csgState, shaders, meshes);
    RendererMaterialSystem materials(
        testWorld.arena, testWorld.world, graphics, assets, shapes, materialState, shaders, meshes, csg, opticalVolumes
    );
    ScopeExit release([&materials]()noexcept{ materials.invalidateResources(); });
    const auto baseline = heap.lifecycleStatistics();
    ASSERT_TRUE(baseline.initialized);
    Assets::AssetRef<Material> material;
    material.virtualPath = s_MaterialName;
    MaterialSurfaceInfo* firstInfo = nullptr;

    for(u32 generation = 0u; generation < 3u; ++generation){
        SCOPED_TRACE(generation);
        MaterialSurfaceInfo* info = nullptr;
        ASSERT_TRUE(materials.createMaterialSurfaceInfo(material, info));
        ASSERT_NE(info, nullptr);
        if(generation == 0u)
            firstInfo = info;
        EXPECT_EQ(info, firstInfo);
        EXPECT_EQ(source.readCount(), 1u) << "Reacquisition must use the retained CPU material cache.";
        EXPECT_TRUE(info->resourceReferencesResolved);
        EXPECT_TRUE(info->resourceFixturesResolved);
        ASSERT_EQ(info->constantTypedBytes.size(), 8u);
        EXPECT_NE(ReadSlot(*info, 0u), s_UnpatchedSlot);
        EXPECT_NE(ReadSlot(*info, 4u), s_UnpatchedSlot);
        const auto resolved = heap.lifecycleStatistics();
        EXPECT_EQ(resolved.resourceLiveSlotCount, baseline.resourceLiveSlotCount + 1u);
        EXPECT_EQ(resolved.samplerLiveSlotCount, baseline.samplerLiveSlotCount + 1u);

        MaterialSurfaceInfo* found = nullptr;
        ASSERT_TRUE(materials.findMaterialSurfaceInfo(material, found));
        EXPECT_EQ(found, info);
        ASSERT_TRUE(device().waitForIdle());
        materials.invalidateResources();
        EXPECT_FALSE(info->resourceReferencesResolved);
        EXPECT_FALSE(info->resourceFixturesResolved);
        EXPECT_EQ(ReadSlot(*info, 0u), s_UnpatchedSlot);
        EXPECT_EQ(ReadSlot(*info, 4u), s_UnpatchedSlot);
        EXPECT_FALSE(materials.findMaterialSurfaceInfo(material, found));
        EXPECT_EQ(found, nullptr);
        const auto retired = heap.lifecycleStatistics();
        EXPECT_EQ(retired.resourceLiveSlotCount, baseline.resourceLiveSlotCount);
        EXPECT_EQ(retired.samplerLiveSlotCount, baseline.samplerLiveSlotCount);
        materials.invalidateResources();
        const auto repeated = heap.lifecycleStatistics();
        EXPECT_EQ(repeated.resourceLiveSlotCount, baseline.resourceLiveSlotCount);
        EXPECT_EQ(repeated.samplerLiveSlotCount, baseline.samplerLiveSlotCount);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

