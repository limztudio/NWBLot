// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_material/material_avboit_shader_generator.h>
#include <tests/common/capturing_logger.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_avboit_shader_generator_tests{


TEST(MaterialAvboitShaderGenerator, MissingInterfaceRejectsTransparentEntryWithoutChangingShaderNames){
    NWB::Tests::AssetsGraphicsFixture::TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(Name("tests/avboit_shader_generator/scratch"));
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard guard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
    auto root = NWB::Tests::AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "avboit_shader_missing_interface_v2");
    ASSERT_TRUE(root);

    NWB::Impl::MaterialCookVector<NWB::Impl::MaterialCookEntry> entries(testArena.arena);
    entries.emplace_back(testArena.arena);
    entries.back().virtualPath = "project/materials/opaque";
    entries.back().surfaceSource = "opaque.slang";
    entries.emplace_back(testArena.arena);
    entries.back().virtualPath = "project/materials/transparent";
    entries.back().surfaceSource = "transparent.slang";
    entries.back().transparent = true;

    using Generator = NWB::Impl::MaterialAvboitShaderGenerator;
    EXPECT_FALSE(Generator::emitAccumulate(*root, "test", testArena.arena, scratchArena, entries));
    EXPECT_FALSE(Generator::emitOccupancy(*root, "test", testArena.arena, scratchArena, entries));
    EXPECT_FALSE(Generator::emitExtinction(*root, "test", testArena.arena, scratchArena, entries));
    EXPECT_TRUE(entries[0u].avboitAccumulatePixelShaderName.empty());
    EXPECT_TRUE(entries[0u].avboitOccupancyPixelShaderName.empty());
    EXPECT_TRUE(entries[0u].avboitExtinctionPixelShaderName.empty());
    EXPECT_TRUE(entries[1u].avboitAccumulatePixelShaderName.empty());
    EXPECT_TRUE(entries[1u].avboitOccupancyPixelShaderName.empty());
    EXPECT_TRUE(entries[1u].avboitExtinctionPixelShaderName.empty());
    EXPECT_EQ(logger.errorCount(), 3u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("project/materials/transparent")));
    EXPECT_FALSE(logger.sawErrorContaining(NWB_TEXT("project/materials/opaque")));
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
