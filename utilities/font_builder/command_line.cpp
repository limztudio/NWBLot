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
    AInteropString rendererArgument(Impl::s_FontAtlasRasterModeBitmap.data(), Impl::s_FontAtlasRasterModeBitmap.size());
    BakeOptions options(UtilityDetail::Arena());
    CLI::App app{ AInteropString(s_FontBuilderAppDescription.data(), s_FontBuilderAppDescription.size()) };
    app.add_option(AInteropString(s_FontOptionFlag.data(), s_FontOptionFlag.size()), sourceArgument, "Static source .ttf/.otf or prepared .font")->required();
    app.add_option(AInteropString(s_OutputOptionFlag.data(), s_OutputOptionFlag.size()), outputArgument, "Output .nwb asset bunch; paired binary .font shares its stem")->required();
    app.add_option(AInteropString(s_PpemOptionFlag.data(), s_PpemOptionFlag.size()), options.ppem, "Bake pixels per em (16..256, default64)");
    app.add_option(AInteropString(s_SpreadOptionFlag.data(), s_SpreadOptionFlag.size()), options.spread, "SDF distance range (2..32, default8)");
    app.add_option(AInteropString(s_ExtentOptionFlag.data(), s_ExtentOptionFlag.size()), options.extent, "Maximum square packing extent; final groups are cropped (32..2048, default1024)");
    app.add_option(AInteropString(s_MaxGroupsOptionFlag.data(), s_MaxGroupsOptionFlag.size()), options.maxGroups, "Maximum four-plane packing groups (1..8, default8)");
    app.add_option(AInteropString(s_RendererOptionFlag.data(), s_RendererOptionFlag.size()), rendererArgument, "bitmap (default) or outline; errors never switch algorithms");
    app.add_flag(AInteropString(s_OverwriteOptionFlag.data(), s_OverwriteOptionFlag.size()), options.overwrite, "Replace an existing complete .nwb/.font pair");
    return InvokeTerminalEntry<CLI::ParseError>([&](){
                app.parse(argc, argv);
                if(rendererArgument != Impl::s_FontAtlasRasterModeBitmap && rendererArgument != Impl::s_FontAtlasRasterModeOutline){
                    NWB_LOGGER_ERROR(NWB_TEXT("font_builder: unknown renderer; choose bitmap or outline"));
                    return 1;
                }
                options.outline = rendererArgument == Impl::s_FontAtlasRasterModeOutline;
                options.source = AStringView(sourceArgument.data(), sourceArgument.size());
                options.output = AStringView(outputArgument.data(), outputArgument.size());
                Core::Alloc::ScratchArena scratch(s_FontBuilderBakeArena);
                if(!ValidateOptions(options))
                    return 1;
                const auto exists = FileExists(options.output);
                if(!exists){
                    NWB_LOGGER_ERROR(NWB_TEXT("font_builder: failed to inspect output metadata: {}"), ConvertString(exists.error().message()));
                    return 1;
                }
                if(*exists && !options.overwrite){
                    NWB_LOGGER_ERROR(NWB_TEXT("font_builder: cannot replace output metadata without --overwrite"));
                    return 1;
                }
                Core::Assets::AssetBytes sourceFontBytes(UtilityDetail::Arena());
                auto payload = Bake(options, UtilityDetail::Arena(), scratch, sourceFontBytes);
                if(!payload || !WriteOutputs(options, sourceFontBytes, *payload, scratch))
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

