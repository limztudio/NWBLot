// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "build.h"

#include "../command_line.h"

#include <core/assets/paths.h>
#include <core/alloc/thread.h>
#include <core/common/log.h>
#include <global/cpu_topology.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_builder{


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
    const ACompactString virtualRoot(parentName == "impl" ? "engine" : "project");
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
    const auto result = ParseCommandLine(argc, argv, PipelineTool::AssetBuilder, parsed);
    if(result != CommandLineParseResult::Success)
        return result == CommandLineParseResult::Help ? 0 : 1;

    const u32 cores = QueryCpuCoreCount(CpuAffinity::Any);
    NWB::Core::Alloc::ThreadPool threadPool(cores > 1u ? cores - 1u : 0u, CpuAffinity::Any);
    NWB::Pipeline::AssetBuilder::AssetBuildOptions options(arena, threadPool);
    options.repoRoot = parsed.repoRoot;
    options.outputDirectory = parsed.outputDirectory;
    options.cacheDirectory = parsed.cacheDirectory;
    options.configuration = parsed.configuration;
    options.assetType = parsed.assetType;
    options.inputs = parsed.inputs;
    options.useExplicitInputs = true;
    if(!__hidden_asset_builder::ResolveRoots(parsed, options))
        return 1;
    const bool built = NWB::Pipeline::AssetBuilder::BuildAssets(options);
    threadPool.finish();
    return built ? 0 : 1;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

