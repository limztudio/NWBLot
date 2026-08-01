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
    bool force = false;
    bool linear = false;

    CLI::App app{ "Convert an LDR image into an NWB UASTC texture asset." };
    app.add_option("input", inputArgument, "Input image (.png, .jpg, .jpeg, .jfif, .tga, or .qoi)")->required();
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
        const AString inputPathText(inputArgument.data(), inputArgument.size());
        const AString outputPathText(outputArgument.data(), outputArgument.size());
        const Path inputPath = PathFromUtf8(inputPathText);
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

        OutputPaths outputPaths;
        if(!ResolveOutputPaths(inputPath, outputPathText, outputPaths) || !ValidateOutputPaths(outputPaths, force))
            return 1;

        TexturePayload payload;
        if(!EncodeTexture(inputPath, !linear, payload))
            return 1;
        if(!WriteOutputs(outputPaths, payload, !linear, force))
            return 1;

        AStringStream report;
        report
            << "Wrote " << PathToUtf8(outputPaths.metadata) << "\n"
            << "Wrote " << PathToUtf8(outputPaths.data) << "\n"
            << "  " << payload.width << "x" << payload.height
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

