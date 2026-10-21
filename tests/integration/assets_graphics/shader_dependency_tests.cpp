// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_shader/binary_payload.h>
#include <impl/assets_shader/source_dependencies.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_shader_dependency{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_MATERIAL_BIND_INCLUDES = "material_bind_includes";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ShaderDependenciesTrackContinuedIncludesAndRejectUnplannedIncludes){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_dependency_directives");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path sourcePath = root / "source.slang";
    const Path trackedInclude = root / "tracked.slangi";
    static constexpr AStringView s_Source = "/*\n#include \"missing_block.slangi\"\n*/\n" "// #include \"missing_line.slangi\"\n" "#include \\\r\n\"tracked.slangi\"\n";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(sourcePath, s_Source));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(trackedInclude, "static const uint trackedValue = 1u;\n"));
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    ASSERT_TRUE(Impl::ShaderSourceDependencies::Gather(sourcePath, includeDirectories, {}, dependencies, scratchArena));
    ASSERT_EQ(dependencies.size(), 2u);
    ASSERT_NE(FindIf(dependencies.begin(), dependencies.end(), [&trackedInclude](const Path& path)noexcept{
        return path == trackedInclude;
    }
    ), dependencies.end()
    );
    u64 beforeChecksum = 0u;
    u64 afterChecksum = 0u;
    const auto dependencyResult4 = shaderCook.computeDependencyChecksum(dependencies, includeDirectories, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(dependencyResult4);
    beforeChecksum = dependencyResult4->checksum;
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(trackedInclude, "static const uint trackedValue = 2u;\n"));
    const auto dependencyResult3 = shaderCook.computeDependencyChecksum(dependencies, includeDirectories, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(dependencyResult3);
    afterChecksum = dependencyResult3->checksum;
    EXPECT_NE(beforeChecksum, afterChecksum);
    EXPECT_EQ(logger.errorCount(), 0u);

    static constexpr AStringView s_UnplannedIncludes[] = { "#define GENERATED_INCLUDE \"tracked.slangi\"\n#include GENERATED_INCLUDE\n", "#include_next \"tracked.slangi\"\n", "#import \"tracked.slangi\"\n", };
    for(const AStringView source : s_UnplannedIncludes){
        SCOPED_TRACE(source);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(sourcePath, source));
        EXPECT_FALSE(Impl::ShaderSourceDependencies::Gather(sourcePath, includeDirectories, {}, dependencies, scratchArena));
    }
    const usize failureErrors = logger.errorCount();
    EXPECT_GT(failureErrors, 0u);
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(sourcePath, s_Source));
    EXPECT_TRUE(Impl::ShaderSourceDependencies::Gather(sourcePath, includeDirectories, {}, dependencies, scratchArena));
    EXPECT_EQ(logger.errorCount(), failureErrors);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderDependencyChecksumAliasesGeneratedRoot){
    TestArena testArena;
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_dependency_checksum_alias");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);


    const Path relativeIncludePath = Path(testArena.arena, "project") / "material_interfaces" / "test_surface.bind";
    const Path firstGeneratedRoot = root / "first" / s_MATERIAL_BIND_INCLUDES;
    const Path secondGeneratedRoot = root / "second" / s_MATERIAL_BIND_INCLUDES;
    const Path firstGeneratedInclude = firstGeneratedRoot / relativeIncludePath;
    const Path secondGeneratedInclude = secondGeneratedRoot / relativeIncludePath;
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(firstGeneratedInclude, "generated include\n"));
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(secondGeneratedInclude, "generated include\n"));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);

    NWB::Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> firstDependencies(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> secondDependencies(testArena.arena);
    firstDependencies.push_back(firstGeneratedInclude);
    secondDependencies.push_back(secondGeneratedInclude);

    u64 firstChecksum = 0u;
    u64 secondChecksum = 0u;
    const auto dependencyResult2 = shaderCook.computeDependencyChecksum(firstDependencies, includeDirectories, { { firstGeneratedRoot, s_MATERIAL_BIND_INCLUDES } }, scratchArena);
    ASSERT_TRUE(dependencyResult2);
    firstChecksum = dependencyResult2->checksum;
    const auto dependencyResult1 = shaderCook.computeDependencyChecksum(secondDependencies, includeDirectories, { { secondGeneratedRoot, s_MATERIAL_BIND_INCLUDES } }, scratchArena);
    ASSERT_TRUE(dependencyResult1);
    secondChecksum = dependencyResult1->checksum;
    EXPECT_EQ(firstChecksum, secondChecksum);

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(secondGeneratedInclude, "changed generated include\n"));
    u64 changedChecksum = 0u;
    const auto dependencyResult0 = shaderCook.computeDependencyChecksum(secondDependencies, includeDirectories, { { secondGeneratedRoot, s_MATERIAL_BIND_INCLUDES } }, scratchArena);
    ASSERT_TRUE(dependencyResult0);
    changedChecksum = dependencyResult0->checksum;
    EXPECT_NE(firstChecksum, changedChecksum);

#if defined(NWB_FINAL)
    const Path unaliasedDependency = root / "outside" / "unaliased.slangi";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(unaliasedDependency, "outside alias root\n"));
    NWB::Impl::ShaderCook::CookVector<Path> unaliasedDependencies(testArena.arena);
    unaliasedDependencies.push_back(unaliasedDependency);

    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);
    EXPECT_FALSE(shaderCook.computeDependencyChecksum(unaliasedDependencies, includeDirectories, { { firstGeneratedRoot, s_MATERIAL_BIND_INCLUDES } }, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("outside the declared dependency root aliases")));
#endif

    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderDependencyChecksumIncludesOrderedSearchRootsWithSameFileUnion){
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_dependency_search_order");
    ASSERT_TRUE(rootResult);
    const Path root = Move(*rootResult);
    const Path firstRoot = root / "first";
    const Path secondRoot = root / "second";
    const Path sourcePath = root / "source.slang";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(sourcePath, "#include <choice.slangi>\nstatic const uint selectedValue = NWB_TEST_SELECTED;\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(firstRoot / "choice.slangi", "#ifndef NWB_TEST_FIRST\n#define NWB_TEST_FIRST\n#ifndef NWB_TEST_SELECTED\n#define NWB_TEST_SELECTED 1\n#endif\n#include \"../second/choice.slangi\"\n#endif\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(secondRoot / "choice.slangi", "#ifndef NWB_TEST_SECOND\n#define NWB_TEST_SECOND\n#ifndef NWB_TEST_SELECTED\n#define NWB_TEST_SELECTED 2\n#endif\n#include \"../first/choice.slangi\"\n#endif\n"));

    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::CookVector<Path> firstDirectories(testArena.arena);
    Impl::ShaderCook::CookVector<Path> secondDirectories(testArena.arena);
    Impl::ShaderCook::CookVector<Path> noDirectories(testArena.arena);
    firstDirectories.push_back(firstRoot);
    firstDirectories.push_back(secondRoot);
    secondDirectories.push_back(secondRoot);
    secondDirectories.push_back(firstRoot);
    Impl::ShaderCook::CookVector<Path> firstDependencies(testArena.arena);
    Impl::ShaderCook::CookVector<Path> secondDependencies(testArena.arena);
    ASSERT_TRUE(Impl::ShaderSourceDependencies::Gather(sourcePath, firstDirectories, {}, firstDependencies, scratchArena));
    ASSERT_TRUE(Impl::ShaderSourceDependencies::Gather(sourcePath, secondDirectories, {}, secondDependencies, scratchArena));
    ASSERT_EQ(firstDependencies.size(), 3u);
    ASSERT_EQ(secondDependencies.size(), firstDependencies.size());
    for(const Path& dependency : firstDependencies)
        EXPECT_NE(FindIf(secondDependencies.begin(), secondDependencies.end(), [&dependency](const Path& path)noexcept{ return path == dependency; }), secondDependencies.end());

    const auto firstFileUnion = shaderCook.computeDependencyChecksum(firstDependencies, noDirectories, { { root, "source" } }, scratchArena);
    const auto secondFileUnion = shaderCook.computeDependencyChecksum(secondDependencies, noDirectories, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(firstFileUnion);
    ASSERT_TRUE(secondFileUnion);
    EXPECT_EQ(firstFileUnion->checksum, secondFileUnion->checksum);
    const auto firstSearch = shaderCook.computeDependencyChecksum(firstDependencies, firstDirectories, { { root, "source" } }, scratchArena);
    const auto secondSearch = shaderCook.computeDependencyChecksum(secondDependencies, secondDirectories, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(firstSearch);
    ASSERT_TRUE(secondSearch);
    EXPECT_NE(firstSearch->checksum, secondSearch->checksum);
    firstDirectories.push_back(root.parentPath());
    const auto externalSearch = shaderCook.computeDependencyChecksum(firstDependencies, firstDirectories, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(externalSearch);
    EXPECT_NE(externalSearch->checksum, firstSearch->checksum);
    // Unused case-only search roots keep their exact spelling even on case-insensitive hosts.
    firstDirectories.clear();
    firstDirectories.push_back(root / "CaseOnly");
    const auto upperCaseSearch = shaderCook.computeDependencyChecksum(firstDependencies, firstDirectories, { { root, "source" } }, scratchArena);
    firstDirectories.clear();
    firstDirectories.push_back(root / "caseonly");
    const auto lowerCaseSearch = shaderCook.computeDependencyChecksum(firstDependencies, firstDirectories, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(upperCaseSearch);
    ASSERT_TRUE(lowerCaseSearch);
    EXPECT_NE(upperCaseSearch->checksum, lowerCaseSearch->checksum);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderDependencyChecksumPreservesPhysicalPathCaseAcrossAliasedRoots){
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_dependency_case_identity");
    ASSERT_TRUE(rootResult);
    const Path root = Move(*rootResult);
    const Path firstRoot = root / "first";
    const Path secondRoot = root / "second";
    const Path upperPath = firstRoot / "Case.slangi";
    const Path lowerPath = secondRoot / "case.slangi";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(upperPath, "same dependency bytes\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(lowerPath, "same dependency bytes\n"));
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::CookVector<Path> noDirectories(testArena.arena);
    Impl::ShaderCook::CookVector<Path> upperDependencies(testArena.arena);
    Impl::ShaderCook::CookVector<Path> lowerDependencies(testArena.arena);
    upperDependencies.push_back(upperPath);
    lowerDependencies.push_back(lowerPath);
    const auto upperKey = shaderCook.computeDependencyChecksum(upperDependencies, noDirectories, { { firstRoot, "source" } }, scratchArena);
    const auto lowerKey = shaderCook.computeDependencyChecksum(lowerDependencies, noDirectories, { { secondRoot, "source" } }, scratchArena);
    ASSERT_TRUE(upperKey);
    ASSERT_TRUE(lowerKey);
    EXPECT_NE(upperKey->checksum, lowerKey->checksum);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderDependencyCollectionRetainsCaseDistinctPhysicalInputs){
#if defined(NWB_PLATFORM_WINDOWS)
    GTEST_SKIP() << "Windows shader path duplicate policy is case insensitive";
#endif
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_dependency_case_collection");
    ASSERT_TRUE(rootResult);
    const Path root = Move(*rootResult);
    const Path sourcePath = root / "source.slang";
    const Path upperPath = root / "Case.slangi";
    const Path lowerPath = root / "case.slangi";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(upperPath, "static const uint upperValue = 1;\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(lowerPath, "static const uint lowerValue = 2;\n"));
    AString<Core::Alloc::ScratchArena> upperText(scratchArena);
    ASSERT_TRUE(ReadTextFile(upperPath, upperText));
    if(upperText != "static const uint upperValue = 1;\n"){
        EXPECT_TRUE(RemoveAllIfExists(root));
        GTEST_SKIP() << "Filesystem provider cannot store case-distinct shader inputs";
    }
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(sourcePath, "#include \"Case.slangi\"\n#include \"case.slangi\"\n"));
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::CookVector<Path> noDirectories(testArena.arena);
    Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    ASSERT_TRUE(Impl::ShaderSourceDependencies::Gather(sourcePath, noDirectories, {}, dependencies, scratchArena));
    ASSERT_EQ(dependencies.size(), 3u);
    EXPECT_NE(FindIf(dependencies.begin(), dependencies.end(), [&upperPath](const Path& path)noexcept{ return path == upperPath; }), dependencies.end());
    EXPECT_NE(FindIf(dependencies.begin(), dependencies.end(), [&lowerPath](const Path& path)noexcept{ return path == lowerPath; }), dependencies.end());
    const auto firstKey = shaderCook.computeDependencyChecksum(dependencies, noDirectories, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(firstKey);
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(lowerPath, "static const uint lowerValue = 3;\n"));
    const auto changedKey = shaderCook.computeDependencyChecksum(dependencies, noDirectories, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(changedKey);
    EXPECT_NE(firstKey->checksum, changedKey->checksum);
    EXPECT_TRUE(RemoveAllIfExists(root));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

