// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_subsystem_boundary_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct RendererSubsystemBoundaryContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<RendererSubsystemBoundaryContractTestArenaTag>;


static bool ContainsText(const AStringView text, const AStringView expected){
    return text.find(expected) != AStringView::npos;
}


static usize CountText(const AStringView text, const AStringView expected){
    if(expected.empty())
        return 0u;

    usize count = 0u;
    usize offset = 0u;
    while(offset < text.size()){
        const usize found = text.find(expected, offset);
        if(found == AStringView::npos)
            break;
        ++count;
        offset = found + expected.size();
    }
    return count;
}


static AString CompactSource(const AStringView source){
    AString compact;
    compact.reserve(source.size());
    for(const char ch : source){
        if(ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' && ch != '\f' && ch != '\v')
            compact += ch;
    }
    return compact;
}


static bool IsOptionalParameterName(const AStringView text){
    if(text.empty())
        return true;

    const auto isLetterOrUnderscore = [](const char ch){
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
    };
    if(!isLetterOrUnderscore(text.front()))
        return false;
    for(const char ch : text){
        if(!isLetterOrUnderscore(ch) && !(ch >= '0' && ch <= '9'))
            return false;
    }
    return true;
}


static bool ConstructorParameterTypesMatch(
    const AStringView compactHeader,
    const AStringView constructorOpen,
    const InitializerList<StringView> expectedTypes
){
    if(CountText(compactHeader, constructorOpen) != 1u)
        return false;

    const usize parameterListBegin = compactHeader.find(constructorOpen) + constructorOpen.size();
    const usize parameterListEnd = compactHeader.find(");", parameterListBegin);
    if(parameterListEnd == AStringView::npos)
        return false;

    usize parameterBegin = parameterListBegin;
    usize expectedTypeIndex = 0u;
    for(const StringView expectedTypeStorage : expectedTypes){
        const bool lastParameter = expectedTypeIndex + 1u == expectedTypes.size();
        const usize comma = compactHeader.find(',', parameterBegin);
        if((lastParameter && comma < parameterListEnd) || (!lastParameter && (comma == AStringView::npos || comma >= parameterListEnd)))
            return false;

        const usize parameterEnd = lastParameter ? parameterListEnd : comma;
        const AStringView parameter = compactHeader.substr(parameterBegin, parameterEnd - parameterBegin);
        const AStringView expectedType(expectedTypeStorage.data(), expectedTypeStorage.size());
        if(parameter.size() < expectedType.size() || parameter.substr(0u, expectedType.size()) != expectedType)
            return false;
        if(!IsOptionalParameterName(parameter.substr(expectedType.size())))
            return false;

        parameterBegin = parameterEnd + 1u;
        ++expectedTypeIndex;
    }
    return parameterListBegin != parameterListEnd && expectedTypeIndex == expectedTypes.size();
}


static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, ShaderSystemOwnsOnlyItsNarrowConstructionBoundary){
    TestArena testArena;
    AString headerSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "ecs_render" / "shader" / "shader_system.h", headerSource));
    const AStringView header(headerSource.data(), headerSource.size());
    const AString compactHeaderStorage = CompactSource(header);
    const AStringView compactHeader(compactHeaderStorage.data(), compactHeaderStorage.size());

    EXPECT_FALSE(ContainsText(header, "RendererFramePipeline"));
    EXPECT_FALSE(ContainsText(header, "RendererFramePipelineSubsystemBase"));
    EXPECT_TRUE(ConstructorParameterTypesMatch(
        compactHeader,
        "RendererShaderSystem(",
        {
            "Core::Graphics&",
            "Core::Assets::AssetManager&",
            "RendererShaderPathResolveCallback&",
        }
    ));
}


TEST(EcsGraphics, MeshSystemOwnsOnlyItsNarrowConstructionBoundary){
    TestArena testArena;
    AString headerSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "ecs_render" / "mesh" / "mesh_system.h", headerSource));
    const AStringView header(headerSource.data(), headerSource.size());
    const AString compactHeaderStorage = CompactSource(header);
    const AStringView compactHeader(compactHeaderStorage.data(), compactHeaderStorage.size());

    EXPECT_FALSE(ContainsText(header, "RendererFramePipeline"));
    EXPECT_FALSE(ContainsText(header, "RendererFramePipelineSubsystemBase"));
    EXPECT_TRUE(ConstructorParameterTypesMatch(
        compactHeader,
        "RendererMeshSystem(",
        {
            "Core::Alloc::GlobalArena&",
            "Core::ECS::World&",
            "Core::Graphics&",
            "Core::Assets::AssetManager&",
            "RendererMeshState&",
            "RendererDrawState&",
        }
    ));
}


TEST(EcsGraphics, FramePipelineDoesNotPrivilegeNarrowShaderOrMeshSystems){
    TestArena testArena;
    AString headerSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "ecs_render" / "renderer_frame_pipeline.h", headerSource));
    const AString compactHeaderStorage = CompactSource(AStringView(headerSource.data(), headerSource.size()));
    const AStringView compactHeader(compactHeaderStorage.data(), compactHeaderStorage.size());

    EXPECT_FALSE(ContainsText(compactHeader, "friendclassRendererShaderSystem;"));
    EXPECT_FALSE(ContainsText(compactHeader, "friendclassRendererMeshSystem;"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

