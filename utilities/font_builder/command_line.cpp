// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"

#include <core/common/log.h>

#include <global/terminal_entry.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    AInteropString sourceArgument;
    AInteropString outputArgument;
    AInteropString rendererArgument(s_RasterModeBitmapText.data(), s_RasterModeBitmapText.size());
    BakeOptions options(UtilityDetail::Arena());
    CLI::App app{ "Build readable .nwb font declarations and a paired .font containing shaping bytes and compact SDF images." };
    app.add_option("--font", sourceArgument, "Static source .ttf/.otf or prepared .font")->required();
    app.add_option("-o,--output", outputArgument, "Output .nwb asset bunch; paired binary .font shares its stem")->required();
    app.add_option("--ppem", options.ppem, "Bake pixels per em (16..256, default64)");
    app.add_option("--spread", options.spread, "SDF distance range (2..32, default8)");
    app.add_option("--extent", options.extent, "Maximum square packing extent; final groups are cropped (32..2048, default1024)");
    app.add_option("--max-groups", options.maxGroups, "Maximum four-plane packing groups (1..8, default8)");
    app.add_option("--renderer", rendererArgument, "bitmap (default) or outline; errors never switch algorithms");
    app.add_flag("--overwrite", options.overwrite, "Replace an existing complete .nwb/.font pair");
    return InvokeTerminalEntry<CLI::ParseError>([&](){
                app.parse(argc, argv);
                if(rendererArgument != s_RasterModeBitmapText && rendererArgument != s_RasterModeOutlineText){
                    NWB_LOGGER_ERROR(NWB_TEXT("font_builder: unknown renderer; choose bitmap or outline"));
                    return 1;
                }
                options.outline = rendererArgument == s_RasterModeOutlineText;
                options.source = AStringView(sourceArgument.data(), sourceArgument.size());
                options.output = AStringView(outputArgument.data(), outputArgument.size());
                Core::Alloc::ScratchArena scratch(Name("utilities/font_builder/bake"));
                if(!ValidateOptions(options, scratch))
                    return 1;
                const auto exists = FileExists(options.output);
                if(!exists){
                    NWB_LOGGER_ERROR(NWB_TEXT("font_builder: failed to inspect output metadata: {}"), StringConvert(exists.error().message()));
                    return 1;
                }
                if(*exists && !options.overwrite){
                    NWB_LOGGER_ERROR(NWB_TEXT("font_builder: cannot replace output metadata without --overwrite"));
                    return 1;
                }
                auto payload = Bake(options, UtilityDetail::Arena(), scratch);
                if(!payload || !WriteOutputs(options, *payload, scratch))
                    return 1;
                NWB_LOGGER_INFO(NWB_TEXT("font_builder: published font asset bunch '{}' with {} glyphs / {} compact groups"), PathToString<tchar>(options.output), payload->glyphs.size(), payload->groups.size());
                return 0;
            },
            [&](const CLI::ParseError& error){ return app.exit(error, NWB_COUT, NWB_CERR); },
            []()noexcept{ return -1; }
        );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

