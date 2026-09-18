// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"

#include "../command_line.h"

#include <core/assets/paths.h>
#include <core/task/cpu/scheduler.h>
#include <core/common/log.h>
#include <global/cpu_topology.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_builder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_ImplSourceDirectoryName = "impl";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AddRoot(const NWB::Path& path, NWB::Pipeline::AssetBuilder::AssetBuildOptions& options){
    auto& arena = options.assetRoots.get_allocator().arena();
    auto text = PathToString(arena, path.lexically_normal());
    for(const auto& root : options.assetRoots){
        if(root.path == text)
            return true;
    }
    auto parentName = PathToString(arena, path.parent_path().filename());
    CanonicalizeTextInPlace(parentName);
    const ACompactString virtualRoot(parentName == s_ImplSourceDirectoryName ? NWB::Core::Assets::s_EngineVirtualRoot : NWB::Core::Assets::s_ProjectVirtualRoot);
    options.assetRoots.emplace_back(arena, AStringView(text), virtualRoot);
    return true;
}

static bool ResolveRoots(const PipelineOptions& parsed, NWB::Pipeline::AssetBuilder::AssetBuildOptions& options){
    auto& arena = options.assetRoots.get_allocator().arena();
    ErrorCode error;
    const Path repoRoot = AbsolutePath(Path(arena, options.repoRoot.empty() ? AStringView(".") : AStringView(options.repoRoot)), error);
    if(error){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to resolve repository root"));
        return false;
    }
    const auto& sources = parsed.assetRoots.empty() ? parsed.inputs : parsed.assetRoots;
    for(const auto& input : sources){
        Path path(arena, input);
        if(!path.is_absolute())
            path = repoRoot / path;
        path = path.lexically_normal();
        if(parsed.assetRoots.empty()){
            const bool directory = IsDirectory(path, error);
            if(error){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: failed to inspect input '{}'"), StringConvert(input));
                return false;
            }
            if(!directory)
                path = path.parent_path();
            Path ancestor = path;
            while(!ancestor.empty()){
                auto name = PathToString(arena, ancestor.filename());
                CanonicalizeTextInPlace(name);
                if(name == NWB::Core::Assets::s_AssetsDirectoryName){
                    path = ancestor;
                    break;
                }
                const Path parent = ancestor.parent_path();
                if(parent == ancestor)
                    break;
                ancestor = parent;
            }
        }
        if(!AddRoot(path, options))
            return false;
    }
    if(options.assetRoots.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: an empty input list requires --asset-root"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunPipelineTool(const int argc, char** argv){
    NWB::Core::Assets::AssetArena arena(Name("pipeline/asset_builder"));
    PipelineOptions parsed(arena);
    PipelineCommandLine commandLine(PipelineTool::AssetBuilder);
    return commandLine.run(argc, argv, parsed, [&](PipelineOptions& options){
        const u32 cores = QueryCpuCoreCount(CpuAffinity::Any);
        NWB::Core::CpuTaskScheduler cpuScheduler(cores > 1u ? cores - 1u : 0u);
        NWB::Pipeline::AssetBuilder::AssetBuildOptions buildOptions(arena, cpuScheduler);
        buildOptions.repoRoot = options.repoRoot;
        buildOptions.outputDirectory = options.outputDirectory;
        buildOptions.cacheDirectory = options.cacheDirectory;
        buildOptions.configuration = options.configuration;
        buildOptions.assetType = options.assetType;
        buildOptions.inputs = options.inputs;
        buildOptions.useExplicitInputs = true;
        if(!__hidden_asset_builder::ResolveRoots(options, buildOptions))
            return 1;
        const bool built = NWB::Pipeline::AssetBuilder::BuildAssets(buildOptions);
        cpuScheduler.wait();
        return built ? 0 : 1;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

