// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "nwb_refresh_command.h"
#include "fbx_cli_diagnostics.h"
#include "import_prompts.h"

#include <core/common/log.h>

#include <global/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


b8 NwbRefreshCommand::IsNwbRefreshMode(const ImportOptions& options, Core::Alloc::ScratchArena& scratchArena){
    if(options.refreshNwb)
        return true;
    const ScratchPath inputPath(scratchArena, options.inputPath);
    static constexpr Array<AStringView, 1u> s_Extensions = { Core::Assets::s_NwbExtension };
    return PathHasListedExtension(inputPath, s_Extensions);
}

void NwbRefreshCommand::WriteRefreshCount(const AStringView name, const usize before, const usize after, ScratchStringStream& inOutReport){
    inOutReport << "  " << name << ": " << before << " -> " << after << s_ReportLineEndText;
}

void NwbRefreshCommand::WriteCanonicalizeReport(const SourceMeshCanonicalizeReport& canonicalizeReport, ScratchStringStream& inOutReport){
    WriteRefreshCount(Impl::s_MeshPositionsFieldNameView, canonicalizeReport.before.positions, canonicalizeReport.after.positions, inOutReport);
    WriteRefreshCount(Impl::s_MeshNormalsFieldNameView, canonicalizeReport.before.normals, canonicalizeReport.after.normals, inOutReport);
    WriteRefreshCount(Impl::s_MeshTangentsFieldNameView, canonicalizeReport.before.tangents, canonicalizeReport.after.tangents, inOutReport);
    WriteRefreshCount(Impl::s_MeshUv0FieldNameView, canonicalizeReport.before.uv0, canonicalizeReport.after.uv0, inOutReport);
    WriteRefreshCount(Impl::s_MeshColorsFieldNameView, canonicalizeReport.before.colors, canonicalizeReport.after.colors, inOutReport);
    WriteRefreshCount(s_SkinStreamLabel, canonicalizeReport.before.skin, canonicalizeReport.after.skin, inOutReport);
    WriteRefreshCount(Impl::s_MeshVertexRefsFieldNameView, canonicalizeReport.before.vertexRefs, canonicalizeReport.after.vertexRefs, inOutReport);
    WriteRefreshCount(Impl::s_MeshIndicesFieldNameView, canonicalizeReport.before.indices, canonicalizeReport.after.indices, inOutReport);
}

i32 NwbRefreshCommand::Run(const CLI::App& app, const OptionPresence& presence, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, ImportOptions& inOutOptions, b8& inOutPrompted){
    if(inOutOptions.listMeshes)
        return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbListMeshesFlag, "Only valid for FBX input.", scratchArena);

    if(!presence.output && !inOutOptions.acceptDefaults){
        auto output = ImportPrompts::PromptString(s_OutputPathPromptLabel, inOutOptions.inputPath, inOutPrompted);
        if(!output)
            return s_FbxToNwbExitFailure;
        inOutOptions.outputPath = Move(*output);
    }
    if(inOutOptions.outputPath.empty())
        inOutOptions.outputPath = inOutOptions.inputPath;
    inOutOptions.outputPath = UnquoteMatchingAsciiQuotes(Move(inOutOptions.outputPath));

    const ScratchPath outputPath(scratchArena, inOutOptions.outputPath);
    if(outputPath.empty())
        return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbOutputOption, s_OutputPathEmptyText, scratchArena);
    if(!ImportPrompts::ValidateOutputOverwrite(outputPath, inOutOptions, scratchArena, inOutPrompted))
        return s_FbxToNwbExitFailure;

    const ScratchPath inputPath(scratchArena, inOutOptions.inputPath);
    const auto canonicalizeReport = RefreshNwbMeshAsset(inputPath, outputPath, cpuScheduler, scratchArena);
    if(!canonicalizeReport)
        return s_FbxToNwbExitFailure;

    const auto outputText = PathToGenericString(scratchArena, outputPath);
    const auto inputText = PathToGenericString(scratchArena, inputPath);
    ScratchStringStream report(s_FileOpenWrite, scratchArena);
    report << "Refreshed " << outputText << s_ReportLineEndText << "  input: " << inputText << s_ReportLineEndText;
    WriteCanonicalizeReport(*canonicalizeReport, report);
    NWB_LOGGER_ESSENTIAL_INFO(ConvertString(report.str()));
    return s_FbxToNwbExitSuccess;
}


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
