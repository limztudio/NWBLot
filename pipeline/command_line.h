// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/global.h>
#include <core/common/terminal_entry.h>

#include <CLI.hpp>


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

// The parsed option values and their CLI bindings stay alive through the terminal entry's help/error handler.
class PipelineCommandLine final : NoCopy{
public:
    explicit PipelineCommandLine(PipelineTool::Enum inTool);
    PipelineCommandLine(PipelineCommandLine&&) = delete;


public:
    [[nodiscard]] bool parse(int argc, char** argv, PipelineOptions& options);
    [[nodiscard]] int exit(const CLI::ParseError& error)const;


private:
    InteropVector<AInteropString> m_inputs;
    InteropVector<AInteropString> m_assetRoots;
    AInteropString m_inputList;
    AInteropString m_repoRoot;
    AInteropString m_outputDirectory;
    AInteropString m_cacheDirectory;
    AInteropString m_configuration;
    AInteropString m_assetType;
    PipelineTool::Enum m_tool;
    CLI::App m_app;
};

int RunPipelineTool(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

