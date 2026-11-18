// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/alloc/scratch.h>
#include <core/assets/global.h>
#include <core/common/application_entry.h>

#include <global/terminal_entry.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_PIPELINE_BEGIN
#define NWB_PIPELINE_BEGIN NWB_BEGIN namespace Pipeline{
#define NWB_PIPELINE_END }; NWB_END
#endif


NWB_PIPELINE_BEGIN

// The compiler must start before ADHD can generate the asset tools.
namespace CompilerBootstrap{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PipelineTool{
    enum Enum : u8{
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
inline constexpr AStringView s_PipelineIncludeDirOption = "-I,--include-dir";
inline constexpr AStringView s_PipelineDepfileOption = "--depfile";
inline constexpr AStringView s_PipelineNamespaceOption = "--namespace";
inline constexpr AStringView s_PipelineNamespaceMapOption = "--namespace-map";
inline constexpr AStringView s_PipelineCppHeaderOption = "--cpp-header";
inline constexpr AStringView s_PipelineCppIncludeDirOption = "--cpp-include-dir";
inline constexpr AStringView s_PipelineDefineOption = "-D,--define";
inline constexpr AStringView s_PipelineCppDefineOption = "--cpp-define";
inline constexpr AStringView s_PipelineCppArgOption = "--cpp-arg";
inline constexpr AStringView s_PipelineEmitCppExternsOption = "--emit-cpp-externs";
inline constexpr AStringView s_PipelineCliHelpRequestName = "CallForHelp";

struct PipelineOptions{
    Core::Assets::AssetVector<Core::Assets::AssetString> inputs;
    Core::Assets::AssetVector<Core::Assets::AssetString> includeDirectories;
    Core::Assets::AssetVector<Core::Assets::AssetString> cppHeaders;
    Core::Assets::AssetVector<Core::Assets::AssetString> cppIncludeDirectories;
    Core::Assets::AssetVector<Core::Assets::AssetString> macroDefinitions;
    Core::Assets::AssetVector<Core::Assets::AssetString> cppDefines;
    Core::Assets::AssetVector<Core::Assets::AssetString> cppCompilerArgs;
    Core::Assets::AssetString outputPath;
    Core::Assets::AssetString depfilePath;
    Core::Assets::AssetString customNamespace;
    Core::Assets::AssetString namespaceMapPath;
    Core::Assets::AssetString emitCppExternsPath;


public:
    explicit PipelineOptions(Core::Assets::AssetArena& arena)noexcept
        : inputs(arena)
        , includeDirectories(arena)
        , cppHeaders(arena)
        , cppIncludeDirectories(arena)
        , macroDefinitions(arena)
        , cppDefines(arena)
        , cppCompilerArgs(arena)
        , outputPath(arena)
        , depfilePath(arena)
        , customNamespace(arena)
        , namespaceMapPath(arena)
        , emitCppExternsPath(arena)
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
        }, [&](const CLI::ParseError& error){ return exit(error); }, []()noexcept{ return s_PipelineExitFatal; });
    }


private:
    InteropVector<AInteropString> m_inputs;
    InteropVector<AInteropString> m_includeDirectories;
    InteropVector<AInteropString> m_cppHeaders;
    InteropVector<AInteropString> m_cppIncludeDirectories;
    InteropVector<AInteropString> m_macroDefinitions;
    InteropVector<AInteropString> m_cppDefines;
    InteropVector<AInteropString> m_cppCompilerArgs;
    AInteropString m_inputList;
    AInteropString m_outputPath;
    AInteropString m_depfilePath;
    AInteropString m_customNamespace;
    AInteropString m_namespaceMapPath;
    AInteropString m_emitCppExternsPath;
    CLI::App m_app;
};

};

using namespace CompilerBootstrap;

int RunPipelineTool(int argc, char** argv);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_PIPELINE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

