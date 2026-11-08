// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "import_prompts.h"

#include <core/common/log.h>

#include <global/allocation_size.h>
#include <global/command_line.h>
#include <global/terminal_entry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


Expected<AString> ImportPrompts::promptString(const AStringView label, const AStringView defaultValue, b8& inOutPrompted){
    inOutPrompted = true;
    NWB_COUT << label;
    if(!defaultValue.empty())
        NWB_COUT << " [" << defaultValue << "]";
    NWB_COUT << ": ";

    AString line;
    if(ReadTextLine(NWB_CIN, line))
        line = TrimCopy(Move(line));
    if(line.empty())
        line = defaultValue;
    if(line.empty())
        return MakeUnexpected(Failure{});
    return line;
}

b8 ImportPrompts::promptBool(const AStringView label, const b8 defaultValue, Core::Alloc::ScratchArena& scratchArena, b8& inOutPrompted){
    inOutPrompted = true;
    ScratchString line(scratchArena);
    for(;;){
        NWB_COUT << label << (defaultValue ? " [Y/n]: " : " [y/N]: ");

        line.clear();
        if(!ReadTextLine(NWB_CIN, line))
            return defaultValue;

        line = NormalizeOptionText(Move(line));
        if(line.empty())
            return defaultValue;
        const auto value = ParseConfirmText(AStringView(line.data(), line.size()));
        if(value)
            return *value;

        NWB_COUT << "Please answer y or n.\n";
    }
}

f64 ImportPrompts::promptDouble(const AStringView label, const f64 defaultValue, Core::Alloc::ScratchArena& scratchArena, b8& inOutPrompted){
    inOutPrompted = true;
    ScratchString line(scratchArena);
    for(;;){
        NWB_COUT << label << " [" << defaultValue << "]: ";

        line.clear();
        if(!ReadTextLine(NWB_CIN, line))
            return defaultValue;

        line = TrimCopy(Move(line));
        if(line.empty())
            return defaultValue;

        const auto parsed = ParseF64FromChars(AStringView(line.data(), line.size()));
        if(parsed && IsFinite(*parsed) && *parsed > 0.0)
            return *parsed;

        NWB_COUT << "Please enter a positive finite number.\n";
    }
}

b8 ImportPrompts::validateOutputOverwrite(const ScratchPath& outputPath, const ImportOptions& options, Core::Alloc::ScratchArena& scratchArena, b8& inOutPrompted){
    const auto exists = FileExistsNoFollow(outputPath);
    if(!exists){
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to query output path: {}"), ConvertString(exists.error().message()));
        return false;
    }
    if(!*exists)
        return true;
    if(options.forceOverwrite)
        return true;
    if(options.acceptDefaults){
        NWB_LOGGER_WARNING(NWB_TEXT("Output already exists. Pass --force to overwrite: {}"), PathToString<tchar>(outputPath));
        return false;
    }

    return promptBool("Output already exists. Overwrite it?", false, scratchArena, inOutPrompted);
}

b8 ImportPrompts::configurePromptsBeforeLoad(const OptionPresence& presence, Core::Alloc::ScratchArena& scratchArena, ImportOptions& inOutOptions, b8& inOutPrompted){
    if(inOutOptions.inputPath.empty()){
        if(inOutOptions.acceptDefaults){
            NWB_LOGGER_WARNING(NWB_TEXT("Input FBX or NWB path is required."));
            return false;
        }

        auto input = promptString("Input FBX or NWB path", {}, inOutPrompted);
        if(!input){
            NWB_LOGGER_WARNING(NWB_TEXT("Input FBX or NWB path is required."));
            return false;
        }
        inOutOptions.inputPath = Move(*input);
    }
    inOutOptions.inputPath = UnquoteMatchingAsciiQuotes(Move(inOutOptions.inputPath));

    if(!presence.preserveSpace && !inOutOptions.acceptDefaults && !inOutOptions.listMeshes){
        const b8 convertSpace = promptBool("Convert axes/units to NWB space (+X right, +Y up, +Z forward, 1 unit = 1 meter)?", true, scratchArena, inOutPrompted);
        inOutOptions.preserveSpace = !convertSpace;
    }

    return true;
}

b8 ImportPrompts::configurePromptsAfterLoad(const OptionPresence& presence, const Span<const MeshInstance> visibleInstances, Core::Alloc::ScratchArena& scratchArena, ImportOptions& inOutOptions, b8& inOutPrompted){
    if(!presence.assetType && !inOutOptions.acceptDefaults){
        const ScratchString choices = OutputAssetTypeOptionsText(scratchArena);
        ScratchString prompt("Asset type (", scratchArena);
        prompt.reserve(AddSize(AddSize(prompt.size(), choices.size()), 1u));
        prompt += choices;
        prompt += ")";
        auto assetType = promptString(prompt, inOutOptions.assetType, inOutPrompted);
        if(!assetType)
            return false;
        inOutOptions.assetType = Move(*assetType);
    }

    if(!presence.mesh && !inOutOptions.acceptDefaults){
        PrintMeshInstances(visibleInstances, scratchArena);
        auto selector = promptString("Mesh selector (all, first, index, node name, or mesh name)", inOutOptions.meshSelector, inOutPrompted);
        if(!selector)
            return false;
        inOutOptions.meshSelector = Move(*selector);
    }

    if(!presence.output && !inOutOptions.acceptDefaults){
        const auto defaultOutput = PathToGenericString(scratchArena, DefaultOutputPath(inOutOptions.inputPath, scratchArena));
        auto output = promptString(s_OutputPathPromptLabel, defaultOutput, inOutPrompted);
        if(!output)
            return false;
        inOutOptions.outputPath = Move(*output);
    }
    if(inOutOptions.outputPath.empty())
        inOutOptions.outputPath = PathToGenericString(scratchArena, DefaultOutputPath(inOutOptions.inputPath, scratchArena));
    inOutOptions.outputPath = UnquoteMatchingAsciiQuotes(Move(inOutOptions.outputPath));

    if(!presence.normalMode && !inOutOptions.acceptDefaults){
        const ScratchString choices = NormalModeOptionsText(scratchArena);
        ScratchString prompt("Normal mode (", scratchArena);
        prompt.reserve(AddSize(AddSize(prompt.size(), choices.size()), 1u));
        prompt += choices;
        prompt += ")";
        auto normalMode = promptString(prompt, inOutOptions.normalMode, inOutPrompted);
        if(!normalMode)
            return false;
        inOutOptions.normalMode = Move(*normalMode);
    }

    if(!presence.scale && !inOutOptions.acceptDefaults)
        inOutOptions.scale = promptDouble("Additional uniform scale", inOutOptions.scale, scratchArena, inOutPrompted);

    if(!presence.local && !inOutOptions.acceptDefaults)
        inOutOptions.bakeTransforms = promptBool("Bake node transforms into the mesh?", inOutOptions.bakeTransforms, scratchArena, inOutPrompted);

    if(!presence.ignoreColors && !inOutOptions.acceptDefaults)
        inOutOptions.importColors = promptBool("Import FBX vertex colors when present?", inOutOptions.importColors, scratchArena, inOutPrompted);

    if(!presence.defaultColor && !inOutOptions.acceptDefaults){
        auto colorText = promptString("Default RGBA color for vertices without FBX color", inOutOptions.defaultColorText, inOutPrompted);
        if(!colorText)
            return false;
        inOutOptions.defaultColorText = Move(*colorText);
    }

    if(!presence.flipWinding && !inOutOptions.acceptDefaults)
        inOutOptions.flipWinding = promptBool("Flip triangle winding?", inOutOptions.flipWinding, scratchArena, inOutPrompted);

    return true;
}


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
