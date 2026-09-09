// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;

TEST(EcsGraphics, ReflectionSurfaceContractUsesExplicitFieldsAndNeutralDefaults){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "mesh" / "surface.slangi", source));
    const AStringView shader(source.data(), source.size());
    const usize structure = shader.find("struct NwbMeshSurface{");
    const usize constructor = shader.find("NwbMeshSurface nwbMakeMeshSurface(half3 baseColor, float3 normal, half param0, half param1)");
    ASSERT_NE(structure, AStringView::npos);
    ASSERT_NE(constructor, AStringView::npos);
    ASSERT_LT(structure, constructor);
    const AStringView fields = shader.substr(structure, constructor - structure);
    EXPECT_TRUE(ContainsText(fields, "half3  specularF0;"));
    EXPECT_TRUE(ContainsText(fields, "half   perceptualRoughness;"));

    const usize constructorEnd = shader.find("return surface;", constructor);
    ASSERT_NE(constructorEnd, AStringView::npos);
    const AStringView constructorBody = shader.substr(constructor, constructorEnd - constructor);
    EXPECT_TRUE(ContainsText(constructorBody, "surface.specularF0 = NWB_SURFACE_SPECULAR_F0_NONE;"));
    EXPECT_TRUE(ContainsText(constructorBody, "surface.perceptualRoughness = NWB_SURFACE_ROUGHNESS_MAX;"));
    EXPECT_TRUE(ContainsText(shader, "#define NWB_SURFACE_SPECULAR_F0_NONE half3(0.0h, 0.0h, 0.0h)"));
    EXPECT_TRUE(ContainsText(shader, "#define NWB_SURFACE_ROUGHNESS_MAX    half(1.0)"));
    EXPECT_TRUE(ContainsText(constructorBody, "surface.param0 = param0;"));
    EXPECT_TRUE(ContainsText(constructorBody, "surface.param1 = param1;"));
}

TEST(EcsGraphics, ReflectionAuthoringSanitizesOnlyTheDedicatedReflectionFields){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "mesh" / "surface.slangi", source));
    const AStringView shader(source.data(), source.size());
    const usize begin = shader.find("void nwbSetMeshSurfaceReflection(");
    ASSERT_NE(begin, AStringView::npos);
    const usize end = shader.find("\n}", begin);
    ASSERT_NE(end, AStringView::npos);
    const AStringView authoring = shader.substr(begin, end - begin);
    EXPECT_TRUE(ContainsText(authoring, "all(isfinite(float3(specularF0))) ? saturate(specularF0) : NWB_SURFACE_SPECULAR_F0_NONE"));
    EXPECT_TRUE(ContainsText(authoring, "isfinite(float(perceptualRoughness))"));
    EXPECT_TRUE(ContainsText(authoring, "? saturate(perceptualRoughness) : NWB_SURFACE_ROUGHNESS_MAX"));
    EXPECT_FALSE(ContainsText(authoring, "param0"));
    EXPECT_FALSE(ContainsText(authoring, "param1"));
    EXPECT_FALSE(ContainsText(authoring, "refractionIor"));
    EXPECT_FALSE(ContainsText(authoring, "shadowAbsorptionTint"));
    EXPECT_FALSE(ContainsText(authoring, "renderCoverage"));
}

TEST(EcsGraphics, GlassReflectionUsesFiniteDielectricF0WithoutCoverageScaling){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "mesh" / "surface.slangi", source));
    const AStringView shader(source.data(), source.size());
    const usize begin = shader.find("NwbMeshSurface nwbMakeGlassSurface(");
    ASSERT_NE(begin, AStringView::npos);
    const usize end = shader.find("return surface;", begin);
    ASSERT_NE(end, AStringView::npos);
    const AStringView glass = shader.substr(begin, end - begin);
    EXPECT_TRUE(ContainsText(glass, "isfinite(float(refractionIor)) ? max(float(refractionIor), 1.0) : 1.0"));
    EXPECT_TRUE(ContainsText(glass, "(validIor - 1.0) / (validIor + 1.0)"));
    EXPECT_TRUE(ContainsText(glass, "surface.refractionIor = half(validIor);"));
    EXPECT_TRUE(ContainsText(glass, "nwbSetMeshSurfaceReflection(surface, half3(dielectricRatio * dielectricRatio), half(0.0));"));
    const usize reflectionWrite = glass.find("nwbSetMeshSurfaceReflection(");
    const usize coverageWrite = glass.find("surface.renderCoverage =");
    ASSERT_NE(reflectionWrite, AStringView::npos);
    ASSERT_NE(coverageWrite, AStringView::npos);
    EXPECT_LT(reflectionWrite, coverageWrite);
}

TEST(EcsGraphics, SmokeReflectionInputsHaveIndependentTypedDefaults){
    TestArena testArena;
    const TestPath shaders = RepoRoot(testArena) / "tests" / "smoke" / "assets" / "shaders";
    AString bindingSource;
    AString hookSource;
    ASSERT_TRUE(ReadTextFile(shaders / "smoke_surface.bind", bindingSource));
    ASSERT_TRUE(ReadTextFile(shaders / "smoke_surface.surface", hookSource));
    const AStringView bindings(bindingSource.data(), bindingSource.size());
    const AStringView hook(hookSource.data(), hookSource.size());
    EXPECT_TRUE(ContainsText(bindings, "[default(\"half3(0.0h, 0.0h, 0.0h)\")]\n    half3 specular_f0;"));
    EXPECT_TRUE(ContainsText(bindings, "[default(\"half(1.0h)\")]\n    half perceptual_roughness;"));
    EXPECT_TRUE(ContainsText(hook, "nwbSetMeshSurfaceReflection(result, runtime.specular_f0, runtime.perceptual_roughness);"));
    EXPECT_FALSE(ContainsText(hook, "result.param0"));
    EXPECT_FALSE(ContainsText(hook, "result.param1"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

