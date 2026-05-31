// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <CLI.hpp>

#include <core/common/log.h>

#include <string>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_line{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OptionPresence{
    bool input = false;
    bool output = false;
    bool meshClass = false;
    bool mesh = false;
    bool normalMode = false;
    bool defaultColor = false;
    bool scale = false;
    bool preserveSpace = false;
    bool includeHidden = false;
    bool local = false;
    bool ignoreColors = false;
    bool flipWinding = false;
};


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

    line = Trim(Move(line));
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

        line = ToLower(Trim(Move(line)));
        if(line.empty()){
            outValue = defaultValue;
            return true;
        }
        if(line == "y" || line == "yes" || line == "true" || line == "1"){
            outValue = true;
            return true;
        }
        if(line == "n" || line == "no" || line == "false" || line == "0"){
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

        line = Trim(Move(line));
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
            NWB_LOGGER_WARNING(NWB_TEXT("Input FBX path is required."));
            return false;
        }

        AString input;
        if(!PromptString("Input FBX path", AString(), input, prompted)){
            NWB_LOGGER_WARNING(NWB_TEXT("Input FBX path is required."));
            return false;
        }
        options.inputPath = input;
    }
    options.inputPath = UnquotePath(Move(options.inputPath));

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
    if(!presence.meshClass && !options.acceptDefaults){
        AString meshClass;
        AString prompt = "Asset type (";
        prompt += MeshClassOptionsText();
        prompt += ")";
        PromptString(prompt, options.meshClass, meshClass, prompted);
        options.meshClass = meshClass;
    }

    if(!presence.mesh && !options.acceptDefaults){
        PrintMeshInstances(visibleInstances);
        AString selector;
        PromptString("Mesh selector (all, first, index, node name, or mesh name)", options.meshSelector, selector, prompted);
        options.meshSelector = selector;
    }

    if(!presence.output && !options.acceptDefaults){
        AString output;
        const AString defaultOutput = PathToUtf8(DefaultOutputPath(options.inputPath));
        PromptString("Output .nwb path", defaultOutput, output, prompted);
        options.outputPath = output;
    }
    if(options.outputPath.empty())
        options.outputPath = PathToUtf8(DefaultOutputPath(options.inputPath));
    options.outputPath = UnquotePath(Move(options.outputPath));

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(int argc, char** argv, bool& prompted){
    ImportOptions options;
    __hidden_command_line::OptionPresence presence;

    CLI::App app{ "fbx_to_nwb" };
    app.set_help_flag("-h,--help", "Show help");

    std::string inputPath(options.inputPath.data(), options.inputPath.size());
    std::string outputPathText(options.outputPath.data(), options.outputPath.size());
    std::string meshClass(options.meshClass.data(), options.meshClass.size());
    std::string meshSelector(options.meshSelector.data(), options.meshSelector.size());
    std::string normalMode(options.normalMode.data(), options.normalMode.size());
    std::string defaultColorText(options.defaultColorText.data(), options.defaultColorText.size());

    bool local = false;
    bool ignoreColors = false;

    CLI::Option* inputOption = app.add_option("input", inputPath, "Input FBX file path");
    CLI::Option* outputOption = app.add_option("-o,--output", outputPathText, "Output NWB mesh metadata path");
    std::string meshClassDescription = "Output asset type: ";
    const AString meshClassOptions = MeshClassOptionsText();
    meshClassDescription.append(meshClassOptions.data(), meshClassOptions.size());
    CLI::Option* meshClassOption = app.add_option("--asset-type", meshClass, meshClassDescription);
    CLI::Option* meshOption = app.add_option("-m,--mesh", meshSelector, "Mesh selector: all, first, zero-based index, node name, or mesh name");
    CLI::Option* normalModeOption = app.add_option(
        "--normal-mode",
        normalMode,
        "Normal mode: imported, smooth shared-position normals, or regenerated per-triangle face normals"
    );
    CLI::Option* scaleOption = app.add_option("--scale", options.scale, "Additional uniform scale applied after import");
    app.add_option(
        "--triangle-area-length-squared-epsilon",
        options.triangleAreaLengthSquaredEpsilon,
        "Minimum squared triangle cross-product length kept during import"
    );
    CLI::Option* defaultColorOption = app.add_option("--default-color", defaultColorText, "Default RGBA color, for example 1,1,1,1");
    CLI::Option* preserveSpaceOption = app.add_flag("--preserve-space", options.preserveSpace, "Keep the FBX source axes and units");
    CLI::Option* includeHiddenOption = app.add_flag("--include-hidden", options.includeHidden, "Include hidden FBX mesh nodes");
    CLI::Option* localOption = app.add_flag("--local", local, "Do not bake node transforms into mesh");
    CLI::Option* ignoreColorsOption = app.add_flag("--ignore-colors", ignoreColors, "Use the default color instead of FBX vertex colors");
    CLI::Option* flipWindingOption = app.add_flag("--flip-winding", options.flipWinding, "Swap the second and third index of every triangle");
    app.add_flag("--force", options.forceOverwrite, "Overwrite an existing output file");
    app.add_flag("-y,--yes", options.acceptDefaults, "Use defaults for any import options that were not supplied");
    app.add_flag("--list-meshes", options.listMeshes, "List importable mesh instances and exit");

    try{
        app.parse(argc, argv);
    }
    catch(const CLI::ParseError& error){
        return app.exit(error, NWB_COUT, NWB_CERR);
    }

    options.inputPath.assign(inputPath.data(), inputPath.size());
    options.outputPath.assign(outputPathText.data(), outputPathText.size());
    options.meshClass.assign(meshClass.data(), meshClass.size());
    options.meshSelector.assign(meshSelector.data(), meshSelector.size());
    options.normalMode.assign(normalMode.data(), normalMode.size());
    options.defaultColorText.assign(defaultColorText.data(), defaultColorText.size());

    options.bakeTransforms = !local;
    options.importColors = !ignoreColors;

    presence.input = inputOption->count() > 0u;
    presence.output = outputOption->count() > 0u;
    presence.meshClass = meshClassOption->count() > 0u;
    presence.mesh = meshOption->count() > 0u;
    presence.normalMode = normalModeOption->count() > 0u;
    presence.scale = scaleOption->count() > 0u;
    presence.defaultColor = defaultColorOption->count() > 0u;
    presence.preserveSpace = preserveSpaceOption->count() > 0u;
    presence.includeHidden = includeHiddenOption->count() > 0u;
    presence.local = localOption->count() > 0u;
    presence.ignoreColors = ignoreColorsOption->count() > 0u;
    presence.flipWinding = flipWindingOption->count() > 0u;

    if(!__hidden_command_line::ConfigurePromptsBeforeLoad(options, presence, prompted))
        return 1;

    if(!IsFinite(options.scale) || options.scale <= 0.0){
        NWB_LOGGER_WARNING(NWB_TEXT("--scale must be a positive finite number."));
        return 1;
    }
    if(!IsFinite(options.triangleAreaLengthSquaredEpsilon) || options.triangleAreaLengthSquaredEpsilon < 0.0){
        NWB_LOGGER_WARNING(NWB_TEXT("--triangle-area-length-squared-epsilon must be a finite non-negative number."));
        return 1;
    }

    ErrorCode errorCode;
    const bool inputIsRegularFile = IsRegularFile(PathFromUtf8(options.inputPath), errorCode);
    if(errorCode && !IsMissingPathError(errorCode)){
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to query input FBX path: {}"), StringConvert(errorCode.message()));
        return 1;
    }
    if(!inputIsRegularFile){
        NWB_LOGGER_WARNING(NWB_TEXT("Input FBX file was not found: {}"), StringConvert(options.inputPath));
        return 1;
    }

    SceneHandle scene;
    if(!LoadScene(options, scene))
        return 1;

    if(!presence.includeHidden && !options.acceptDefaults && !options.listMeshes)
        __hidden_command_line::PromptBool("Include hidden mesh nodes?", options.includeHidden, options.includeHidden, prompted);

    UtilityVector<MeshInstance> instances = CollectMeshInstances(scene.scene, options.includeHidden);
    if(options.listMeshes){
        PrintMeshInstances(instances);
        return 0;
    }
    if(instances.empty()){
        if(options.includeHidden)
            NWB_LOGGER_WARNING(NWB_TEXT("No mesh instances found in FBX."));
        else
            NWB_LOGGER_WARNING(NWB_TEXT("No mesh instances found in FBX (use --include-hidden to include hidden nodes)."));
        return 1;
    }

    if(!__hidden_command_line::ConfigurePromptsAfterLoad(options, presence, instances, prompted))
        return 1;

    if(!ValidateMeshClassText(options.meshClass))
        return 1;
    if(!ValidateNormalModeText(options.normalMode))
        return 1;

    Vec4 defaultColor;
    if(!ParseColorText(options.defaultColorText, defaultColor)){
        NWB_LOGGER_WARNING(NWB_TEXT("--default-color must contain four finite numbers, for example 1,1,1,1."));
        return 1;
    }

    UtilityVector<usize> selection;
    if(!SelectMeshInstances(instances, options.meshSelector, selection))
        return 1;

    const Path outputPath = PathFromUtf8(options.outputPath);
    if(outputPath.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Output path is empty."));
        return 1;
    }
    if(!__hidden_command_line::ValidateOutputOverwrite(outputPath, options, prompted))
        return 1;

    SourceMeshStreams mesh;
    u32 skeletonJointCount = 0u;
    UtilityVector<MeshJointMatrix> inverseBindMatrices;
    bool sawVertexColors = false;
    bool sawVertexUvs = false;
    SourceTangentReport tangentReport;
    if(!BuildMesh(
        instances,
        selection,
        options,
        defaultColor,
        mesh,
        skeletonJointCount,
        inverseBindMatrices,
        sawVertexColors,
        sawVertexUvs,
        tangentReport
    ))
        return 1;

    if(!WriteNwbMesh(
        outputPath,
        mesh,
        skeletonJointCount,
        inverseBindMatrices,
        options.meshClass
    ))
        return 1;

    AStringStream report;
    report
        << "Wrote " << PathToUtf8(outputPath) << "\n"
        << "  positions: " << mesh.positions.size() << "\n"
        << "  normals: " << mesh.normals.size() << "\n"
        << "  vertex_refs: " << mesh.vertexRefs.size() << "\n"
        << "  triangles: " << (mesh.indices.size() / 3u) << "\n"
        << "  asset_type: " << (IsNormalizedSkinnedMeshClass(options.meshClass) ? "skinned_mesh" : "mesh") << "\n"
        << "  normal_mode: " << options.normalMode << "\n"
        << "  tangents: " << SourceTangentModeText(tangentReport.mode) << "\n"
        << "  vertex colors: " << (sawVertexColors ? "imported" : "default") << "\n";
    report << "  uv0: " << (sawVertexUvs ? "imported" : "default") << "\n";
    if(tangentReport.mode == SourceTangentMode::GeneratedFallback){
        report
            << "  tangent_fallback_vertices: " << tangentReport.fallbackTangentVertexCount << "\n"
            << "  tangent_degenerate_uv_triangles: " << tangentReport.degenerateUvTriangleCount << "\n";
    }
    if(IsNormalizedSkinnedMeshClass(options.meshClass))
        report << "  skeleton joints: " << skeletonJointCount << "\n";
    NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report.str()));

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

