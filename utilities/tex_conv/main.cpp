#include <core/common/application_entry.h>
#include <core/common/module.h>

#include <basisu_comp.h>
#include <basisu_enc.h>
#include <CLI.hpp>

#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace __hidden_tex_conv{

constexpr std::uint32_t s_UastcBlockWidth = 4u;
constexpr std::uint32_t s_UastcBlockHeight = 4u;
constexpr std::uint32_t s_UastcBytesPerBlock = 16u;
constexpr const char* s_UastcSpecificationRevision = "b624c07ad3c659e7b0f0badcb36e9a6b8820a99d";

struct MipLevel final{
    std::uint32_t level = 0u;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t blocksX = 0u;
    std::uint32_t blocksY = 0u;
    std::uint64_t offsetBytes = 0u;
    std::uint64_t sizeBytes = 0u;
};

struct TexturePayload final{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    bool hasAlpha = false;
    std::vector<MipLevel> mips;
    std::vector<std::uint8_t> bytes;
};

struct OutputPaths final{
    std::filesystem::path metadata;
    std::filesystem::path data;
    std::filesystem::path metadataTemporary;
    std::filesystem::path dataTemporary;
};

class BasisLibrary final{
public:
    BasisLibrary() = default;
    BasisLibrary(const BasisLibrary&) = delete;
    BasisLibrary& operator=(const BasisLibrary&) = delete;

    ~BasisLibrary(){
        if(m_initialized)
            basisu::basisu_encoder_deinit();
    }

    [[nodiscard]] bool initialize(std::string& error){
        m_initialized = basisu::basisu_encoder_init(false);
        if(!m_initialized){
            error = "Basis Universal encoder initialization failed.";
            return false;
        }
        return true;
    }

private:
    bool m_initialized = false;
};

[[nodiscard]] static std::string ToUtf8(const std::filesystem::path& path){
    const auto encoded = path.u8string();
    std::string result;
    result.reserve(encoded.size());
    for(const auto character : encoded)
        result.push_back(static_cast<char>(character));
    return result;
}

[[nodiscard]] static std::filesystem::path PathFromUtf8(const std::string& text){
#if defined(__cpp_char8_t)
    std::u8string encoded;
    encoded.reserve(text.size());
    for(const unsigned char character : text)
        encoded.push_back(static_cast<char8_t>(character));
    return std::filesystem::path(encoded);
#else
    return std::filesystem::path(text);
#endif
}

[[nodiscard]] static std::string ToLowerAscii(std::string value){
    for(char& character : value)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

[[nodiscard]] static bool IsSupportedInputPath(const std::filesystem::path& path){
    const std::string extension = ToLowerAscii(path.extension().string());
    return extension == ".png"
        || extension == ".jpg"
        || extension == ".jpeg"
        || extension == ".jfif"
        || extension == ".tga"
        || extension == ".qoi";
}

[[nodiscard]] static bool ResolveOutputPaths(
    const std::filesystem::path& inputPath,
    const std::string& outputArgument,
    OutputPaths& outputPaths,
    std::string& error
){
    std::filesystem::path outputBase;
    if(outputArgument.empty()){
        outputBase = inputPath;
        outputBase.replace_extension();

        outputPaths.metadata = outputBase;
        outputPaths.metadata += ".nwb";
        outputPaths.data = outputBase;
        outputPaths.data += ".tex";
    }
    else{
        outputBase = PathFromUtf8(outputArgument);
        const std::string extension = ToLowerAscii(outputBase.extension().string());
        if(extension.empty()){
            outputPaths.metadata = outputBase;
            outputPaths.metadata += ".nwb";
            outputPaths.data = outputBase;
            outputPaths.data += ".tex";
        }
        else if(extension == ".nwb"){
            outputPaths.metadata = outputBase;
            outputPaths.data = outputBase;
            outputPaths.data.replace_extension(".tex");
        }
        else{
            error = "--output must be an output base name or a .nwb filename.";
            return false;
        }
    }

    outputPaths.metadataTemporary = outputPaths.metadata;
    outputPaths.metadataTemporary += ".tmp";
    outputPaths.dataTemporary = outputPaths.data;
    outputPaths.dataTemporary += ".tmp";

    return true;
}

[[nodiscard]] static bool CheckOutputPath(
    const std::filesystem::path& path,
    const bool force,
    const bool isTemporary,
    std::string& error
){
    std::error_code filesystemError;
    const bool exists = std::filesystem::exists(path, filesystemError);
    if(filesystemError){
        error = "Failed to query output path " + ToUtf8(path) + ": " + filesystemError.message();
        return false;
    }
    if(!exists)
        return true;

    if(std::filesystem::is_directory(path, filesystemError)){
        if(filesystemError){
            error = "Failed to inspect output path " + ToUtf8(path) + ": " + filesystemError.message();
            return false;
        }
        error = "Output path is a directory: " + ToUtf8(path);
        return false;
    }
    if(filesystemError){
        error = "Failed to inspect output path " + ToUtf8(path) + ": " + filesystemError.message();
        return false;
    }

    if(isTemporary){
        error = "Temporary output already exists: " + ToUtf8(path);
        return false;
    }
    if(!force){
        error = "Output already exists: " + ToUtf8(path) + ". Pass --force to replace both output files.";
        return false;
    }

    return true;
}

[[nodiscard]] static bool ValidateOutputPaths(const OutputPaths& outputPaths, const bool force, std::string& error){
    return CheckOutputPath(outputPaths.metadata, force, false, error)
        && CheckOutputPath(outputPaths.data, force, false, error)
        && CheckOutputPath(outputPaths.metadataTemporary, false, true, error)
        && CheckOutputPath(outputPaths.dataTemporary, false, true, error);
}

static void RemoveQuietly(const std::filesystem::path& path){
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

[[nodiscard]] static bool RemoveOutputPath(const std::filesystem::path& path, std::string& error){
    std::error_code filesystemError;
    const bool removed = std::filesystem::remove(path, filesystemError);
    if(filesystemError){
        error = "Failed to replace output " + ToUtf8(path) + ": " + filesystemError.message();
        return false;
    }
    if(!removed && std::filesystem::exists(path, filesystemError)){
        if(filesystemError)
            error = "Failed to query output " + ToUtf8(path) + ": " + filesystemError.message();
        else
            error = "Failed to remove existing output " + ToUtf8(path) + ".";
        return false;
    }
    if(filesystemError){
        error = "Failed to query output " + ToUtf8(path) + ": " + filesystemError.message();
        return false;
    }
    return true;
}

[[nodiscard]] static bool WriteBytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::string& error
){
    if(bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())){
        error = "Texture payload is too large to write.";
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if(!file){
        error = "Failed to open output data file " + ToUtf8(path) + ".";
        return false;
    }

    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if(!file){
        error = "Failed while writing output data file " + ToUtf8(path) + ".";
        return false;
    }

    return true;
}

[[nodiscard]] static bool WriteText(const std::filesystem::path& path, const std::string& text, std::string& error){
    std::ofstream file(path, std::ios::trunc);
    if(!file){
        error = "Failed to open output metadata file " + ToUtf8(path) + ".";
        return false;
    }

    file << text;
    if(!file){
        error = "Failed while writing output metadata file " + ToUtf8(path) + ".";
        return false;
    }

    return true;
}

[[nodiscard]] static std::string EscapeMetascriptString(const std::string& value){
    std::string result;
    result.reserve(value.size());
    for(const char character : value){
        switch(character){
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

[[nodiscard]] static std::string BuildMetadata(
    const TexturePayload& payload,
    const std::filesystem::path& dataPath,
    const bool srgb
){
    std::ostringstream output;
    output
        << "// Generated by tex_conv. The referenced .tex file is a contiguous raw UASTC block stream.\n"
        << "texture asset;\n"
        << "\n"
        << "asset.version = 1;\n"
        << "asset.format = \"uastc_ldr_4x4\";\n"
        << "asset.uastc_spec_revision = \"" << s_UastcSpecificationRevision << "\";\n"
        << "asset.color_space = \"" << (srgb ? "srgb" : "linear") << "\";\n"
        << "asset.width = " << payload.width << ";\n"
        << "asset.height = " << payload.height << ";\n"
        << "asset.block_width = " << s_UastcBlockWidth << ";\n"
        << "asset.block_height = " << s_UastcBlockHeight << ";\n"
        << "asset.bytes_per_block = " << s_UastcBytesPerBlock << ";\n"
        << "asset.payload_layout = \"mip_major_blocks\";\n"
        << "asset.mip_address_mode = \"clamp\";\n"
        << "asset.has_alpha = " << (payload.hasAlpha ? 1 : 0) << ";\n"
        << "asset.mip_count = " << payload.mips.size() << ";\n"
        << "asset.data = \"" << EscapeMetascriptString(ToUtf8(dataPath.filename())) << "\";\n"
        << "asset.mips = [\n"
    ;

    for(std::size_t index = 0u; index < payload.mips.size(); ++index){
        const MipLevel& mip = payload.mips[index];
        output
            << "    { \"level\": " << mip.level
            << ", \"width\": " << mip.width
            << ", \"height\": " << mip.height
            << ", \"blocks_x\": " << mip.blocksX
            << ", \"blocks_y\": " << mip.blocksY
            << ", \"offset_bytes\": " << mip.offsetBytes
            << ", \"size_bytes\": " << mip.sizeBytes
            << " }" << (index + 1u == payload.mips.size() ? "" : ",") << "\n"
        ;
    }

    output << "];\n";
    return output.str();
}

[[nodiscard]] static bool EncodeTexture(
    const std::filesystem::path& inputPath,
    const bool srgb,
    TexturePayload& output,
    std::string& error
){
    BasisLibrary library;
    if(!library.initialize(error))
        return false;

    basisu::job_pool jobPool(1u);
    basisu::basis_compressor_params parameters;
    parameters.set_format_mode(basist::basis_tex_format::cUASTC_LDR_4x4);
    parameters.set_srgb_options(srgb);
    parameters.m_read_source_images = true;
    parameters.m_source_filenames.push_back(ToUtf8(inputPath));
    parameters.m_mip_gen = true;
    parameters.m_mip_smallest_dimension = 1;
    parameters.m_mip_wrapping = false;
    parameters.m_status_output = false;
    parameters.m_compute_stats = false;
    parameters.m_print_stats = false;
    parameters.m_write_output_basis_or_ktx2_files = false;
    parameters.m_create_ktx2_file = false;
    parameters.m_pJob_pool = &jobPool;

    basisu::basis_compressor compressor;
    if(!compressor.init(parameters)){
        error = "Basis Universal encoder initialization failed for " + ToUtf8(inputPath) + ".";
        return false;
    }

    const basisu::basis_compressor::error_code encodeResult = compressor.process();
    if(encodeResult != basisu::basis_compressor::cECSuccess){
        error = "UASTC encoding failed for " + ToUtf8(inputPath)
            + " (Basis Universal error " + std::to_string(static_cast<unsigned int>(encodeResult)) + ").";
        return false;
    }

    const basisu::basisu_backend_output& backendOutput = compressor.get_uastc_backend_output();
    if(backendOutput.m_tex_format != basist::basis_tex_format::cUASTC_LDR_4x4){
        error = "Basis Universal did not produce UASTC LDR 4x4 blocks.";
        return false;
    }
    if(backendOutput.m_slice_desc.empty()
        || backendOutput.m_slice_desc.size() != backendOutput.m_slice_image_data.size()){
        error = "Basis Universal returned an incomplete UASTC mip chain.";
        return false;
    }

    const std::size_t mipCount = backendOutput.m_slice_desc.size();
    const std::size_t noSlice = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> sliceForMip(mipCount, noSlice);
    for(std::size_t sliceIndex = 0u; sliceIndex < mipCount; ++sliceIndex){
        const basisu::basisu_backend_slice_desc& descriptor = backendOutput.m_slice_desc[sliceIndex];
        if(descriptor.m_source_file_index != 0u
            || static_cast<std::size_t>(descriptor.m_mip_index) >= mipCount
            || sliceForMip[descriptor.m_mip_index] != noSlice){
            error = "Basis Universal returned an unexpected texture-slice layout.";
            return false;
        }
        sliceForMip[descriptor.m_mip_index] = sliceIndex;
    }

    output = {};
    output.mips.reserve(mipCount);
    std::uint64_t offsetBytes = 0u;
    for(std::size_t mipIndex = 0u; mipIndex < mipCount; ++mipIndex){
        const std::size_t sliceIndex = sliceForMip[mipIndex];
        if(sliceIndex == noSlice){
            error = "Basis Universal omitted a mip level.";
            return false;
        }

        const basisu::basisu_backend_slice_desc& descriptor = backendOutput.m_slice_desc[sliceIndex];
        const basisu::uint8_vec& encodedBlocks = backendOutput.m_slice_image_data[sliceIndex];
        if(descriptor.m_orig_width == 0u || descriptor.m_orig_height == 0u){
            error = "Basis Universal produced a mip level with an invalid resolution.";
            return false;
        }

        const std::uint32_t blocksX = (descriptor.m_orig_width + s_UastcBlockWidth - 1u) / s_UastcBlockWidth;
        const std::uint32_t blocksY = (descriptor.m_orig_height + s_UastcBlockHeight - 1u) / s_UastcBlockHeight;
        const std::uint64_t expectedSize = static_cast<std::uint64_t>(blocksX)
            * static_cast<std::uint64_t>(blocksY)
            * static_cast<std::uint64_t>(s_UastcBytesPerBlock);
        const std::size_t encodedSize = encodedBlocks.size_in_bytes();

        if(descriptor.m_num_blocks_x != blocksX
            || descriptor.m_num_blocks_y != blocksY
            || expectedSize != static_cast<std::uint64_t>(encodedSize)){
            error = "Basis Universal produced an invalid UASTC block layout.";
            return false;
        }
        if(output.bytes.size() > std::numeric_limits<std::size_t>::max() - encodedSize){
            error = "UASTC payload is too large to store.";
            return false;
        }

        MipLevel mip;
        mip.level = static_cast<std::uint32_t>(mipIndex);
        mip.width = descriptor.m_orig_width;
        mip.height = descriptor.m_orig_height;
        mip.blocksX = blocksX;
        mip.blocksY = blocksY;
        mip.offsetBytes = offsetBytes;
        mip.sizeBytes = expectedSize;
        output.mips.push_back(mip);

        const std::uint8_t* const source = encodedBlocks.get_ptr();
        output.bytes.insert(output.bytes.end(), source, source + encodedSize);
        offsetBytes += expectedSize;
    }

    output.width = output.mips.front().width;
    output.height = output.mips.front().height;
    output.hasAlpha = compressor.get_any_source_image_has_alpha();
    return true;
}

[[nodiscard]] static bool WriteOutputs(
    const OutputPaths& outputPaths,
    const TexturePayload& payload,
    const bool srgb,
    const bool force,
    std::string& error
){
    const std::string metadata = BuildMetadata(payload, outputPaths.data, srgb);
    if(!WriteBytes(outputPaths.dataTemporary, payload.bytes, error)){
        RemoveQuietly(outputPaths.dataTemporary);
        return false;
    }
    if(!WriteText(outputPaths.metadataTemporary, metadata, error)){
        RemoveQuietly(outputPaths.dataTemporary);
        RemoveQuietly(outputPaths.metadataTemporary);
        return false;
    }

    if(force){
        if(!RemoveOutputPath(outputPaths.data, error) || !RemoveOutputPath(outputPaths.metadata, error)){
            RemoveQuietly(outputPaths.dataTemporary);
            RemoveQuietly(outputPaths.metadataTemporary);
            return false;
        }
    }

    std::error_code filesystemError;
    std::filesystem::rename(outputPaths.dataTemporary, outputPaths.data, filesystemError);
    if(filesystemError){
        RemoveQuietly(outputPaths.dataTemporary);
        RemoveQuietly(outputPaths.metadataTemporary);
        error = "Failed to finalize output data file " + ToUtf8(outputPaths.data) + ": " + filesystemError.message();
        return false;
    }

    std::filesystem::rename(outputPaths.metadataTemporary, outputPaths.metadata, filesystemError);
    if(filesystemError){
        RemoveQuietly(outputPaths.data);
        RemoveQuietly(outputPaths.metadataTemporary);
        error = "Failed to finalize output metadata file " + ToUtf8(outputPaths.metadata) + ": " + filesystemError.message();
        return false;
    }

    return true;
}

[[nodiscard]] static int Run(const int argc, char** argv){
    std::string inputArgument;
    std::string outputArgument;
    bool force = false;
    bool linear = false;

    CLI::App app("Convert an LDR image into an NWB UASTC texture asset.");
    app.add_option("input", inputArgument, "Input image (.png, .jpg, .jpeg, .jfif, .tga, or .qoi)")->required();
    app.add_option("-o,--output", outputArgument, "Output base name or .nwb filename");
    app.add_flag("--linear", linear, "Treat input as linear data instead of sRGB color");
    app.add_flag("--force", force, "Replace existing .nwb and .tex output files");

    try{
        app.parse(argc, argv);
    }
    catch(const CLI::ParseError& parseError){
        return app.exit(parseError, std::cout, std::cerr);
    }

    try{
        const std::filesystem::path inputPath = PathFromUtf8(inputArgument);
        std::error_code filesystemError;
        if(!std::filesystem::is_regular_file(inputPath, filesystemError)){
            if(filesystemError)
                std::cerr << "tex_conv: failed to inspect input " << ToUtf8(inputPath) << ": " << filesystemError.message() << "\n";
            else
                std::cerr << "tex_conv: input image was not found or is not a regular file: " << ToUtf8(inputPath) << "\n";
            return 1;
        }
        if(!IsSupportedInputPath(inputPath)){
            std::cerr << "tex_conv: unsupported input format. v1 accepts PNG, JPEG/JFIF, TGA, and QOI LDR images.\n";
            return 1;
        }

        OutputPaths outputPaths;
        std::string error;
        if(!ResolveOutputPaths(inputPath, outputArgument, outputPaths, error)
            || !ValidateOutputPaths(outputPaths, force, error)){
            std::cerr << "tex_conv: " << error << "\n";
            return 1;
        }

        TexturePayload payload;
        if(!EncodeTexture(inputPath, !linear, payload, error)){
            std::cerr << "tex_conv: " << error << "\n";
            return 1;
        }
        if(!WriteOutputs(outputPaths, payload, !linear, force, error)){
            std::cerr << "tex_conv: " << error << "\n";
            return 1;
        }

        std::cout
            << "Wrote " << ToUtf8(outputPaths.metadata) << "\n"
            << "Wrote " << ToUtf8(outputPaths.data) << "\n"
            << "  " << payload.width << "x" << payload.height
            << ", " << payload.mips.size() << " mips, " << payload.bytes.size() << " bytes\n"
        ;
        return 0;
    }
    catch(const std::exception& exception){
        std::cerr << "tex_conv: " << exception.what() << "\n";
        return 1;
    }
}

int EntryPoint(const isize argc, char** argv, void*){
    return Run(static_cast<int>(argc), argv);
}

#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, Run);
}
#endif

};

NWB_DEFINE_APPLICATION_ENTRY_POINT(__hidden_tex_conv::EntryPoint)
