// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_shader_compile.h"

#include <shaderc/shaderc.hpp>

#include <core/graphics/shader_cook.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_shader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static shaderc_shader_kind MapStageToShaderKind(const AStringView stage){
    struct StageMapping{
        AStringView name;
        shaderc_shader_kind kind;
    };

    static constexpr StageMapping s_StageMappings[] = {
        { "vs", shaderc_vertex_shader },
        { "ps", shaderc_fragment_shader },
        { "cs", shaderc_compute_shader },
        { "gs", shaderc_geometry_shader },
        { "hs", shaderc_tess_control_shader },
        { "ds", shaderc_tess_evaluation_shader },
        { "vert", shaderc_vertex_shader },
        { "frag", shaderc_fragment_shader },
        { "comp", shaderc_compute_shader },
        { "geom", shaderc_geometry_shader },
        { "tesc", shaderc_tess_control_shader },
        { "tese", shaderc_tess_evaluation_shader },
        { "task", shaderc_task_shader },
        { "mesh", shaderc_mesh_shader },
        { "rgen", shaderc_raygen_shader },
        { "rint", shaderc_intersection_shader },
        { "rahit", shaderc_anyhit_shader },
        { "rchit", shaderc_closesthit_shader },
        { "rmiss", shaderc_miss_shader },
        { "rcall", shaderc_callable_shader }
    };

    for(const StageMapping& mapping : s_StageMappings){
        if(stage == mapping.name)
            return mapping.kind;
    }

    return shaderc_glsl_infer_from_source;
}


static shaderc_source_language MapCompilerToSourceLanguage(const AStringView compiler){
    if(compiler == "dxc" || compiler == "hlsl")
        return shaderc_source_language_hlsl;
    return shaderc_source_language_glsl;
}

static UniquePtr<IShaderCompiler> CreateVulkanShaderCompiler(){
    return MakeUnique<VulkanShaderCompiler>();
}

static ShaderCompilerFactory s_VulkanShaderCompilerFactory = {
    "vulkan",
    &CreateVulkanShaderCompiler
};
static AutoShaderCompilerFactoryRegistration s_VulkanShaderCompilerFactoryRegistration(s_VulkanShaderCompilerFactory);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderFileIncluder final : public shaderc::CompileOptions::IncluderInterface{
private:
    struct IncludePayload{
        std::string sourceName;
        std::string content;
    };


public:
    explicit ShaderFileIncluder(const Vector<Path>& includeDirectories)
        : m_includeDirectories(includeDirectories)
    {}


    shaderc_include_result* GetInclude(
        const char* requestedSource,
        const shaderc_include_type type,
        const char* requestingSource,
        [[maybe_unused]] const size_t includeDepth
    ) override{
        const Path requestingDirectory = Path(requestingSource).parent_path();
        Path resolvedPath;

        if(type == shaderc_include_type_relative){
            std::error_code errorCode;
            const Path localCandidate = (requestingDirectory / Path(requestedSource)).lexically_normal();
            if(FileExists(localCandidate, errorCode) && !errorCode)
                resolvedPath = localCandidate;
        }

        if(resolvedPath.empty()){
            for(const Path& includeDirectory : m_includeDirectories){
                std::error_code errorCode;
                const Path candidate = (includeDirectory / Path(requestedSource)).lexically_normal();
                if(FileExists(candidate, errorCode) && !errorCode){
                    resolvedPath = candidate;
                    break;
                }
            }
        }

        auto* result = new shaderc_include_result{};
        auto* payload = new IncludePayload{};

        if(resolvedPath.empty()){
            payload->content = StringFormat("Include '{}' not found", requestedSource);
            result->source_name = "";
            result->source_name_length = 0;
            result->content = payload->content.c_str();
            result->content_length = payload->content.size();
            result->user_data = payload;
            return result;
        }

        AString fileContent;
        if(!ReadTextFile(resolvedPath, fileContent)){
            payload->content = StringFormat("Failed to read include '{}'", PathToString(resolvedPath));
            result->source_name = "";
            result->source_name_length = 0;
            result->content = payload->content.c_str();
            result->content_length = payload->content.size();
            result->user_data = payload;
            return result;
        }

        payload->sourceName = PathToString(resolvedPath);
        payload->content.assign(fileContent.data(), fileContent.size());
        result->source_name = payload->sourceName.c_str();
        result->source_name_length = payload->sourceName.size();
        result->content = payload->content.c_str();
        result->content_length = payload->content.size();
        result->user_data = payload;
        return result;
    }

    void ReleaseInclude(shaderc_include_result* data) override{
        if(!data)
            return;

        delete static_cast<IncludePayload*>(data->user_data);
        delete data;
    }


private:
    const Vector<Path>& m_includeDirectories;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool VulkanShaderCompiler::compileVariant(const ShaderCompilerRequest& request, AString& outError){
    std::error_code errorCode;
    CreateDirectories(request.cachePath.parent_path(), errorCode);
    if(errorCode){
        outError = StringFormat(
            "Failed to create cache directory '{}' : {}",
            PathToString(request.cachePath.parent_path()),
            errorCode.message()
        );
        return false;
    }

    AString sourceText;
    if(!ReadTextFile(request.sourcePath, sourceText)){
        outError = StringFormat("Failed to read shader source '{}'", PathToString(request.sourcePath));
        return false;
    }

    const shaderc_shader_kind shaderKind = __hidden_vulkan_shader::MapStageToShaderKind(request.stage);
    if(shaderKind == shaderc_glsl_infer_from_source){
        outError = StringFormat("Unknown shader stage '{}' in entry '{}'", request.stage, request.shaderName);
        return false;
    }

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    options.SetSourceLanguage(__hidden_vulkan_shader::MapCompilerToSourceLanguage(request.compiler));

    for(const auto& [defineName, value] : request.defineCombo)
        options.AddMacroDefinition(defineName, value);

    options.SetIncluder(std::make_unique<__hidden_vulkan_shader::ShaderFileIncluder>(request.includeDirectories));

    shaderc::Compiler compiler;
    const AString inputFileName = PathToString(request.sourcePath);
    const AString entryPoint = AString(request.entryPoint);
    const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        sourceText.c_str(),
        sourceText.size(),
        shaderKind,
        inputFileName.c_str(),
        entryPoint.c_str(),
        options
    );

    if(result.GetCompilationStatus() != shaderc_compilation_status_success){
        outError = StringFormat(
            "Shader compile failed for '{}' (variant '{}') :\n{}",
            request.shaderName,
            Core::ShaderCook::BuildVariantName(request.defineCombo),
            result.GetErrorMessage()
        );
        return false;
    }

    const usize spirvWordCount = static_cast<usize>(result.cend() - result.cbegin());
    const usize spirvSize = spirvWordCount * sizeof(u32);
    if(spirvSize == 0){
        outError = StringFormat(
            "Shader compile failed for '{}' (variant '{}') : compiled bytecode is empty",
            request.shaderName,
            Core::ShaderCook::BuildVariantName(request.defineCombo)
        );
        return false;
    }

    std::ofstream stream(request.cachePath, std::ofstream::binary | std::ofstream::trunc);
    if(!stream.is_open()){
        outError = StringFormat("Failed to open output '{}' for writing", PathToString(request.cachePath));
        return false;
    }

    stream.write(reinterpret_cast<const char*>(result.cbegin()), static_cast<std::streamsize>(spirvSize));
    if(!stream.good()){
        outError = StringFormat("Failed to write SPIR-V output '{}'", PathToString(request.cachePath));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

