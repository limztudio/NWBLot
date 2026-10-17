// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>

#include <global/terminal_entry.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TexConvCliDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_InputOptionName = "input";
inline constexpr StringView s_CubeOptionName = "--cube";
inline constexpr StringView s_VolumeOptionName = "--volume";
inline constexpr StringView s_OutputOptionName = "-o,--output";
inline constexpr StringView s_AlphaOptionName = "--alpha";
inline constexpr StringView s_LinearFlagName = "--linear";
inline constexpr StringView s_ForceFlagName = "--force";
inline constexpr int s_MinVolumeSliceCount = 1;
inline constexpr int s_UnboundedOptionCount = -1;
inline constexpr u32 s_SingleTextureInputModeCount = 1u;
inline constexpr int s_AlphaOptionPresentCount = 0;
inline constexpr int s_TexConvExitSuccess = 0;
inline constexpr int s_TexConvExitFailure = 1;
inline constexpr int s_TexConvExitFatal = -1;
inline constexpr usize s_CompletionReportReserveSlack = 128u;
inline constexpr Name s_ConversionScratchArena("utilities/tex_conv/conversion");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<bool AlphaMask>
[[nodiscard]] bool ValidateInputImage(const ScratchPath& path){
    const auto isRegularFile = IsRegularFile(path);
    if(!isRegularFile && !IsMissingPathError(isRegularFile.error())){
        if constexpr(AlphaMask)
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to inspect alpha image '{}': {}"), PathToString<tchar>(path), StringConvert(isRegularFile.error().message()));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to inspect input '{}': {}"), PathToString<tchar>(path), StringConvert(isRegularFile.error().message()));
        return false;
    }
    if(!isRegularFile || !*isRegularFile){
        if constexpr(AlphaMask)
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha image was not found or is not a regular file: '{}'"), PathToString<tchar>(path));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: input image was not found or is not a regular file: '{}'"), PathToString<tchar>(path));
        return false;
    }
    if(!IsSupportedInputPath(path)){
        if constexpr(AlphaMask)
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported alpha image format; accepted: PNG, JPEG/JFIF, TGA, QOI, OpenEXR, " "and Radiance HDR."));
        else
            NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: unsupported input format; accepted: PNG, JPEG/JFIF, TGA, QOI, OpenEXR, " "and Radiance HDR."));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    AInteropString inputArgument;
    AInteropString outputArgument;
    AInteropString alphaArgument;
    InteropVector<AInteropString> cubeArguments;
    InteropVector<AInteropString> volumeArguments;
    bool force = false;
    bool linear = false;

    CLI::App app{ "Convert LDR or HDR images into prepared linear UASTC texture assets." };
    app.add_option(TexConvCliDetail::s_InputOptionName.data(), inputArgument, "2D input image (.png, .jpg, .jpeg, .jfif, .tga, .qoi, .exr, or .hdr)");
    app.add_option(TexConvCliDetail::s_CubeOptionName.data(), cubeArguments, "Six cubemap faces: +X -X +Y -Y +Z -Z")->expected(static_cast<int>(TextureFormat::s_TextureCubeFaceCount));
    app.add_option(TexConvCliDetail::s_VolumeOptionName.data(), volumeArguments, "Ordered volume Z slices: z0 z1 ... zN")->expected(TexConvCliDetail::s_MinVolumeSliceCount, TexConvCliDetail::s_UnboundedOptionCount);
    app.add_option(TexConvCliDetail::s_OutputOptionName.data(), outputArgument, "Output base name or .nwb filename");
    CLI::Option* alphaOption = app.add_option(TexConvCliDetail::s_AlphaOptionName.data(), alphaArgument, "Alpha source: mask image red channel, white, or black");
    app.add_flag(TexConvCliDetail::s_LinearFlagName.data(), linear, "Treat LDR input as linear data instead of sRGB color (HDR input is always linear)");
    app.add_flag(TexConvCliDetail::s_ForceFlagName.data(), force, "Replace existing .nwb and .tex output files");

    return ::InvokeTerminalEntry<CLI::ParseError>([&](){
        app.parse(argc, argv);
        Core::Alloc::ScratchArena scratchArena(TexConvCliDetail::s_ConversionScratchArena);

        {
            const AStringView outputPathText(outputArgument);
            TextureDimension::Enum dimension = TextureDimension::Texture2D;
            ::Vector<ScratchPath, Core::Alloc::ScratchArena> inputPaths(scratchArena);
            AlphaSource alphaSource(scratchArena);

            const u32 modeCount = static_cast<u32>(!inputArgument.empty()) + static_cast<u32>(!cubeArguments.empty()) + static_cast<u32>(!volumeArguments.empty());
            if(modeCount != TexConvCliDetail::s_SingleTextureInputModeCount){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: provide exactly one of a 2D input, --cube, or --volume."));
                return TexConvCliDetail::s_TexConvExitFailure;
            }

            if(!inputArgument.empty()){
                const AStringView inputPathText(inputArgument);
                inputPaths.reserve(1u);
                inputPaths.emplace_back(scratchArena, inputPathText);
            }
            else if(!cubeArguments.empty()){
                dimension = TextureDimension::TextureCube;
                inputPaths.reserve(cubeArguments.size());
                for(const AInteropString& argument : cubeArguments){
                    const AStringView inputPathText(argument);
                    inputPaths.emplace_back(scratchArena, inputPathText);
                }
            }
            else{
                dimension = TextureDimension::Texture3D;
                inputPaths.reserve(volumeArguments.size());
                for(const AInteropString& argument : volumeArguments){
                    const AStringView inputPathText(argument);
                    inputPaths.emplace_back(scratchArena, inputPathText);
                }
            }

            if(alphaOption->count() > TexConvCliDetail::s_AlphaOptionPresentCount){
                const AStringView alphaText(alphaArgument);
                if(EqualsAsciiIgnoreCase(alphaText, s_AlphaWhiteKeyword)){
                    alphaSource.mode = AlphaSourceMode::Constant;
                    alphaSource.constant = s_AlphaWhiteConstant;
                }
                else if(EqualsAsciiIgnoreCase(alphaText, s_AlphaBlackKeyword)){
                    alphaSource.mode = AlphaSourceMode::Constant;
                    alphaSource.constant = s_AlphaBlackConstant;
                }
                else if(alphaText.empty()){
                    NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: --alpha expects an image path, white, or black."));
                    return TexConvCliDetail::s_TexConvExitFailure;
                }
                else{
                    alphaSource.mode = AlphaSourceMode::Image;
                    alphaSource.path = alphaText;
                }
            }

            for(const ScratchPath& inputPath : inputPaths){
                if(!TexConvCliDetail::ValidateInputImage<false>(inputPath))
                    return TexConvCliDetail::s_TexConvExitFailure;
            }
            if(alphaSource.mode == AlphaSourceMode::Image && !TexConvCliDetail::ValidateInputImage<true>(alphaSource.path))
                return TexConvCliDetail::s_TexConvExitFailure;

            const auto outputPaths = ResolveOutputPaths(inputPaths.front(), outputPathText, scratchArena);
            if(!outputPaths || !ValidateOutputPaths(*outputPaths, force, scratchArena))
                return TexConvCliDetail::s_TexConvExitFailure;

            const auto payload = PrepareTexture(inputPaths, dimension, !linear, alphaSource, scratchArena);
            if(!payload)
                return TexConvCliDetail::s_TexConvExitFailure;
            if(!WriteOutputs(*outputPaths, *payload, force, scratchArena))
                return TexConvCliDetail::s_TexConvExitFailure;

            const auto metadataPathText = PathToGenericString(scratchArena, outputPaths->metadata);
            const auto dataPathText = PathToGenericString(scratchArena, outputPaths->data);
            ScratchString report(scratchArena);
            report.reserve(AddSize(AddSize(metadataPathText.size(), dataPathText.size()), TexConvCliDetail::s_CompletionReportReserveSlack));
            StringAppendFormat(report, "Wrote {}\nWrote {}\n  {}x{}", metadataPathText, dataPathText, payload->width, payload->height);
            if(payload->dimension == TextureDimension::TextureCube)
                report += " cube";
            else if(payload->dimension == TextureDimension::Texture3D)
                StringAppendFormat(report, "x{}", payload->depth);
            StringAppendFormat(report, ", {} linear UASTC mips, {} raw bytes\n", payload->mipCount, payload->bytes.size());
            NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report));
            return TexConvCliDetail::s_TexConvExitSuccess;
        }
    }, [&](const CLI::ParseError& error){ return app.exit(error, NWB_COUT, NWB_CERR); }, []()noexcept{ return TexConvCliDetail::s_TexConvExitFatal; }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

