// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EcsGraphicsTaskGraphContractTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct TaskGraphContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<TaskGraphContractTestArenaTag>;


inline bool ContainsText(const AStringView text, const AStringView expected){
    AString normalized;
    normalized.reserve(text.size());
    for(const char ch : text){
        if(ch != '\r')
            normalized += ch;
    }
    return AStringView(normalized.data(), normalized.size()).find(expected) != AStringView::npos;
}

inline usize CountText(const AStringView text, const AStringView expected){
    if(expected.empty())
        return 0u;
    AString normalized;
    normalized.reserve(text.size());
    for(const char ch : text){
        if(ch != '\r')
            normalized += ch;
    }
    const AStringView normalizedText(normalized.data(), normalized.size());
    usize count = 0u;
    usize offset = 0u;
    while(offset < normalizedText.size()){
        const usize found = normalizedText.find(expected, offset);
        if(found == AStringView::npos)
            break;
        ++count;
        offset = found + expected.size();
    }
    return count;
}

inline TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}

inline void ExpectRendererBaselineEnvOwnedBySmokeHelper(const TestPath& repoRoot){
    AString helperSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "smoke_project_helpers.h", helperSource));
    const AStringView helpers(helperSource.data(), helperSource.size());
    EXPECT_TRUE(ContainsText(helpers, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(helpers, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
}


// Source contracts name only their implementation owners. Do not sweep every task-graph source here: an unrelated
// file split must not change a contract that has no dependency on it.
inline bool ReadRendererSources(
    const TestPath& repoRoot,
    const InitializerList<StringView> sourcePaths,
    AString& outSource
){
    const TestPath rendererDirectory = repoRoot / "impl" / "ecs_render";
    outSource.clear();
    for(const StringView sourcePath : sourcePaths){
        AString source;
        if(!ReadTextFile(rendererDirectory / sourcePath.data(), source))
            return false;
        if(!outSource.empty())
            outSource += "\n\n";
        outSource.append(source.data(), source.size());
    }
    return true;
}


inline bool ReadGraphicsModuleSources(const TestPath& repoRoot, AString& outSource){
    static constexpr StringView s_SourceNames[] = {
        "module.cpp",
        "module_graph_setup.cpp",
        "module_texture_upload.cpp",
        "module_setup.cpp",
    };

    const TestPath graphicsDirectory = repoRoot / "core" / "graphics";
    outSource.clear();
    for(const StringView sourceName : s_SourceNames){
        AString source;
        if(!ReadTextFile(graphicsDirectory / sourceName.data(), source))
            return false;
        if(!outSource.empty())
            outSource += "\n\n";
        outSource.append(source.data(), source.size());
    }
    return true;
}


inline bool ReadRendererFramePipelineRuntimeSources(const TestPath& repoRoot, AString& outSource){
    static constexpr StringView s_SourceNames[] = {
        "renderer_frame_pipeline.cpp",
        "renderer_frame_pipeline_resources.cpp",
        "renderer_frame_pipeline_execute.cpp",
    };

    const TestPath pipelineDirectory = repoRoot / "impl" / "ecs_render";
    outSource.clear();
    for(const StringView sourceName : s_SourceNames){
        AString source;
        if(!ReadTextFile(pipelineDirectory / sourceName.data(), source))
            return false;
        if(!outSource.empty())
            outSource += "\n\n";
        outSource.append(source.data(), source.size());
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

