// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>

#include <CLI.hpp>
#include <core/common/terminal_entry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TexConvCliDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr const char* s_InputOptionName = "input";
inline constexpr const char* s_CubeOptionName = "--cube";
inline constexpr const char* s_VolumeOptionName = "--volume";
inline constexpr const char* s_OutputOptionName = "-o,--output";
inline constexpr const char* s_AlphaOptionName = "--alpha";
inline constexpr const char* s_LinearFlagName = "--linear";
inline constexpr const char* s_ForceFlagName = "--force";
inline constexpr int s_MinVolumeSliceCount = 1;
inline constexpr int s_UnboundedOptionCount = -1;
inline constexpr u32 s_SingleTextureInputModeCount = 1u;
inline constexpr int s_AlphaOptionPresentCount = 0;
inline constexpr int s_TexConvExitSuccess = 0;
inline constexpr int s_TexConvExitFailure = 1;
inline constexpr int s_TexConvExitFatal = -1;


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

    CLI::App app{ "Convert LDR or HDR images into an NWB 2D, cube, or volume texture asset." };
    app.add_option(TexConvCliDetail::s_InputOptionName, inputArgument, "2D input image (.png, .jpg, .jpeg, .jfif, .tga, .qoi, .exr, or .hdr)");
    app.add_option(TexConvCliDetail::s_CubeOptionName, cubeArguments, "Six cubemap faces: +X -X +Y -Y +Z -Z")->expected(static_cast<int>(s_TextureCubeFaceCount));
    app.add_option(TexConvCliDetail::s_VolumeOptionName, volumeArguments, "Ordered volume Z slices: z0 z1 ... zN")->expected(TexConvCliDetail::s_MinVolumeSliceCount, TexConvCliDetail::s_UnboundedOptionCount);
    app.add_option(TexConvCliDetail::s_OutputOptionName, outputArgument, "Output base name or .nwb filename");
    CLI::Option* alphaOption = app.add_option(TexConvCliDetail::s_AlphaOptionName, alphaArgument, "Alpha source: mask image red channel, white, or black");
    app.add_flag(TexConvCliDetail::s_LinearFlagName, linear, "Treat LDR input as linear data instead of sRGB color (HDR input is always linear)");
    app.add_flag(TexConvCliDetail::s_ForceFlagName, force, "Replace existing .nwb and .tex output files");

    return NWB::Core::Common::InvokeTerminalEntry<CLI::ParseError>([&](){
        app.parse(argc, argv);

        {
            const AString outputPathText(outputArgument.data(), outputArgument.size());
            TextureDimension::Enum dimension = TextureDimension::Texture2D;
            Vector<Path> inputPaths;
            AlphaSource alphaSource;

            const u32 modeCount = static_cast<u32>(!inputArgument.empty())
                + static_cast<u32>(!cubeArguments.empty())
                + static_cast<u32>(!volumeArguments.empty())
            ;
            if(modeCount != TexConvCliDetail::s_SingleTextureInputModeCount){
                NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: provide exactly one of a 2D input, --cube, or --volume."));
                return s_TexConvExitFailure;
            }

            if(!inputArgument.empty()){
                const AString inputPathText(inputArgument.data(), inputArgument.size());
                inputPaths.push_back(Path(UtilityDetail::Arena(), inputPathText));
            }
            else if(!cubeArguments.empty()){
                dimension = TextureDimension::TextureCube;
                inputPaths.reserve(cubeArguments.size());
                for(const AInteropString& argument : cubeArguments){
                    const AString inputPathText(argument.data(), argument.size());
                    inputPaths.push_back(Path(UtilityDetail::Arena(), inputPathText));
                }
            }
            else{
                dimension = TextureDimension::Texture3D;
                inputPaths.reserve(volumeArguments.size());
                for(const AInteropString& argument : volumeArguments){
                    const AString inputPathText(argument.data(), argument.size());
                    inputPaths.push_back(Path(UtilityDetail::Arena(), inputPathText));
                }
            }

            if(alphaOption->count() > TexConvCliDetail::s_AlphaOptionPresentCount){
                const AString alphaText(alphaArgument.data(), alphaArgument.size());
                const AString alphaKeyword = ToAsciiLowerCopy(alphaText);
                if(alphaKeyword == s_AlphaWhiteKeyword){
                    alphaSource.mode = AlphaSourceMode::Constant;
                    alphaSource.constant = s_AlphaWhiteConstant;
                }
                else if(alphaKeyword == s_AlphaBlackKeyword){
                    alphaSource.mode = AlphaSourceMode::Constant;
                    alphaSource.constant = s_AlphaBlackConstant;
                }
                else if(alphaText.empty()){
                    NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: --alpha expects an image path, white, or black."));
                    return s_TexConvExitFailure;
                }
                else{
                    alphaSource.mode = AlphaSourceMode::Image;
                    alphaSource.path = Path(UtilityDetail::Arena(), alphaText);
                }
            }

            for(const Path& inputPath : inputPaths){
                ErrorCode errorCode;
                const bool inputIsRegularFile = IsRegularFile(inputPath, errorCode);
                if(errorCode && !IsMissingPathError(errorCode)){
                    NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to inspect input '{}': {}")
                        , PathToString<tchar>(inputPath)
                        , StringConvert(errorCode.message())
                    );
                    return s_TexConvExitFailure;
                }
                if(!inputIsRegularFile){
                    NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: input image was not found or is not a regular file: '{}'")
                        , PathToString<tchar>(inputPath)
                    );
                    return s_TexConvExitFailure;
                }
                if(!IsSupportedInputPath(inputPath)){
                    NWB_LOGGER_ERROR(NWB_TEXT(
                        "tex_conv: unsupported input format; accepted: PNG, JPEG/JFIF, TGA, QOI, OpenEXR, "
                        "and Radiance HDR."
                    ));
                    return s_TexConvExitFailure;
                }
            }

            if(alphaSource.mode == AlphaSourceMode::Image){
                ErrorCode errorCode;
                const bool alphaIsRegularFile = IsRegularFile(alphaSource.path, errorCode);
                if(errorCode && !IsMissingPathError(errorCode)){
                    NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: failed to inspect alpha image '{}': {}")
                        , PathToString<tchar>(alphaSource.path)
                        , StringConvert(errorCode.message())
                    );
                    return s_TexConvExitFailure;
                }
                if(!alphaIsRegularFile){
                    NWB_LOGGER_ERROR(NWB_TEXT("tex_conv: alpha image was not found or is not a regular file: '{}'")
                        , PathToString<tchar>(alphaSource.path)
                    );
                    return s_TexConvExitFailure;
                }
                if(!IsSupportedInputPath(alphaSource.path)){
                    NWB_LOGGER_ERROR(NWB_TEXT(
                        "tex_conv: unsupported alpha image format; accepted: PNG, JPEG/JFIF, TGA, QOI, OpenEXR, "
                        "and Radiance HDR."
                    ));
                    return s_TexConvExitFailure;
                }
            }

            OutputPaths outputPaths;
            if(!ResolveOutputPaths(inputPaths.front(), outputPathText, outputPaths) || !ValidateOutputPaths(outputPaths, force))
                return s_TexConvExitFailure;

            TexturePayload payload;
            if(!EncodeTexture(inputPaths, dimension, !linear, alphaSource, payload))
                return s_TexConvExitFailure;
            if(!WriteOutputs(outputPaths, payload, force))
                return s_TexConvExitFailure;

            AStringStream report;
            report
                << "Wrote " << PathToGenericString<AString>(outputPaths.metadata) << "\n"
                << "Wrote " << PathToGenericString<AString>(outputPaths.data) << "\n"
                << "  " << payload.width << "x" << payload.height
            ;
            if(payload.dimension == TextureDimension::TextureCube)
                report << " cube";
            else if(payload.dimension == TextureDimension::Texture3D)
                report << "x" << payload.depth;
            const usize totalPayloadBytes = payload.bytes.size() + payload.alphaBytes.size();
            report
                << ", " << payload.mips.size() << " mips, " << totalPayloadBytes << " bytes\n"
            ;
            NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report.str()));
            return s_TexConvExitSuccess;
        }
    }, [&](const CLI::ParseError& error){ return app.exit(error, NWB_COUT, NWB_CERR); }, [](){ return s_TexConvExitFatal; });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

