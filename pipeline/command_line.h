// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "root_discovery.h"

#include <core/alloc/scratch.h>
#include <core/assets/global.h>
#include <core/common/application_entry.h>

#include <global/optional.h>
#include <global/terminal_entry.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PipelineTool{
    enum Enum : u8{
        DependencyComputer,
        AssetBuilder,
        AssetGatherer,
        AdhdCompiler,
    };
};

inline constexpr int s_PipelineExitSuccess = 0;
inline constexpr int s_PipelineExitFailure = 1;
inline constexpr int s_PipelineExitFatal = -1;
inline constexpr AStringView s_PipelineAppDescription = "NWB asset pipeline";
inline constexpr AStringView s_PipelineInputOption = "input,--input";
inline constexpr AStringView s_PipelineInputListOption = "--input-list";
inline constexpr AStringView s_PipelineOutputOption = "-o,--output,--output-dir,--output-directory";
inline constexpr AStringView s_PipelineRepoRootOption = "--repo-root";
inline constexpr AStringView s_PipelineAssetRootOption = "--asset-root";
inline constexpr AStringView s_PipelineAssetTypeOption = "--asset-type";
inline constexpr AStringView s_PipelineConfigurationOption = "--configuration";
inline constexpr AStringView s_PipelineIncludeDirOption = "-I,--include-dir";
inline constexpr AStringView s_PipelineDepfileOption = "--depfile";
inline constexpr AStringView s_PipelineNamespaceOption = "--namespace";
inline constexpr AStringView s_PipelineCliHelpRequestName = "CallForHelp";

struct PipelineOptions{
    Core::Assets::AssetVector<Core::Assets::AssetString> inputs;
    Core::Assets::AssetVector<Core::Assets::AssetString> assetRoots;
    Core::Assets::AssetVector<Core::Assets::AssetString> includeDirectories;
    Core::Assets::AssetString repoRoot;
    Core::Assets::AssetString outputPath;
    Core::Assets::AssetString depfilePath;
    Core::Assets::AssetString customNamespace;
    ACompactString configuration;
    // Unset when --asset-type is omitted; the asset builder owns the default domain.
    Optional<ACompactString> assetType;


public:
    explicit PipelineOptions(Core::Assets::AssetArena& arena)noexcept
        : inputs(arena)
        , assetRoots(arena)
        , includeDirectories(arena)
        , repoRoot(arena)
        , outputPath(arena)
        , depfilePath(arena)
        , customNamespace(arena)
    {}
};

// Retain parsed values and CLI bindings through the terminal entry's help/error handler.
class PipelineCommandLine final : NoCopy{
public:
    explicit PipelineCommandLine(PipelineTool::Enum inTool);
    PipelineCommandLine(PipelineCommandLine&&) = delete;


public:
    [[nodiscard]] Expected<PipelineOptions> parse(int argc, char** argv, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] int exit(const CLI::ParseError& error)const;

    template<typename ToolBody>
    [[nodiscard]] int run(const int argc, char** argv, ToolBody&& body, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
        return ::InvokeTerminalEntry<CLI::ParseError>([&](){
            auto options = parse(argc, argv, arena, scratchArena);
            if(!options)
                return s_PipelineExitFailure;
            return body(*options);
        }, [&](const CLI::ParseError& error){ return exit(error); }, []()noexcept{ return s_PipelineExitFatal; }
        );
    }


private:
    InteropVector<AInteropString> m_inputs;
    InteropVector<AInteropString> m_assetRoots;
    InteropVector<AInteropString> m_includeDirectories;
    AInteropString m_inputList;
    AInteropString m_repoRoot;
    AInteropString m_outputPath;
    AInteropString m_depfilePath;
    AInteropString m_customNamespace;
    AInteropString m_configuration;
    AInteropString m_assetType;
    PipelineTool::Enum m_tool;
    CLI::App m_app;
};

int RunPipelineTool(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

