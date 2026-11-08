// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "import_prompts.h"
#include "fbx_cli_diagnostics.h"
#include "nwb_refresh_command.h"

#include <core/common/log.h>

#include <global/allocation_size.h>
#include <global/command_line.h>
#include <global/terminal_entry.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_line{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<b8> SelectedMeshesUseSkinning(const Span<const MeshInstance> instances, const Span<const usize> selection){
    b8 sawStatic = false;
    b8 sawSkinned = false;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            NWB_LOGGER_WARNING(NWB_TEXT("Selected mesh index is out of range"));
            return MakeUnexpected(Failure{});
        }

        const MeshInstance& instance = instances[instanceIndex];
        const b8 hasSkin = instance.mesh && instance.mesh->skin_deformers.count != 0u;
        sawSkinned = sawSkinned || hasSkin;
        sawStatic = sawStatic || !hasSkin;
    }

    if(sawStatic && sawSkinned){
        NWB_LOGGER_WARNING(NWB_TEXT("Model export does not support mixed static and skinned source meshes yet"));
        return MakeUnexpected(Failure{});
    }

    return sawSkinned;
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


i32 Run(i32 argc, char** argv, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, b8& prompted){
    ImportOptions options;
    OptionPresence presence;

    CLI::App app{ s_FbxToNwbAppName.data() };
    app.set_help_flag(s_FbxToNwbHelpFlag.data(), "Show help");

    AInteropString inputPath(options.inputPath.data(), options.inputPath.size());
    AInteropString outputPathText(options.outputPath.data(), options.outputPath.size());
    AInteropString assetType(options.assetType.data(), options.assetType.size());
    AInteropString virtualRoot(options.virtualRoot.data(), options.virtualRoot.size());
    AInteropString meshSelector(options.meshSelector.data(), options.meshSelector.size());
    AInteropString normalMode(options.normalMode.data(), options.normalMode.size());
    AInteropString defaultColorText(options.defaultColorText.data(), options.defaultColorText.size());

    b8 local = false;
    b8 ignoreColors = false;

    app.add_option(s_FbxToNwbInputOption.data(), inputPath, "Input FBX file path");
    CLI::Option& outputOption = *app.add_option(s_FbxToNwbOutputOption.data(), outputPathText, "Output NWB asset metadata path");
    AInteropString assetTypeDescription = "Output asset type: ";
    const ScratchString assetTypeOptions = OutputAssetTypeOptionsText(scratchArena);
    assetTypeDescription.append(assetTypeOptions.data(), assetTypeOptions.size());
    CLI::Option& assetTypeOption = *app.add_option(s_FbxToNwbAssetTypeOption.data(), assetType, assetTypeDescription);
    app.add_option(s_FbxToNwbVirtualRootOption.data(), virtualRoot, "Virtual asset root used when output path is outside an assets directory");
    CLI::Option& meshOption = *app.add_option(s_FbxToNwbMeshOption.data(), meshSelector, "Mesh selector: all, first, zero-based index, node name, or mesh name");
    CLI::Option& normalModeOption = *app.add_option(s_FbxToNwbNormalModeOption.data(), normalMode, "Normal mode: imported, smooth shared-position normals, or regenerated per-triangle face normals");
    CLI::Option& scaleOption = *app.add_option(s_FbxToNwbScaleOption.data(), options.scale, "Additional uniform scale applied after import");
    app.add_option(s_FbxToNwbTriangleAreaEpsilonOption.data(), options.triangleAreaLengthSquaredEpsilon, "Minimum squared triangle cross-product length kept during import");
    CLI::Option& defaultColorOption = *app.add_option(s_FbxToNwbDefaultColorOption.data(), defaultColorText, "Default RGBA color, for example 1,1,1,1");
    CLI::Option& preserveSpaceOption = *app.add_flag(s_FbxToNwbPreserveSpaceFlag.data(), options.preserveSpace, "Keep the FBX source axes and units");
    CLI::Option& includeHiddenOption = *app.add_flag(s_FbxToNwbIncludeHiddenFlag.data(), options.includeHidden, "Include hidden FBX mesh nodes");
    CLI::Option& localOption = *app.add_flag(s_FbxToNwbLocalFlag.data(), local, "Do not bake node transforms into mesh");
    CLI::Option& ignoreColorsOption = *app.add_flag(s_FbxToNwbIgnoreColorsFlag.data(), ignoreColors, "Use the default color instead of FBX vertex colors");
    CLI::Option& flipWindingOption = *app.add_flag(s_FbxToNwbFlipWindingFlag.data(), options.flipWinding, "Swap the second and third index of every triangle");
    app.add_flag(s_FbxToNwbSeparateAssetsFlag.data(), options.separateAssets, "Write a model package as separate .nwb files instead of one asset bunch");
    app.add_flag(s_FbxToNwbRefreshNwbFlag.data(), options.refreshNwb, "Read a mesh .nwb, canonicalize mesh streams, and rewrite it");
    app.add_flag(s_FbxToNwbForceFlag.data(), options.forceOverwrite, "Overwrite an existing output file");
    app.add_flag(s_FbxToNwbYesFlag.data(), options.acceptDefaults, "Use defaults for any import options that were not supplied");
    app.add_flag(s_FbxToNwbListMeshesFlag.data(), options.listMeshes, "List importable mesh instances and exit");

    return ::InvokeTerminalEntry<CLI::ParseError>([&](){
        app.parse(argc, argv);

        options.inputPath.assign(inputPath.data(), inputPath.size());
        options.outputPath.assign(outputPathText.data(), outputPathText.size());
        options.assetType.assign(assetType.data(), assetType.size());
        options.virtualRoot.assign(virtualRoot.data(), virtualRoot.size());
        options.meshSelector.assign(meshSelector.data(), meshSelector.size());
        options.normalMode.assign(normalMode.data(), normalMode.size());
        options.defaultColorText.assign(defaultColorText.data(), defaultColorText.size());

        options.bakeTransforms = !local;
        options.importColors = !ignoreColors;

        presence.output = outputOption.count() > s_CliOptionPresentCount;
        presence.assetType = assetTypeOption.count() > s_CliOptionPresentCount;
        presence.mesh = meshOption.count() > s_CliOptionPresentCount;
        presence.normalMode = normalModeOption.count() > s_CliOptionPresentCount;
        presence.scale = scaleOption.count() > s_CliOptionPresentCount;
        presence.defaultColor = defaultColorOption.count() > s_CliOptionPresentCount;
        presence.preserveSpace = preserveSpaceOption.count() > s_CliOptionPresentCount;
        presence.includeHidden = includeHiddenOption.count() > s_CliOptionPresentCount;
        presence.local = localOption.count() > s_CliOptionPresentCount;
        presence.ignoreColors = ignoreColorsOption.count() > s_CliOptionPresentCount;
        presence.flipWinding = flipWindingOption.count() > s_CliOptionPresentCount;

        if(!ImportPrompts::ConfigurePromptsBeforeLoad(presence, scratchArena, options, prompted))
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbInputOption, "Input FBX or NWB path is required.", scratchArena);

        if(!IsFinite(options.scale) || options.scale <= 0.0)
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbScaleOption, "Must be a positive finite number.", scratchArena);
        if(!IsFinite(options.triangleAreaLengthSquaredEpsilon) || options.triangleAreaLengthSquaredEpsilon < 0.0)
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbTriangleAreaEpsilonOption, "Must be a finite non-negative number.", scratchArena);

        const auto inputIsRegularFile = IsRegularFile(ScratchPath(scratchArena, options.inputPath));
        if(!inputIsRegularFile && !IsMissingPathError(inputIsRegularFile.error())){
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to query input FBX path: {}"), ConvertString(inputIsRegularFile.error().message()));
            return s_FbxToNwbExitFailure;
        }
        if(!inputIsRegularFile || !*inputIsRegularFile)
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbInputOption, "Input path must name an existing regular file.", scratchArena);

        if(NwbRefreshCommand::IsNwbRefreshMode(options, scratchArena))
            return NwbRefreshCommand::Run(app, presence, cpuScheduler, scratchArena, options, prompted);

        auto scene = LoadScene(options, scratchArena);
        if(!scene)
            return s_FbxToNwbExitFailure;

        if(!presence.includeHidden && !options.acceptDefaults && !options.listMeshes)
            options.includeHidden = ImportPrompts::PromptBool("Include hidden mesh nodes?", options.includeHidden, scratchArena, prompted);

        UtilityVector<MeshInstance> instances = CollectMeshInstances(*scene->scene, options.includeHidden);
        if(options.listMeshes){
            PrintMeshInstances(instances, scratchArena);
            return s_FbxToNwbExitSuccess;
        }
        if(instances.empty()){
            if(options.includeHidden)
                NWB_LOGGER_WARNING(NWB_TEXT("No mesh instances found in FBX."));
            else
                NWB_LOGGER_WARNING(NWB_TEXT("No mesh instances found in FBX (use --include-hidden to include hidden nodes)."));
            return s_FbxToNwbExitFailure;
        }

        if(!ImportPrompts::ConfigurePromptsAfterLoad(presence, instances, scratchArena, options, prompted))
            return s_FbxToNwbExitFailure;

        if(!ValidateAssetTypeText(options.assetType))
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbAssetTypeOption, OutputAssetTypeErrorText(scratchArena), scratchArena);
        if(!ValidateNormalModeText(options.normalMode))
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbNormalModeOption, NormalModeErrorText(scratchArena), scratchArena);

        const auto defaultColor = ParseColorText(options.defaultColorText, scratchArena);
        if(!defaultColor)
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbDefaultColorOption, "Must contain four finite numbers, for example 1,1,1,1.", scratchArena);

        auto selectionResult = SelectMeshInstances(instances, options.meshSelector);
        if(!selectionResult)
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbMeshOption, "Must select an available zero-based index, node name, or mesh name.", scratchArena);

        const UtilityVector<usize>& selection = *selectionResult;
        const auto assetTypeResult = ParseAssetTypeText(options.assetType);
        if(!assetTypeResult)
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbAssetTypeOption, OutputAssetTypeErrorText(scratchArena), scratchArena);
        const OutputAssetType::Enum assetTypeValue = *assetTypeResult;
        if(options.separateAssets && assetTypeValue != OutputAssetType::Bunch)
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbSeparateAssetsFlag, "Only valid with asset type 'bunch'.", scratchArena);
        b8 usesSkinning = false;
        b8 wantsSkinning = false;
        if(AssetTypeCanUseSkinning(assetTypeValue)){
            const auto selectedUsesSkinning = __hidden_command_line::SelectedMeshesUseSkinning(instances, selection);
            if(!selectedUsesSkinning)
                return s_FbxToNwbExitFailure;
            usesSkinning = wantsSkinning = *selectedUsesSkinning;
        }
        if(AssetTypeRequiresSkinning(assetTypeValue) && !usesSkinning)
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbAssetTypeOption, "Selected source mesh is not skinned; requested asset type requires skinning.", scratchArena);

        const ScratchPath outputPath(scratchArena, options.outputPath);
        if(outputPath.empty())
            return FbxCliDiagnostics::ExitValidation(app, s_FbxToNwbOutputOption, s_OutputPathEmptyText, scratchArena);
        if(!ImportPrompts::ValidateOutputOverwrite(outputPath, options, scratchArena, prompted))
            return s_FbxToNwbExitFailure;
        if(options.separateAssets){
            const ScratchPath packageDirectory = ScratchPath(scratchArena, outputPath.parentPathView()) / outputPath.stemView();
            if(!ImportPrompts::ValidateOutputOverwrite(packageDirectory / s_MeshSplitFileName, options, scratchArena, prompted))
                return s_FbxToNwbExitFailure;
            if(wantsSkinning && (!ImportPrompts::ValidateOutputOverwrite(packageDirectory / s_SkeletonSplitFileName, options, scratchArena, prompted)
                || !ImportPrompts::ValidateOutputOverwrite(packageDirectory / s_SkinSplitFileName, options, scratchArena, prompted)))
                return s_FbxToNwbExitFailure;
        }

        auto built = BuildMesh(instances, selection, options, wantsSkinning, *defaultColor, cpuScheduler, scratchArena);
        if(!built)
            return s_FbxToNwbExitFailure;
        const SourceMeshStreams& mesh = built->mesh;
        const UtilityVector<ufbx_node*>& skeletonJoints = built->skeletonJoints;
        const UtilityVector<JointMatrix>& skeletonBindPoseMatrices = built->skeletonBindPoseMatrices;
        const UtilityVector<JointMatrix>& inverseBindMatrices = built->inverseBindMatrices;
        const b8 sawVertexColors = built->sawVertexColors;
        const b8 sawVertexUvs = built->sawVertexUvs;
        const SourceTangentReport& tangentReport = built->tangentReport;

        if(!WriteNwbAsset(outputPath, mesh, options.assetType, options.virtualRoot, options.separateAssets, skeletonJoints, skeletonBindPoseMatrices, inverseBindMatrices, scratchArena))
            return s_FbxToNwbExitFailure;

        const auto outputText = PathToGenericString(scratchArena, outputPath);
        ScratchStringStream report(s_FileOpenWrite, scratchArena);
        report
            << "Wrote " << outputText << s_ReportLineEndText
            << "  positions: " << mesh.positions.size() << s_ReportLineEndText
            << "  normals: " << mesh.normals.size() << s_ReportLineEndText
            << "  vertex_refs: " << mesh.vertexRefs.size() << s_ReportLineEndText
            << "  triangles: " << (mesh.indices.size() / Impl::s_MeshletTriangleIndexCount) << s_ReportLineEndText
            << "  asset_type: " << options.assetType << s_ReportLineEndText
            << "  normal_mode: " << options.normalMode << s_ReportLineEndText
            << "  tangents: " << SourceTangentModeText(tangentReport.mode) << s_ReportLineEndText
            << "  vertex colors: " << (sawVertexColors ? s_ImportedSourceLabel : s_DefaultSourceLabel) << s_ReportLineEndText;
        if(assetTypeValue == OutputAssetType::Bunch)
            report << "  asset_layout: " << (options.separateAssets ? s_SeparateAssetLayoutLabel : s_DefaultOutputAssetTypeText) << s_ReportLineEndText;
        report << "  uv0: " << (sawVertexUvs ? s_ImportedSourceLabel : s_DefaultSourceLabel) << s_ReportLineEndText;
        if(!skeletonJoints.empty())
            report << "  skeleton_joints: " << skeletonJoints.size() << s_ReportLineEndText;
        if(tangentReport.mode == SourceTangentMode::GeneratedFallback)
            report << "  tangent_fallback_vertices: " << tangentReport.fallbackTangentVertexCount << s_ReportLineEndText << "  tangent_degenerate_uv_triangles: " << tangentReport.degenerateUvTriangleCount << s_ReportLineEndText;
        NWB_LOGGER_ESSENTIAL_INFO(ConvertString(report.str()));

        return s_FbxToNwbExitSuccess;
    }, [&](const CLI::ParseError& error){ return FbxCliDiagnostics::ExitParseError(app, error, scratchArena); }, [](){
        NWB_LOGGER_ERROR(NWB_TEXT("FBX conversion terminated with an unexpected exception"));
        return -1;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

