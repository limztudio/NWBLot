// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset_builder/global.h"

#include <core/assets/global.h>
#include <global/terminal_entry.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PipelineTool{
    enum Enum : u8{
        DependencyComputer,
        AssetBuilder,
        AssetGatherer,
    };
};

inline constexpr int s_PipelineExitSuccess = 0;
inline constexpr int s_PipelineExitFailure = 1;
inline constexpr int s_PipelineExitFatal = -1;
inline constexpr AStringView s_PipelineAppDescription = "NWB asset pipeline";
inline constexpr AStringView s_PipelineInputOption = "input,--input";
inline constexpr AStringView s_PipelineInputListOption = "--input-list";
inline constexpr AStringView s_PipelineOutputOption = "-o,--output,--output-directory";
inline constexpr AStringView s_PipelineRepoRootOption = "--repo-root";
inline constexpr AStringView s_PipelineAssetRootOption = "--asset-root";
inline constexpr AStringView s_PipelineCacheDirectoryOption = "--cache-directory";
inline constexpr AStringView s_PipelineAssetTypeOption = "--asset-type";
inline constexpr AStringView s_PipelineConfigurationOption = "--configuration";
inline constexpr AStringView s_PipelineSkinDependenciesOption = "--include-skin-dependencies";
inline constexpr AStringView s_PipelineCliHelpRequestName = "CallForHelp";

struct PipelineOptions{
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString> inputs;
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString> assetRoots;
    NWB::Core::Assets::AssetString repoRoot;
    NWB::Core::Assets::AssetString outputPath;
    NWB::Core::Assets::AssetString cacheDirectory;
    ACompactString configuration;
    ACompactString assetType{ NWB::Pipeline::AssetBuilder::s_GraphicsAssetBuildType };
    bool includeSkinDependencies = false;

    explicit PipelineOptions(NWB::Core::Assets::AssetArena& arena)
        : inputs(arena)
        , assetRoots(arena)
        , repoRoot(arena)
        , outputPath(arena)
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
        return ::InvokeTerminalEntry<CLI::ParseError>([&](){
            if(!parse(argc, argv, options))
                return s_PipelineExitFailure;
            return body(options);
        }, [&](const CLI::ParseError& error){ return exit(error); }, [](){ return s_PipelineExitFatal; });
    }


private:
    InteropVector<AInteropString> m_inputs;
    InteropVector<AInteropString> m_assetRoots;
    AInteropString m_inputList;
    AInteropString m_repoRoot;
    AInteropString m_outputPath;
    AInteropString m_cacheDirectory;
    AInteropString m_configuration;
    AInteropString m_assetType{ NWB::Pipeline::AssetBuilder::s_GraphicsAssetBuildType };
    bool m_includeSkinDependencies = false;
    PipelineTool::Enum m_tool;
    CLI::App m_app;
};

int RunPipelineTool(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

