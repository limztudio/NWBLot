// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>

#include <CLI.hpp>
#include <global/terminal_entry.h>


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
    app.add_option(TexConvCliDetail::s_InputOptionName.data(), inputArgument, "2D input image (.png, .jpg, .jpeg, .jfif, .tga, .qoi, .exr, or .hdr)");
    app.add_option(TexConvCliDetail::s_CubeOptionName.data(), cubeArguments, "Six cubemap faces: +X -X +Y -Y +Z -Z")->expected(static_cast<int>(TextureFormat::s_TextureCubeFaceCount));
    app.add_option(TexConvCliDetail::s_VolumeOptionName.data(), volumeArguments, "Ordered volume Z slices: z0 z1 ... zN")->expected(TexConvCliDetail::s_MinVolumeSliceCount, TexConvCliDetail::s_UnboundedOptionCount);
    app.add_option(TexConvCliDetail::s_OutputOptionName.data(), outputArgument, "Output base name or .nwb filename");
    CLI::Option* alphaOption = app.add_option(TexConvCliDetail::s_AlphaOptionName.data(), alphaArgument, "Alpha source: mask image red channel, white, or black");
    app.add_flag(TexConvCliDetail::s_LinearFlagName.data(), linear, "Treat LDR input as linear data instead of sRGB color (HDR input is always linear)");
    app.add_flag(TexConvCliDetail::s_ForceFlagName.data(), force, "Replace existing .nwb and .tex output files");

    return ::InvokeTerminalEntry<CLI::ParseError>([&](){
        app.parse(argc, argv);

        {
            const AStringView outputPathText(outputArgument);
            TextureDimension::Enum dimension = TextureDimension::Texture2D;
            Vector<Path> inputPaths;
            AlphaSource alphaSource;

            const u32 modeCount = static_cast<u32>(!inputArgument.empty())
                + static_cast<u32>(!cubeArguments.empty())
                + static_cast<u32>(!volumeArguments.empty())
            ;
            if(modeCount != TexConvCliDetail::s_SingleTextureInputModeCount){
                NWB_LOGGER_ERROR(GLB_TEXT("tex_conv: provide exactly one of a 2D input, --cube, or --volume."));
                return TexConvCliDetail::s_TexConvExitFailure;
            }

            if(!inputArgument.empty()){
                const AStringView inputPathText(inputArgument);
                inputPaths.push_back(Path(UtilityDetail::Arena(), inputPathText));
            }
            else if(!cubeArguments.empty()){
                dimension = TextureDimension::TextureCube;
                inputPaths.reserve(cubeArguments.size());
                for(const AInteropString& argument : cubeArguments){
                    const AStringView inputPathText(argument);
                    inputPaths.push_back(Path(UtilityDetail::Arena(), inputPathText));
                }
            }
            else{
                dimension = TextureDimension::Texture3D;
                inputPaths.reserve(volumeArguments.size());
                for(const AInteropString& argument : volumeArguments){
                    const AStringView inputPathText(argument);
                    inputPaths.push_back(Path(UtilityDetail::Arena(), inputPathText));
                }
            }

            if(alphaOption->count() > TexConvCliDetail::s_AlphaOptionPresentCount){
                const AStringView alphaText(alphaArgument);
                const AString alphaKeyword = ToAsciiLowerCopy(AString(alphaText));
                if(alphaKeyword == s_AlphaWhiteKeyword){
                    alphaSource.mode = AlphaSourceMode::Constant;
                    alphaSource.constant = s_AlphaWhiteConstant;
                }
                else if(alphaKeyword == s_AlphaBlackKeyword){
                    alphaSource.mode = AlphaSourceMode::Constant;
                    alphaSource.constant = s_AlphaBlackConstant;
                }
                else if(alphaText.empty()){
                    NWB_LOGGER_ERROR(GLB_TEXT("tex_conv: --alpha expects an image path, white, or black."));
                    return TexConvCliDetail::s_TexConvExitFailure;
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
                    NWB_LOGGER_ERROR(GLB_TEXT("tex_conv: failed to inspect input '{}': {}")
                        , PathToString<tchar>(inputPath)
                        , StringConvert(errorCode.message())
                    );
                    return TexConvCliDetail::s_TexConvExitFailure;
                }
                if(!inputIsRegularFile){
                    NWB_LOGGER_ERROR(GLB_TEXT("tex_conv: input image was not found or is not a regular file: '{}'")
                        , PathToString<tchar>(inputPath)
                    );
                    return TexConvCliDetail::s_TexConvExitFailure;
                }
                if(!IsSupportedInputPath(inputPath)){
                    NWB_LOGGER_ERROR(GLB_TEXT(
                        "tex_conv: unsupported input format; accepted: PNG, JPEG/JFIF, TGA, QOI, OpenEXR, "
                        "and Radiance HDR."
                    ));
                    return TexConvCliDetail::s_TexConvExitFailure;
                }
            }

            if(alphaSource.mode == AlphaSourceMode::Image){
                ErrorCode errorCode;
                const bool alphaIsRegularFile = IsRegularFile(alphaSource.path, errorCode);
                if(errorCode && !IsMissingPathError(errorCode)){
                    NWB_LOGGER_ERROR(GLB_TEXT("tex_conv: failed to inspect alpha image '{}': {}")
                        , PathToString<tchar>(alphaSource.path)
                        , StringConvert(errorCode.message())
                    );
                    return TexConvCliDetail::s_TexConvExitFailure;
                }
                if(!alphaIsRegularFile){
                    NWB_LOGGER_ERROR(GLB_TEXT("tex_conv: alpha image was not found or is not a regular file: '{}'")
                        , PathToString<tchar>(alphaSource.path)
                    );
                    return TexConvCliDetail::s_TexConvExitFailure;
                }
                if(!IsSupportedInputPath(alphaSource.path)){
                    NWB_LOGGER_ERROR(GLB_TEXT(
                        "tex_conv: unsupported alpha image format; accepted: PNG, JPEG/JFIF, TGA, QOI, OpenEXR, "
                        "and Radiance HDR."
                    ));
                    return TexConvCliDetail::s_TexConvExitFailure;
                }
            }

            OutputPaths outputPaths;
            if(!ResolveOutputPaths(inputPaths.front(), outputPathText, outputPaths) || !ValidateOutputPaths(outputPaths, force))
                return TexConvCliDetail::s_TexConvExitFailure;

            TexturePayload payload;
            if(!EncodeTexture(inputPaths, dimension, !linear, alphaSource, payload))
                return TexConvCliDetail::s_TexConvExitFailure;
            if(!WriteOutputs(outputPaths, payload, force))
                return TexConvCliDetail::s_TexConvExitFailure;

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
            return TexConvCliDetail::s_TexConvExitSuccess;
        }
    }, [&](const CLI::ParseError& error){ return app.exit(error, GLB_COUT, GLB_CERR); }, [](){ return TexConvCliDetail::s_TexConvExitFatal; });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

