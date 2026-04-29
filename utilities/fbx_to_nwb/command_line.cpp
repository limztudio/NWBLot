// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"

#include <CLI.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <system_error>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_line{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OptionPresence{
    bool input = false;
    bool output = false;
    bool assetKind = false;
    bool mesh = false;
    bool indexType = false;
    bool defaultColor = false;
    bool scale = false;
    bool preserveSpace = false;
    bool includeHidden = false;
    bool local = false;
    bool ignoreColors = false;
    bool flipWinding = false;
    bool noDeduplicate = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PromptString(const AString& label, const AString& defaultValue, AString& outValue, bool& prompted){
    prompted = true;
    NWB_COUT << label;
    if(!defaultValue.empty())
        NWB_COUT << " [" << defaultValue << "]";
    NWB_COUT << ": ";

    AString line;
    if(!std::getline(std::cin, line)){
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
        if(!std::getline(std::cin, line)){
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
        if(!std::getline(std::cin, line)){
            outValue = defaultValue;
            return true;
        }

        line = Trim(Move(line));
        if(line.empty()){
            outValue = defaultValue;
            return true;
        }

        char* end = nullptr;
        const f64 parsed = std::strtod(line.c_str(), &end);
        if(end && *end == '\0' && IsFinite(parsed) && parsed > 0.0){
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
        NWB_CERR << "Failed to query output path: " << errorCode.message() << "\n";
        return false;
    }
    if(!exists)
        return true;
    if(options.forceOverwrite)
        return true;
    if(options.acceptDefaults){
        NWB_CERR << "Output already exists. Pass --force to overwrite: " << PathToUtf8(outputPath) << "\n";
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
            NWB_CERR << "Input FBX path is required.\n";
            return false;
        }

        AString input;
        if(!PromptString("Input FBX path", AString(), input, prompted)){
            NWB_CERR << "Input FBX path is required.\n";
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
    if(!presence.assetKind && !options.acceptDefaults){
        AString assetKind;
        PromptString("NWB geometry type (geometry or deformable_geometry)", options.assetKind, assetKind, prompted);
        options.assetKind = assetKind;
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

    if(!presence.indexType && !options.acceptDefaults){
        AString indexType;
        PromptString("Index type (auto, u16, or u32)", options.indexType, indexType, prompted);
        options.indexType = indexType;
    }

    if(!presence.scale && !options.acceptDefaults)
        PromptDouble("Additional uniform scale", options.scale, options.scale, prompted);

    if(!presence.local && !options.acceptDefaults)
        PromptBool("Bake node transforms into the geometry?", options.bakeTransforms, options.bakeTransforms, prompted);

    if(!presence.ignoreColors && !options.acceptDefaults)
        PromptBool("Import FBX vertex colors when present?", options.importColors, options.importColors, prompted);

    if(!presence.defaultColor && !options.acceptDefaults){
        AString colorText;
        PromptString("Default RGBA color for vertices without FBX color", options.defaultColorText, colorText, prompted);
        options.defaultColorText = colorText;
    }

    if(!presence.flipWinding && !options.acceptDefaults)
        PromptBool("Flip triangle winding?", options.flipWinding, options.flipWinding, prompted);

    if(!presence.noDeduplicate && !options.acceptDefaults)
        PromptBool("Deduplicate equal vertices?", options.deduplicate, options.deduplicate, prompted);

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

    bool local = false;
    bool ignoreColors = false;
    bool noDeduplicate = false;

    CLI::Option* inputOption = app.add_option("input", options.inputPath, "Input FBX file path");
    CLI::Option* outputOption = app.add_option("-o,--output", options.outputPath, "Output NWB geometry metadata path");
    CLI::Option* assetKindOption = app.add_option("--geometry-type", options.assetKind, "NWB geometry type: geometry or deformable_geometry");
    CLI::Option* meshOption = app.add_option("-m,--mesh", options.meshSelector, "Mesh selector: all, first, zero-based index, node name, or mesh name");
    CLI::Option* indexTypeOption = app.add_option("--index-type", options.indexType, "Index type: auto, u16, or u32");
    CLI::Option* scaleOption = app.add_option("--scale", options.scale, "Additional uniform scale applied after import");
    CLI::Option* defaultColorOption = app.add_option("--default-color", options.defaultColorText, "Default RGBA color, for example 1,1,1,1");
    CLI::Option* preserveSpaceOption = app.add_flag("--preserve-space", options.preserveSpace, "Keep the FBX source axes and units");
    CLI::Option* includeHiddenOption = app.add_flag("--include-hidden", options.includeHidden, "Include hidden FBX mesh nodes");
    CLI::Option* localOption = app.add_flag("--local", local, "Do not bake node transforms into geometry");
    CLI::Option* ignoreColorsOption = app.add_flag("--ignore-colors", ignoreColors, "Use the default color instead of FBX vertex colors");
    CLI::Option* flipWindingOption = app.add_flag("--flip-winding", options.flipWinding, "Swap the second and third index of every triangle");
    CLI::Option* noDeduplicateOption = app.add_flag("--no-deduplicate", noDeduplicate, "Write one vertex per triangle corner");
    app.add_flag("--force", options.forceOverwrite, "Overwrite an existing output file");
    app.add_flag("-y,--yes", options.acceptDefaults, "Use defaults for any import options that were not supplied");
    app.add_flag("--list-meshes", options.listMeshes, "List importable mesh instances and exit");

    try{
        app.parse(argc, argv);
    }
    catch(const CLI::ParseError& error){
        return app.exit(error, NWB_COUT, NWB_CERR);
    }

    options.bakeTransforms = !local;
    options.importColors = !ignoreColors;
    options.deduplicate = !noDeduplicate;

    presence.input = inputOption->count() > 0u;
    presence.output = outputOption->count() > 0u;
    presence.assetKind = assetKindOption->count() > 0u;
    presence.mesh = meshOption->count() > 0u;
    presence.indexType = indexTypeOption->count() > 0u;
    presence.scale = scaleOption->count() > 0u;
    presence.defaultColor = defaultColorOption->count() > 0u;
    presence.preserveSpace = preserveSpaceOption->count() > 0u;
    presence.includeHidden = includeHiddenOption->count() > 0u;
    presence.local = localOption->count() > 0u;
    presence.ignoreColors = ignoreColorsOption->count() > 0u;
    presence.flipWinding = flipWindingOption->count() > 0u;
    presence.noDeduplicate = noDeduplicateOption->count() > 0u;

    if(!__hidden_command_line::ConfigurePromptsBeforeLoad(options, presence, prompted))
        return 1;

    if(!IsFinite(options.scale) || options.scale <= 0.0){
        NWB_CERR << "--scale must be a positive finite number.\n";
        return 1;
    }

    ErrorCode errorCode;
    if(!IsRegularFile(PathFromUtf8(options.inputPath), errorCode)){
        NWB_CERR << "Input FBX file was not found: " << options.inputPath << "\n";
        return 1;
    }

    SceneHandle scene;
    AString error;
    if(!LoadScene(options, scene, error)){
        NWB_CERR << error << "\n";
        return 1;
    }

    if(!presence.includeHidden && !options.acceptDefaults && !options.listMeshes)
        __hidden_command_line::PromptBool("Include hidden mesh nodes?", options.includeHidden, options.includeHidden, prompted);

    UtilityVector<MeshInstance> instances = CollectMeshInstances(scene.scene, options.includeHidden);
    if(options.listMeshes){
        PrintMeshInstances(instances);
        return 0;
    }
    if(instances.empty()){
        NWB_CERR << "No mesh instances found in FBX";
        if(!options.includeHidden)
            NWB_CERR << " (use --include-hidden to include hidden nodes)";
        NWB_CERR << ".\n";
        return 1;
    }

    if(!__hidden_command_line::ConfigurePromptsAfterLoad(options, presence, instances, prompted))
        return 1;

    if(!ValidateAssetKind(options.assetKind, error)){
        NWB_CERR << error << "\n";
        return 1;
    }

    Vec4 defaultColor;
    if(!ParseColorText(options.defaultColorText, defaultColor)){
        NWB_CERR << "--default-color must contain four finite numbers, for example 1,1,1,1.\n";
        return 1;
    }

    UtilityVector<usize> selection;
    if(!SelectMeshInstances(instances, options.meshSelector, selection, error)){
        NWB_CERR << "Invalid mesh selector: " << error << "\n";
        return 1;
    }

    const Path outputPath = PathFromUtf8(options.outputPath);
    if(outputPath.empty()){
        NWB_CERR << "Output path is empty.\n";
        return 1;
    }
    if(!__hidden_command_line::ValidateOutputOverwrite(outputPath, options, prompted))
        return 1;

    UtilityVector<GeometryVertex> vertices;
    UtilityVector<u32> indices;
    bool sawVertexColors = false;
    bool sawVertexUvs = false;
    if(!BuildGeometry(instances, selection, options, defaultColor, vertices, indices, sawVertexColors, sawVertexUvs, error)){
        NWB_CERR << "Failed to build geometry: " << error << "\n";
        return 1;
    }

    AString resolvedIndexType;
    if(!WriteNwbGeometry(outputPath, vertices, indices, options.indexType, options.assetKind, resolvedIndexType, error)){
        NWB_CERR << "Failed to write NWB geometry: " << error << "\n";
        return 1;
    }

    NWB_COUT
        << "Wrote " << PathToUtf8(outputPath) << "\n"
        << "  vertices: " << vertices.size() << "\n"
        << "  triangles: " << (indices.size() / 3u) << "\n"
        << "  geometry_type: " << options.assetKind << "\n"
        << "  index_type: " << resolvedIndexType << "\n"
        << "  vertex colors: " << (sawVertexColors ? "imported" : "default") << "\n";
    if(IsDeformableGeometryKind(options.assetKind))
        NWB_COUT << "  uv0: " << (sawVertexUvs ? "imported" : "default") << "\n";

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

