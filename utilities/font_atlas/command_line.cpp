// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"

#include <global/terminal_entry.h>
#include <logger/client/logger.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_ATLAS_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    AInteropString sourceArgument;
    AInteropString outputArgument;
    AInteropString fontArgument;
    AInteropString rendererArgument = "bitmap";
    BakeOptions options(UtilityDetail::Arena());
    CLI::App app{ "Bake every font glyph into four scalar SDF planes packed as linear RGBA in one NWB atlas file." };
    app.add_option("--font", sourceArgument, "Static source .ttf/.otf font")->required();
    app.add_option("--font-asset", fontArgument, "Typed source font virtual asset identity")->required();
    app.add_option("-o,--output", outputArgument, "Output self-contained .nwb atlas file")->required();
    app.add_option("--ppem", options.ppem, "Bake pixels per em (16..256, default64)");
    app.add_option("--spread", options.spread, "SDF distance range (2..32, default8)");
    app.add_option("--extent", options.extent, "Square page extent (32..2048, default1024)");
    app.add_option("--max-groups", options.maxGroups, "Maximum RGBA groups (1..8, default8)");
    app.add_option("--renderer", rendererArgument, "bitmap (default) or outline; errors never switch algorithms");
    app.add_flag("--overwrite", options.overwrite, "Atomically replace an existing atlas file");
    return
        InvokeTerminalEntry<CLI::ParseError>(
            [&](){
                app.parse(argc, argv);
                if(rendererArgument != "bitmap" && rendererArgument != "outline"){
                    NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: unknown renderer; choose bitmap or outline"));
                    return 1;
                }
                options.outline = rendererArgument == "outline";
                options.source = Path(UtilityDetail::Arena(), AString(sourceArgument.data(), sourceArgument.size()));
                options.output = Path(UtilityDetail::Arena(), AString(outputArgument.data(), outputArgument.size()));
                options.fontAsset.assign(fontArgument.data(), fontArgument.size());
                if(!ValidateOptions(options))
                    return 1;
                ErrorCode error;
                const bool exists = FileExists(options.output, error);
                if(error || (exists && !options.overwrite)){
                    NWB_LOGGER_ERROR(NWB_TEXT("font_atlas: cannot replace output atlas without --overwrite"));
                    return 1;
                }
                Core::Alloc::ScratchArena scratch(Name("utilities/font_atlas/bake"));
                Impl::FontAtlasPayload payload(UtilityDetail::Arena());
                if(!Bake(options, payload, scratch) || !WriteOutputs(options, payload))
                    return 1;
                NWB_LOGGER_INFO(NWB_TEXT("font_atlas: published '{}' with {} glyphs / {} RGBA groups")
                    , PathToString<tchar>(options.output)
                    , payload.glyphs.size()
                    , payload.groups.size()
                );
                return 0;
            },
            [&](const CLI::ParseError& error){ return app.exit(error, NWB_COUT, NWB_CERR); },
            [](){ return -1; }
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_ATLAS_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

