// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PipelineTool{
    enum Enum : u8{
        DependencyComputer,
        AssetBuilder,
        AssetGatherer,
    };
};

struct PipelineOptions{
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString> inputs;
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString> assetRoots;
    NWB::Core::Assets::AssetString repoRoot;
    NWB::Core::Assets::AssetString outputDirectory;
    NWB::Core::Assets::AssetString cacheDirectory;
    ACompactString configuration;
    ACompactString assetType;

    explicit PipelineOptions(NWB::Core::Assets::AssetArena& arena)
        : inputs(arena)
        , assetRoots(arena)
        , repoRoot(arena)
        , outputDirectory(arena)
        , cacheDirectory(arena)
    {}
};

namespace CommandLineParseResult{
    enum Enum : u8{
        Success,
        Help,
        Error,
    };
};

CommandLineParseResult::Enum ParseCommandLine(int argc, char** argv, PipelineTool::Enum tool, PipelineOptions& options);
int RunPipelineTool(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

