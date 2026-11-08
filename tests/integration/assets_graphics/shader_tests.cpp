// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_shader_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ShaderMetadataRejectsDefaultVariantAlias){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_default_variant_alias");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);


    const Path assetRoot = root / "assets";
    const Path includeMetaPath = assetRoot / "shaders" / "default_variant_include.nwb";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(includeMetaPath, "include asset;\n\n" "asset.default_variant = \"NWB_FEATURE=0\";\n" "asset.defines = {\n" "    \"NWB_FEATURE\": [\"0\", \"1\"],\n" "};\n"));
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "default_variant_include.slangi", ""));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(includeMetaPath, testArena, scratchArena, shaderCook));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'default_variant'")));

    EXPECT_TRUE(RemoveAllIfExists(root));
#endif
}


TEST(AssetsGraphics, ShaderMetadataRejectsRetiredGenericTypeAndStageField){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_retired_type_and_stage");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path metadataPath = root / "shader.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "[numthreads(1, 1, 1)] void main(){}\n"));
    Impl::ShaderCook shaderCook(testArena.arena);
    constexpr AStringView s_ValidMetadata = "compute_shader asset;\nasset.entry_point = \"main\";\n";
    constexpr Array<AStringView, 4u> s_RetiredMetadata = {
        "shader asset;\nasset.stage = \"cs\";\nasset.entry_point = \"main\";\n",
        "shader asset;\nasset.entry_point = \"main\";\n",
        "compute_shader asset;\nasset.stage = \"cs\";\nasset.entry_point = \"main\";\n",
        "compute_shader asset;\nasset.stage = \"ps\";\nasset.entry_point = \"main\";\n",
    };
    for(const AStringView metadata : s_RetiredMetadata){
        SCOPED_TRACE(metadata);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, s_ValidMetadata));
        ASSERT_TRUE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(metadataPath, testArena, scratchArena, shaderCook));
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, metadata));
        EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(metadataPath, testArena, scratchArena, shaderCook));
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, s_ValidMetadata));
        EXPECT_TRUE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(metadataPath, testArena, scratchArena, shaderCook));
    }
    EXPECT_TRUE(RemoveAllIfExists(root));
}


TEST(AssetsGraphics, ShaderMetadataRejectsObsoleteProfilesAndInvalidConditionalFlags){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_ray_query_metadata");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path metadataPath = root / "shader.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    struct MetadataCase{
        AStringView fields;
        b8 accepted;
    };
    constexpr Array<MetadataCase, 13u> s_Cases = {{
        { "", true },
        { "asset.ray_query = 0;\n", true },
        { "asset.ray_query = 1;\n", true },
        { "asset.ray_query = -1;\n", false },
        { "asset.ray_query = 2;\n", false },
        { "asset.ray_query = 1.0;\n", false },
        { "asset.ray_query = \"1\";\n", false },
        { "asset.ray_query = [];\n", false },
        { "asset.ray_query = {};\n", false },
        { "asset.target_profile = \"spirv_1_5\";\n", false },
        { "asset.target_profile = \"spirv_1_5+spvRayQueryKHR\";\n", false },
        { "asset.emit_mesh_compute_shadow = 0;\n", false },
        { "asset.emit_mesh_compute_shadow = 1;\n", false },
    }};
    Impl::ShaderCook shaderCook(testArena.arena);
    for(const MetadataCase& testCase : s_Cases){
        SCOPED_TRACE(testCase.fields);
        Impl::ShaderCook::CookString metadata("compute_shader asset;\nasset.entry_point = \"main\";\n", testArena.arena);
        metadata.append(testCase.fields);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, metadata));
        EXPECT_EQ(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(metadataPath, testArena, scratchArena, shaderCook).has_value(), testCase.accepted);
    }
    constexpr Array<AStringView, 3u> s_ShaderAssetTypes = { "vertex_shader", "pixel_shader", "ray_generation_shader" };
    for(const AStringView shaderAssetType : s_ShaderAssetTypes){
        const auto metadata = StringFormat(testArena.arena, "{} asset;\nasset.entry_point = \"main\";\nasset.emit_mesh_compute_shadow = 1;\n", shaderAssetType);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, metadata));
        EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(metadataPath, testArena, scratchArena, shaderCook));
    }
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, "mesh_shader asset;\nasset.entry_point = \"main\";\nasset.emit_mesh_compute_shadow = 0;\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(metadataPath, testArena, scratchArena, shaderCook));
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderMetadataRejectsEngineTransportDefinesAndRecoversWithRealVariants){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_transport_defines");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path shaderPath = root / "shader.nwb";
    const Path includePath = root / "include.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "include.slangi", "static const uint sourceValue = 1u;\n"));
    static constexpr AStringView s_ShaderDeclaration = "compute_shader asset;\nasset.entry_point = \"main\";\n";
    static constexpr AStringView s_RealDefines = "asset.defines = { \"PROJECT_QUALITY\": [\"0\", \"1\"] };\n";
    const auto validShaderMetadata = StringFormat(testArena.arena, "{}{}", s_ShaderDeclaration, s_RealDefines);
    const auto validIncludeMetadata = StringFormat(testArena.arena, "include asset;\n{}", s_RealDefines);
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::ShaderEntry shaderEntry(testArena.arena);
    Impl::ShaderCook::IncludeEntry includeEntry(testArena.arena);

    for(const AStringView transportName : { AStringView("NWB_BINDLESS_TLAS"), AStringView("NWB_CAUSTIC_PHOTON_TRANSPORT") }){
        SCOPED_TRACE(transportName);
        for(const AStringView retiredValue : { AStringView("0"), AStringView("1") }){
            SCOPED_TRACE(retiredValue);
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, validShaderMetadata));
            auto metadataResult5 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(shaderPath, testArena, scratchArena, shaderCook);
            ASSERT_TRUE(metadataResult5);
            if(metadataResult5)
                shaderEntry = Move(*metadataResult5);
            ASSERT_FALSE(shaderEntry.defineValues.empty());
            const auto obsoleteShaderMetadata = StringFormat(testArena.arena, "{}asset.defines = {{ \"{}\": [\"{}\"] }};\n", s_ShaderDeclaration, transportName, retiredValue);
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, obsoleteShaderMetadata));
            EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(shaderPath, testArena, scratchArena, shaderCook));
            EXPECT_FALSE(shaderEntry.defineValues.empty());
            const auto expectedDiagnostic = StringFormat(testArena.arena, NWB_TEXT("define '{}' is an engine transport feature"), ConvertString(transportName));
            EXPECT_TRUE(logger.sawErrorContaining(expectedDiagnostic));
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, validShaderMetadata));
            auto metadataResult4 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(shaderPath, testArena, scratchArena, shaderCook);
            ASSERT_TRUE(metadataResult4);
            if(metadataResult4)
                shaderEntry = Move(*metadataResult4);
            EXPECT_TRUE(Impl::ShaderCook::ValidateVariantSignature("project/shader", "PROJECT_QUALITY=0", shaderEntry.defineValues, scratchArena));
            EXPECT_TRUE(Impl::ShaderCook::ValidateVariantSignature("project/shader", "PROJECT_QUALITY=1", shaderEntry.defineValues, scratchArena));
            const auto invalidSignature = StringFormat(testArena.arena, "{}=1;PROJECT_QUALITY=1", transportName);
            EXPECT_FALSE(Impl::ShaderCook::ValidateVariantSignature("project/shader", invalidSignature, shaderEntry.defineValues, scratchArena));

            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, validIncludeMetadata));
            auto metadataResult3 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(includePath, testArena, scratchArena, shaderCook);
            ASSERT_TRUE(metadataResult3);
            if(metadataResult3)
                includeEntry = Move(*metadataResult3);
            ASSERT_FALSE(includeEntry.defineValues.empty());
            const auto obsoleteIncludeMetadata = StringFormat(testArena.arena, "include asset;\nasset.defines = {{ \"{}\": [\"{}\"] }};\n", transportName, retiredValue);
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, obsoleteIncludeMetadata));
            EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(includePath, testArena, scratchArena, shaderCook));
            EXPECT_FALSE(includeEntry.defineValues.empty());
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, validIncludeMetadata));
            auto metadataResult2 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(includePath, testArena, scratchArena, shaderCook);
            ASSERT_TRUE(metadataResult2);
            if(metadataResult2)
                includeEntry = Move(*metadataResult2);
            EXPECT_TRUE(Impl::ShaderCook::ValidateVariantSignature("project/include", "PROJECT_QUALITY=1", includeEntry.defineValues, scratchArena));
        }
    }
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderMetadataRejectsDefinesThatCannotRoundTripVariantSignatures){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_invalid_define_tokens");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "include.slangi", "static const uint sourceValue = 1u;\n"));
    static constexpr Array<AStringView, 6u> s_InvalidDefines = { "\"\": [\"1\"]", "\"9QUALITY\": [\"1\"]", "\"PROJECT-QUALITY\": [\"1\"]", "\"PROJECT_QUALITY\": [\"1;OTHER=0\"]", "\"PROJECT_QUALITY\": [\" 1\"]", "\"PROJECT_QUALITY\": [\"1 \"]", };
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::ShaderEntry shaderEntry(testArena.arena);
    Impl::ShaderCook::IncludeEntry includeEntry(testArena.arena);
    for(const b8 include : { false, true }){
        SCOPED_TRACE(include ? "include" : "shader");
        const Path metadataPath = root / (include ? "include.nwb" : "shader.nwb");
        const AStringView declaration = include ? "include asset;\n" : "compute_shader asset;\nasset.entry_point = \"main\";\n";
        const auto validMetadata = StringFormat(testArena.arena, "{}asset.defines = {{ \"PROJECT_QUALITY\": [\"1\"] }};\n", declaration);
        const auto parse = [&](){
            if(include){
                auto metadataResult1 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(metadataPath, testArena, scratchArena, shaderCook);
                if(metadataResult1)
                    includeEntry = Move(*metadataResult1);
                return metadataResult1.has_value();
            }
            auto metadataResult0 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(metadataPath, testArena, scratchArena, shaderCook);
            if(metadataResult0)
                shaderEntry = Move(*metadataResult0);
            return metadataResult0.has_value();
        };
        for(const AStringView invalidDefines : s_InvalidDefines){
            SCOPED_TRACE(invalidDefines);
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, validMetadata));
            ASSERT_TRUE(parse());
            ASSERT_FALSE(include ? includeEntry.defineValues.empty() : shaderEntry.defineValues.empty());
            const auto invalidMetadata = StringFormat(testArena.arena, "{}asset.defines = {{ {} }};\n", declaration, invalidDefines);
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, invalidMetadata));
            EXPECT_FALSE(parse());
            EXPECT_FALSE(include ? includeEntry.defineValues.empty() : shaderEntry.defineValues.empty());
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, validMetadata));
            ASSERT_TRUE(parse());
            const auto& defineValues = include ? includeEntry.defineValues : shaderEntry.defineValues;
            EXPECT_TRUE(Impl::ShaderCook::ValidateVariantSignature("project/define_probe", "PROJECT_QUALITY=1", defineValues, scratchArena));
        }
    }
    EXPECT_TRUE(RemoveAllIfExists(root));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

