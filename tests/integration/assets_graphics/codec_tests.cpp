// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <tests/integration/assets_graphics/case_types.adhd.h>

#include <impl/assets_graphics/components.adhd.h>
#include <impl/assets_graphics/cook_types.adhd.h>

#include <core/assets/input_list.h>
#include <core/assets/cook_metadata_internal.h>
#include <core/assets/volume/built_asset.h>

#include <global/filesystem/executable_paths.h>
#include <global/sha1.h>
#include <global/atomic.h>
#include <global/sync.h>
#include <global/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_AssetsDirectory = "assets";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, RendererOpticalVolumeGroupRejectsOnlyAsciiEdgePadding){
    EXPECT_FALSE(NWB::Impl::IsUnpaddedRendererOpticalVolumeGroup(""));
    EXPECT_FALSE(NWB::Impl::IsUnpaddedRendererOpticalVolumeGroup(" \t "));
    EXPECT_FALSE(NWB::Impl::IsUnpaddedRendererOpticalVolumeGroup(" group"));
    EXPECT_FALSE(NWB::Impl::IsUnpaddedRendererOpticalVolumeGroup("group\n"));
    EXPECT_TRUE(NWB::Impl::IsUnpaddedRendererOpticalVolumeGroup("group\tname"));
    EXPECT_TRUE(NWB::Impl::IsUnpaddedRendererOpticalVolumeGroup("\xc2\xa0group"));
}

TEST(AssetsGraphics, RendererComponentCookAndSerializePreservesOpticalFields){
    static constexpr AStringView s_RendererComponentMetadata =
        "renderer_component asset;\n"
        "asset.material = \"project/materials/glass\";\n"
        "asset.visible = 1;\n"
        "asset.optical_volume_coincidence = \"shared_group\";\n"
        "asset.optical_volume_group = \"lens_assembly\";\n"
        "asset.optical_volume_priority = 4;\n"
        "asset.optical_boundary_mode = \"closed_priority\";\n"
        "asset.optical_medium_priority = 9;\n";

    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "renderer_component_optical_fields");
    ASSERT_TRUE(cookCase);
    const Path& root = cookCase->root;
    const Path& outputDirectory = cookCase->outputDirectory;
    const Path assetRoot = root / s_AssetsDirectory;
    const Name virtualPath = NWB::Tests::AssetsGraphicsFixture::s_ProjectEntitiesLensRenderer;

    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "entities" / "lens" / "renderer.nwb", s_RendererComponentMetadata));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, 1u));

    const auto loadedAsset = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::ComponentAssetCodec>(outputDirectory, virtualPath, 1u, testArena);
    ASSERT_TRUE(loadedAsset);
    ASSERT_NE(loadedAsset->get(), nullptr);
    EXPECT_EQ((*loadedAsset)->assetType(), NWB::Impl::RendererComponentAsset::AssetTypeName());
    const auto* rendererAsset = static_cast<const NWB::Impl::RendererComponentAsset*>(loadedAsset->get());
    EXPECT_EQ(rendererAsset->component.material.name(), Name("project/materials/glass"));
    EXPECT_TRUE(rendererAsset->component.visible);
    EXPECT_EQ(rendererAsset->component.opticalVolumeCoincidence, NWB::Impl::OpticalVolumeCoincidence::SharedGroup);
    EXPECT_EQ(rendererAsset->component.opticalVolumeGroup, Name("lens_assembly"));
    EXPECT_EQ(rendererAsset->component.opticalVolumePriority, 4);
    EXPECT_EQ(rendererAsset->component.opticalBoundaryMode, NWB::Impl::OpticalBoundaryMode::ClosedPriority);
    EXPECT_EQ(rendererAsset->component.opticalMediumPriority, 9);
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_TRUE(RemoveAllIfExists(root));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

