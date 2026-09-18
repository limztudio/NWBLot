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

public:
    // Shared RunPipelineTool wrapper: parse options, then invoke the tool body inside the terminal entry.
    template<typename ToolBody>
    [[nodiscard]] int run(const int argc, char** argv, PipelineOptions& options, ToolBody&& body){
        return NWB::Core::Common::InvokeTerminalEntry<CLI::ParseError>([&](){
            if(!parse(argc, argv, options))
                return 1;
            return body(options);
        }, [&](const CLI::ParseError& error){ return exit(error); }, [](){ return -1; });
    }


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

