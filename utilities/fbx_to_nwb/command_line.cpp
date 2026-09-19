// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <CLI.hpp>
#include <core/common/terminal_entry.h>

#include <core/common/log.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_line{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OptionPresence{
    bool output = false;
    bool assetType = false;
    bool mesh = false;
    bool normalMode = false;
    bool defaultColor = false;
    bool scale = false;
    bool preserveSpace = false;
    bool includeHidden = false;
    bool local = false;
    bool ignoreColors = false;
    bool flipWinding = false;
    bool separateAssets = false;
    bool refreshNwb = false;
};

inline constexpr int s_FbxToNwbExitSuccess = 0;
inline constexpr int s_FbxToNwbExitFailure = 1;
inline constexpr int s_FbxToNwbExitFatal = -1;
inline constexpr char s_FbxToNwbAppName[] = "fbx_to_nwb";
inline constexpr char s_FbxToNwbHelpFlag[] = "-h,--help";
inline constexpr char s_FbxToNwbInputOption[] = "input";
inline constexpr char s_FbxToNwbOutputOption[] = "-o,--output";
inline constexpr char s_FbxToNwbAssetTypeOption[] = "--asset-type";
inline constexpr char s_FbxToNwbVirtualRootOption[] = "--virtual-root";
inline constexpr char s_FbxToNwbMeshOption[] = "-m,--mesh";
inline constexpr char s_FbxToNwbNormalModeOption[] = "--normal-mode";
inline constexpr char s_FbxToNwbScaleOption[] = "--scale";
inline constexpr char s_FbxToNwbTriangleAreaEpsilonOption[] = "--triangle-area-length-squared-epsilon";
inline constexpr char s_FbxToNwbDefaultColorOption[] = "--default-color";
inline constexpr char s_FbxToNwbPreserveSpaceFlag[] = "--preserve-space";
inline constexpr char s_FbxToNwbIncludeHiddenFlag[] = "--include-hidden";
inline constexpr char s_FbxToNwbLocalFlag[] = "--local";
inline constexpr char s_FbxToNwbIgnoreColorsFlag[] = "--ignore-colors";
inline constexpr char s_FbxToNwbFlipWindingFlag[] = "--flip-winding";
inline constexpr char s_FbxToNwbSeparateAssetsFlag[] = "--separate-assets";
inline constexpr char s_FbxToNwbRefreshNwbFlag[] = "--refresh-nwb";
inline constexpr u32 s_CliOptionPresentCount = 0u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PromptString(const AString& label, const AString& defaultValue, AString& outValue, bool& prompted){
    prompted = true;
    NWB_COUT << label;
    if(!defaultValue.empty())
        NWB_COUT << " [" << defaultValue << "]";
    NWB_COUT << ": ";

    AString line;
    if(!ReadTextLine(NWB_CIN, line)){
        outValue = defaultValue;
        return !outValue.empty();
    }

    line = TrimCopy(Move(line));
    outValue = line.empty() ? defaultValue : Move(line);
    return !outValue.empty();
}

bool PromptBool(const AString& label, const bool defaultValue, bool& outValue, bool& prompted){
    prompted = true;
    for(;;){
        NWB_COUT << label << (defaultValue ? " [Y/n]: " : " [y/N]: ");

        AString line;
        if(!ReadTextLine(NWB_CIN, line)){
            outValue = defaultValue;
            return true;
        }

        line = NormalizeOptionText(Move(line));
        if(line.empty()){
            outValue = defaultValue;
            return true;
        }
        if(line == s_ConfirmYesShort || line == s_ConfirmYesText || line == s_ConfirmTrueText || line == s_ConfirmOneText){
            outValue = true;
            return true;
        }
        if(line == s_ConfirmNoShort || line == s_ConfirmNoText || line == s_ConfirmFalseText || line == s_ConfirmZeroText){
            outValue = false;
            return true;
        }

        NWB_COUT << "Please answer y or n.\n";
    }
}

bool PromptDouble(const AString& label, const f64 defaultValue, f64& outValue, bool& prompted){
    prompted = true;
    for(;;){
        NWB_COUT << label << " [" << defaultValue << "]: ";

        AString line;
        if(!ReadTextLine(NWB_CIN, line)){
            outValue = defaultValue;
            return true;
        }

        line = TrimCopy(Move(line));
        if(line.empty()){
            outValue = defaultValue;
            return true;
        }

        f64 parsed = 0.0;
        if(ParseF64FromChars(line.data(), line.data() + line.size(), parsed) && IsFinite(parsed) && parsed > 0.0){
            outValue = parsed;
            return true;
        }

        NWB_COUT << "Please enter a positive finite number.\n";
    }
}

bool ValidateOutputOverwrite(const Path& outputPath, const ImportOptions& options, bool& prompted){
    ErrorCode errorCode;
    const bool exists = FileExists(outputPath, errorCode);
    if(errorCode){
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to query output path: {}"), StringConvert(errorCode.message()));
        return false;
    }
    if(!exists)
        return true;
    if(options.forceOverwrite)
        return true;
    if(options.acceptDefaults){
        NWB_LOGGER_WARNING(NWB_TEXT("Output already exists. Pass --force to overwrite: {}"), PathToString<tchar>(outputPath));
        return false;
    }

    bool overwrite = false;
    if(!PromptBool("Output already exists. Overwrite it?", false, overwrite, prompted))
        return false;
    return overwrite;
}

bool ConfigurePromptsBeforeLoad(ImportOptions& options, const OptionPresence& presence, bool& prompted){
    if(options.inputPath.empty()){
        if(options.acceptDefaults){
            NWB_LOGGER_WARNING(NWB_TEXT("Input FBX or NWB path is required."));
            return false;
        }

        AString input;
        if(!PromptString("Input FBX or NWB path", AString(), input, prompted)){
            NWB_LOGGER_WARNING(NWB_TEXT("Input FBX or NWB path is required."));
            return false;
        }
        options.inputPath = input;
    }
    options.inputPath = UnquoteMatchingAsciiQuotes(Move(options.inputPath));

    if(!presence.preserveSpace && !options.acceptDefaults && !options.listMeshes){
        bool convertSpace = true;
        PromptBool("Convert axes/units to NWB space (+X right, +Y up, +Z forward, 1 unit = 1 meter)?", true, convertSpace, prompted);
        options.preserveSpace = !convertSpace;
    }

    return true;
}

bool ConfigurePromptsAfterLoad(
    ImportOptions& options,
    const OptionPresence& presence,
    const UtilityVector<MeshInstance>& visibleInstances,
    bool& prompted
){
    if(!presence.assetType && !options.acceptDefaults){
        AString assetType;
        AString prompt = "Asset type (";
        prompt += OutputAssetTypeOptionsText();
        prompt += ")";
        PromptString(prompt, options.assetType, assetType, prompted);
        options.assetType = assetType;
    }

    if(!presence.mesh && !options.acceptDefaults){
        PrintMeshInstances(visibleInstances);
        AString selector;
        PromptString("Mesh selector (all, first, index, node name, or mesh name)", options.meshSelector, selector, prompted);
        options.meshSelector = selector;
    }

    if(!presence.output && !options.acceptDefaults){
        AString output;
        const AString defaultOutput = PathToGenericString<AString>(DefaultOutputPath(options.inputPath));
        PromptString("Output .nwb path", defaultOutput, output, prompted);
        options.outputPath = output;
    }
    if(options.outputPath.empty())
        options.outputPath = PathToGenericString<AString>(DefaultOutputPath(options.inputPath));
    options.outputPath = UnquoteMatchingAsciiQuotes(Move(options.outputPath));

    if(!presence.normalMode && !options.acceptDefaults){
        AString normalMode;
        AString prompt = "Normal mode (";
        prompt += NormalModeOptionsText();
        prompt += ")";
        PromptString(prompt, options.normalMode, normalMode, prompted);
        options.normalMode = normalMode;
    }

    if(!presence.scale && !options.acceptDefaults)
        PromptDouble("Additional uniform scale", options.scale, options.scale, prompted);

    if(!presence.local && !options.acceptDefaults)
        PromptBool("Bake node transforms into the mesh?", options.bakeTransforms, options.bakeTransforms, prompted);

    if(!presence.ignoreColors && !options.acceptDefaults)
        PromptBool("Import FBX vertex colors when present?", options.importColors, options.importColors, prompted);

    if(!presence.defaultColor && !options.acceptDefaults){
        AString colorText;
        PromptString("Default RGBA color for vertices without FBX color", options.defaultColorText, colorText, prompted);
        options.defaultColorText = colorText;
    }

    if(!presence.flipWinding && !options.acceptDefaults)
        PromptBool("Flip triangle winding?", options.flipWinding, options.flipWinding, prompted);

    return true;
}

bool SelectedMeshesUseSkinning(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    bool& outUsesSkinning
){
    outUsesSkinning = false;
    bool sawStatic = false;
    bool sawSkinned = false;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            NWB_LOGGER_WARNING(NWB_TEXT("Selected mesh index is out of range"));
            return false;
        }

        const MeshInstance& instance = instances[instanceIndex];
        const bool hasSkin = instance.mesh && instance.mesh->skin_deformers.count != 0u;
        sawSkinned = sawSkinned || hasSkin;
        sawStatic = sawStatic || !hasSkin;
    }

    if(sawStatic && sawSkinned){
        NWB_LOGGER_WARNING(NWB_TEXT("Model export does not support mixed static and skinned source meshes yet"));
        return false;
    }

    outUsesSkinning = sawSkinned;
    return true;
}

bool AssetTypeRequiresSkinning(const OutputAssetType::Enum assetType){
    return assetType == OutputAssetType::Skeleton || assetType == OutputAssetType::Skin;
}

bool AssetTypeCanUseSkinning(const OutputAssetType::Enum assetType){
    return assetType == OutputAssetType::Bunch
        || assetType == OutputAssetType::Model
        || AssetTypeRequiresSkinning(assetType)
    ;
}

bool IsNwbRefreshMode(const ImportOptions& options){
    return options.refreshNwb || LowerPathExtension<AString>(Path(UtilityDetail::Arena(), options.inputPath)) == s_NwbOutputExtension;
}

void WriteRefreshCount(AStringStream& report, const char* name, const usize before, const usize after){
    report << "  " << name << ": " << before << " -> " << after << "\n";
}

void WriteCanonicalizeReport(AStringStream& report, const SourceMeshCanonicalizeReport& canonicalizeReport){
    WriteRefreshCount(report, s_PositionsStreamLabel, canonicalizeReport.before.positions, canonicalizeReport.after.positions);
    WriteRefreshCount(report, s_NormalsStreamLabel, canonicalizeReport.before.normals, canonicalizeReport.after.normals);
    WriteRefreshCount(report, s_TangentsStreamLabel, canonicalizeReport.before.tangents, canonicalizeReport.after.tangents);
    WriteRefreshCount(report, s_Uv0StreamLabel, canonicalizeReport.before.uv0, canonicalizeReport.after.uv0);
    WriteRefreshCount(report, s_ColorsStreamLabel, canonicalizeReport.before.colors, canonicalizeReport.after.colors);
    WriteRefreshCount(report, s_SkinStreamLabel, canonicalizeReport.before.skin, canonicalizeReport.after.skin);
    WriteRefreshCount(report, s_VertexRefsStreamLabel, canonicalizeReport.before.vertexRefs, canonicalizeReport.after.vertexRefs);
    WriteRefreshCount(report, s_IndicesStreamLabel, canonicalizeReport.before.indices, canonicalizeReport.after.indices);
}

int RunNwbRefresh(ImportOptions& options, const OptionPresence& presence, Core::CpuTaskScheduler& cpuScheduler, bool& prompted){
    if(options.listMeshes){
        NWB_LOGGER_WARNING(NWB_TEXT("--list-meshes is only valid for FBX input."));
        return s_FbxToNwbExitFailure;
    }

    if(!presence.output && !options.acceptDefaults){
        AString output;
        PromptString("Output .nwb path", options.inputPath, output, prompted);
        options.outputPath = output;
    }
    if(options.outputPath.empty())
        options.outputPath = options.inputPath;
    options.outputPath = UnquoteMatchingAsciiQuotes(Move(options.outputPath));

    const Path outputPath(UtilityDetail::Arena(), options.outputPath);
    if(outputPath.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Output path is empty."));
        return s_FbxToNwbExitFailure;
    }
    if(!ValidateOutputOverwrite(outputPath, options, prompted))
        return s_FbxToNwbExitFailure;

    SourceMeshCanonicalizeReport canonicalizeReport;
    const Path inputPath(UtilityDetail::Arena(), options.inputPath);
    if(!RefreshNwbMeshAsset(inputPath, outputPath, cpuScheduler, canonicalizeReport))
        return s_FbxToNwbExitFailure;

    AStringStream report;
    report
        << "Refreshed " << PathToGenericString<AString>(outputPath) << "\n"
        << "  input: " << PathToGenericString<AString>(inputPath) << "\n"
    ;
    WriteCanonicalizeReport(report, canonicalizeReport);
    NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report.str()));
    return s_FbxToNwbExitSuccess;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(int argc, char** argv, Core::CpuTaskScheduler& cpuScheduler, bool& prompted){
    ImportOptions options;
    __hidden_command_line::OptionPresence presence;

    CLI::App app{ __hidden_command_line::s_FbxToNwbAppName };
    app.set_help_flag(__hidden_command_line::s_FbxToNwbHelpFlag, "Show help");

    AInteropString inputPath(options.inputPath.data(), options.inputPath.size());
    AInteropString outputPathText(options.outputPath.data(), options.outputPath.size());
    AInteropString assetType(options.assetType.data(), options.assetType.size());
    AInteropString virtualRoot(options.virtualRoot.data(), options.virtualRoot.size());
    AInteropString meshSelector(options.meshSelector.data(), options.meshSelector.size());
    AInteropString normalMode(options.normalMode.data(), options.normalMode.size());
    AInteropString defaultColorText(options.defaultColorText.data(), options.defaultColorText.size());

    bool local = false;
    bool ignoreColors = false;

    app.add_option(__hidden_command_line::s_FbxToNwbInputOption, inputPath, "Input FBX file path");
    CLI::Option* outputOption = app.add_option(__hidden_command_line::s_FbxToNwbOutputOption, outputPathText, "Output NWB asset metadata path");
    AInteropString assetTypeDescription = "Output asset type: ";
    const AString assetTypeOptions = OutputAssetTypeOptionsText();
    assetTypeDescription.append(assetTypeOptions.data(), assetTypeOptions.size());
    CLI::Option* assetTypeOption = app.add_option(__hidden_command_line::s_FbxToNwbAssetTypeOption, assetType, assetTypeDescription);
    app.add_option(__hidden_command_line::s_FbxToNwbVirtualRootOption, virtualRoot, "Virtual asset root used when output path is outside an assets directory");
    CLI::Option* meshOption = app.add_option(__hidden_command_line::s_FbxToNwbMeshOption, meshSelector, "Mesh selector: all, first, zero-based index, node name, or mesh name");
    CLI::Option* normalModeOption = app.add_option(
        __hidden_command_line::s_FbxToNwbNormalModeOption,
        normalMode,
        "Normal mode: imported, smooth shared-position normals, or regenerated per-triangle face normals"
    );
    CLI::Option* scaleOption = app.add_option(__hidden_command_line::s_FbxToNwbScaleOption, options.scale, "Additional uniform scale applied after import");
    app.add_option(
        __hidden_command_line::s_FbxToNwbTriangleAreaEpsilonOption,
        options.triangleAreaLengthSquaredEpsilon,
        "Minimum squared triangle cross-product length kept during import"
    );
    CLI::Option* defaultColorOption = app.add_option(__hidden_command_line::s_FbxToNwbDefaultColorOption, defaultColorText, "Default RGBA color, for example 1,1,1,1");
    CLI::Option* preserveSpaceOption = app.add_flag(__hidden_command_line::s_FbxToNwbPreserveSpaceFlag, options.preserveSpace, "Keep the FBX source axes and units");
    CLI::Option* includeHiddenOption = app.add_flag(__hidden_command_line::s_FbxToNwbIncludeHiddenFlag, options.includeHidden, "Include hidden FBX mesh nodes");
    CLI::Option* localOption = app.add_flag(__hidden_command_line::s_FbxToNwbLocalFlag, local, "Do not bake node transforms into mesh");
    CLI::Option* ignoreColorsOption = app.add_flag(__hidden_command_line::s_FbxToNwbIgnoreColorsFlag, ignoreColors, "Use the default color instead of FBX vertex colors");
    CLI::Option* flipWindingOption = app.add_flag(__hidden_command_line::s_FbxToNwbFlipWindingFlag, options.flipWinding, "Swap the second and third index of every triangle");
    CLI::Option* separateAssetsOption = app.add_flag(__hidden_command_line::s_FbxToNwbSeparateAssetsFlag, options.separateAssets, "Write a model package as separate .nwb files instead of one asset bunch");
    CLI::Option* refreshNwbOption = app.add_flag(__hidden_command_line::s_FbxToNwbRefreshNwbFlag, options.refreshNwb, "Read a mesh .nwb, canonicalize mesh streams, and rewrite it");
    app.add_flag("--force", options.forceOverwrite, "Overwrite an existing output file");
    app.add_flag("-y,--yes", options.acceptDefaults, "Use defaults for any import options that were not supplied");
    app.add_flag("--list-meshes", options.listMeshes, "List importable mesh instances and exit");

    return NWB::Core::Common::InvokeTerminalEntry<CLI::ParseError>([&](){
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

        presence.output = outputOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.assetType = assetTypeOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.mesh = meshOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.normalMode = normalModeOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.scale = scaleOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.defaultColor = defaultColorOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.preserveSpace = preserveSpaceOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.includeHidden = includeHiddenOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.local = localOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.ignoreColors = ignoreColorsOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.flipWinding = flipWindingOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.separateAssets = separateAssetsOption->count() > __hidden_command_line::s_CliOptionPresentCount;
        presence.refreshNwb = refreshNwbOption->count() > __hidden_command_line::s_CliOptionPresentCount;

        if(!__hidden_command_line::ConfigurePromptsBeforeLoad(options, presence, prompted))
            return __hidden_command_line::s_FbxToNwbExitFailure;

        if(!IsFinite(options.scale) || options.scale <= 0.0){
            NWB_LOGGER_WARNING(NWB_TEXT("--scale must be a positive finite number."));
            return __hidden_command_line::s_FbxToNwbExitFailure;
        }
        if(!IsFinite(options.triangleAreaLengthSquaredEpsilon) || options.triangleAreaLengthSquaredEpsilon < 0.0){
            NWB_LOGGER_WARNING(NWB_TEXT("--triangle-area-length-squared-epsilon must be a finite non-negative number."));
            return __hidden_command_line::s_FbxToNwbExitFailure;
        }

        ErrorCode errorCode;
        const bool inputIsRegularFile = IsRegularFile(Path(UtilityDetail::Arena(), options.inputPath), errorCode);
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to query input FBX path: {}"), StringConvert(errorCode.message()));
            return __hidden_command_line::s_FbxToNwbExitFailure;
        }
        if(!inputIsRegularFile){
            NWB_LOGGER_WARNING(NWB_TEXT("Input file was not found: {}"), StringConvert(options.inputPath));
            return __hidden_command_line::s_FbxToNwbExitFailure;
        }

        if(__hidden_command_line::IsNwbRefreshMode(options))
            return __hidden_command_line::RunNwbRefresh(options, presence, cpuScheduler, prompted);

        SceneHandle scene;
        if(!LoadScene(options, scene))
            return __hidden_command_line::s_FbxToNwbExitFailure;

        if(!presence.includeHidden && !options.acceptDefaults && !options.listMeshes)
            __hidden_command_line::PromptBool("Include hidden mesh nodes?", options.includeHidden, options.includeHidden, prompted);

        UtilityVector<MeshInstance> instances = CollectMeshInstances(scene.scene, options.includeHidden);
        if(options.listMeshes){
            PrintMeshInstances(instances);
            return __hidden_command_line::s_FbxToNwbExitSuccess;
        }
        if(instances.empty()){
            if(options.includeHidden)
                NWB_LOGGER_WARNING(NWB_TEXT("No mesh instances found in FBX."));
            else
                NWB_LOGGER_WARNING(NWB_TEXT("No mesh instances found in FBX (use --include-hidden to include hidden nodes)."));
            return __hidden_command_line::s_FbxToNwbExitFailure;
        }

        if(!__hidden_command_line::ConfigurePromptsAfterLoad(options, presence, instances, prompted))
            return __hidden_command_line::s_FbxToNwbExitFailure;

        if(!ValidateAssetTypeText(options.assetType))
            return __hidden_command_line::s_FbxToNwbExitFailure;
        if(!ValidateNormalModeText(options.normalMode))
            return __hidden_command_line::s_FbxToNwbExitFailure;

        Vec4 defaultColor;
        if(!ParseColorText(options.defaultColorText, defaultColor)){
            NWB_LOGGER_WARNING(NWB_TEXT("--default-color must contain four finite numbers, for example 1,1,1,1."));
            return __hidden_command_line::s_FbxToNwbExitFailure;
        }

        UtilityVector<usize> selection;
        if(!SelectMeshInstances(instances, options.meshSelector, selection))
            return __hidden_command_line::s_FbxToNwbExitFailure;

        OutputAssetType::Enum assetTypeValue = OutputAssetType::Mesh;
        if(!ParseAssetTypeText(options.assetType, assetTypeValue)){
            NWB_LOGGER_WARNING(StringConvert(OutputAssetTypeErrorText()));
            return __hidden_command_line::s_FbxToNwbExitFailure;
        }
        bool usesSkinning = false;
        bool wantsSkinning = false;
        if(__hidden_command_line::AssetTypeCanUseSkinning(assetTypeValue)){
            if(!__hidden_command_line::SelectedMeshesUseSkinning(instances, selection, wantsSkinning))
                return __hidden_command_line::s_FbxToNwbExitFailure;
            usesSkinning = wantsSkinning;
        }
        if(__hidden_command_line::AssetTypeRequiresSkinning(assetTypeValue) && !usesSkinning){
            NWB_LOGGER_WARNING(NWB_TEXT("Selected source mesh is not skinned; requested asset type requires skinning."));
            return __hidden_command_line::s_FbxToNwbExitFailure;
        }

        const Path outputPath(UtilityDetail::Arena(), options.outputPath);
        if(outputPath.empty()){
            NWB_LOGGER_WARNING(NWB_TEXT("Output path is empty."));
            return __hidden_command_line::s_FbxToNwbExitFailure;
        }
        if(!__hidden_command_line::ValidateOutputOverwrite(outputPath, options, prompted))
            return __hidden_command_line::s_FbxToNwbExitFailure;

        SourceMeshStreams mesh;
        UtilityVector<ufbx_node*> skeletonJoints;
        UtilityVector<JointMatrix> skeletonBindPoseMatrices;
        UtilityVector<JointMatrix> inverseBindMatrices;
        bool sawVertexColors = false;
        bool sawVertexUvs = false;
        SourceTangentReport tangentReport;
        if(!BuildMesh(
            instances,
            selection,
            options,
            wantsSkinning,
            defaultColor,
            cpuScheduler,
            mesh,
            skeletonJoints,
            skeletonBindPoseMatrices,
            inverseBindMatrices,
            sawVertexColors,
            sawVertexUvs,
            tangentReport
        ))
            return __hidden_command_line::s_FbxToNwbExitFailure;

        if(!WriteNwbAsset(
            outputPath,
            mesh,
            options.assetType,
            options.virtualRoot,
            options.separateAssets,
            skeletonJoints,
            skeletonBindPoseMatrices,
            inverseBindMatrices
        ))
            return __hidden_command_line::s_FbxToNwbExitFailure;

        AStringStream report;
        report
            << "Wrote " << PathToGenericString<AString>(outputPath) << "\n"
            << "  positions: " << mesh.positions.size() << "\n"
            << "  normals: " << mesh.normals.size() << "\n"
            << "  vertex_refs: " << mesh.vertexRefs.size() << "\n"
            << "  triangles: " << (mesh.indices.size() / s_TriangleIndexCount) << "\n"
            << "  asset_type: " << options.assetType << "\n"
            << "  normal_mode: " << options.normalMode << "\n"
            << "  tangents: " << SourceTangentModeText(tangentReport.mode) << "\n"
            << "  vertex colors: " << (sawVertexColors ? s_ImportedSourceLabel : s_DefaultSourceLabel) << "\n";
        if(assetTypeValue == OutputAssetType::Bunch)
            report << "  asset_layout: " << (options.separateAssets ? s_SeparateAssetLayoutLabel : s_DefaultOutputAssetTypeText) << "\n";
        report << "  uv0: " << (sawVertexUvs ? s_ImportedSourceLabel : s_DefaultSourceLabel) << "\n";
        if(!skeletonJoints.empty())
            report << "  skeleton_joints: " << skeletonJoints.size() << "\n";
        if(tangentReport.mode == SourceTangentMode::GeneratedFallback){
            report
                << "  tangent_fallback_vertices: " << tangentReport.fallbackTangentVertexCount << "\n"
                << "  tangent_degenerate_uv_triangles: " << tangentReport.degenerateUvTriangleCount << "\n";
        }
        NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report.str()));

        return __hidden_command_line::s_FbxToNwbExitSuccess;
    }, [&](const CLI::ParseError& error){ return app.exit(error, NWB_COUT, NWB_CERR); }, [](){ return -1; });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

