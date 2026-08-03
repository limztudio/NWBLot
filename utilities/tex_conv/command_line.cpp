// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <core/common/log.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int Run(const int argc, char** argv){
    AInteropString inputArgument;
    AInteropString outputArgument;
    std::vector<AInteropString> cubeArguments;
    std::vector<AInteropString> volumeArguments;
    bool force = false;
    bool linear = false;

    CLI::App app{ "Convert LDR images into an NWB UASTC 2D, cube, or volume texture asset." };
    app.add_option("input", inputArgument, "2D input image (.png, .jpg, .jpeg, .jfif, .tga, or .qoi)");
    app.add_option("--cube", cubeArguments, "Six cubemap faces: +X -X +Y -Y +Z -Z")->expected(static_cast<int>(s_TextureCubeFaceCount));
    app.add_option("--volume", volumeArguments, "Ordered volume Z slices: z0 z1 ... zN")->expected(1, -1);
    app.add_option("-o,--output", outputArgument, "Output base name or .nwb filename");
    app.add_flag("--linear", linear, "Treat input as linear data instead of sRGB color");
    app.add_flag("--force", force, "Replace existing .nwb and .tex output files");

    try{
        app.parse(argc, argv);
    }
    catch(const CLI::ParseError& error){
        return app.exit(error, NWB_COUT, NWB_CERR);
    }

    try{
        const AString outputPathText(outputArgument.data(), outputArgument.size());
        TextureDimension::Enum dimension = TextureDimension::Texture2D;
        Vector<Path> inputPaths;

        const u32 modeCount = static_cast<u32>(!inputArgument.empty())
            + static_cast<u32>(!cubeArguments.empty())
            + static_cast<u32>(!volumeArguments.empty())
        ;
        if(modeCount != 1u){
            NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: provide exactly one of a 2D input, --cube, or --volume."));
            return 1;
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

        for(const Path& inputPath : inputPaths){
            ErrorCode errorCode;
            const bool inputIsRegularFile = IsRegularFile(inputPath, errorCode);
            if(errorCode && !IsMissingPathError(errorCode)){
                NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: failed to inspect input '{}': {}")
                    , PathToString<tchar>(inputPath)
                    , StringConvert(errorCode.message())
                );
                return 1;
            }
            if(!inputIsRegularFile){
                NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: input image was not found or is not a regular file: '{}'")
                    , PathToString<tchar>(inputPath)
                );
                return 1;
            }
            if(!IsSupportedInputPath(inputPath)){
                NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: unsupported input format; accepted: PNG, JPEG/JFIF, TGA, and QOI."));
                return 1;
            }
        }

        OutputPaths outputPaths;
        if(!ResolveOutputPaths(inputPaths.front(), outputPathText, outputPaths) || !ValidateOutputPaths(outputPaths, force))
            return 1;

        TexturePayload payload;
        if(!EncodeTexture(inputPaths, dimension, !linear, payload))
            return 1;
        if(!WriteOutputs(outputPaths, payload, !linear, force))
            return 1;

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
        report
            << ", " << payload.mips.size() << " mips, " << payload.bytes.size() << " bytes\n"
        ;
        NWB_LOGGER_ESSENTIAL_INFO(StringConvert(report.str()));
        return 0;
    }
    catch(const GeneralException& exception){
        NWB_LOGGER_WARNING(NWB_TEXT("tex_conv: unexpected conversion exception: {}"), StringConvert(exception.what()));
        return 1;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TEX_CONV_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

