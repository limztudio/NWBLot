// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"

#include <global/terminal_entry.h>
#include <logger/client/logger.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    AInteropString sourceArgument;
    AInteropString outputArgument;
    AInteropString rendererArgument = "bitmap";
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
    return
        InvokeTerminalEntry<CLI::ParseError>(
            [&](){
                app.parse(argc, argv);
                if(rendererArgument != "bitmap" && rendererArgument != "outline"){
                    NWB_LOGGER_ERROR(GLB_TEXT("font_builder: unknown renderer; choose bitmap or outline"));
                    return 1;
                }
                options.outline = rendererArgument == "outline";
                options.source = Path(UtilityDetail::Arena(), AString(sourceArgument.data(), sourceArgument.size()));
                options.output = Path(UtilityDetail::Arena(), AString(outputArgument.data(), outputArgument.size()));
                if(!ValidateOptions(options))
                    return 1;
                ErrorCode error;
                const bool exists = FileExists(options.output, error);
                if(error || (exists && !options.overwrite)){
                    NWB_LOGGER_ERROR(GLB_TEXT("font_builder: cannot replace output metadata without --overwrite"));
                    return 1;
                }
                Core::Alloc::ScratchArena scratch(Name("utilities/font_builder/bake"));
                Impl::FontAtlasPayload payload(UtilityDetail::Arena());
                if(!Bake(options, payload, scratch) || !WriteOutputs(options, payload))
                    return 1;
                NWB_LOGGER_INFO(GLB_TEXT("font_builder: published font asset bunch '{}' with {} glyphs / {} compact groups")
                    , PathToString<tchar>(options.output)
                    , payload.glyphs.size()
                    , payload.groups.size()
                );
                return 0;
            },
            [&](const CLI::ParseError& error){ return app.exit(error, GLB_COUT, GLB_CERR); },
            [](){ return -1; }
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

