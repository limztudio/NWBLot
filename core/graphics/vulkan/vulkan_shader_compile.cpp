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
    if(stage == "vs") return shaderc_vertex_shader;
    if(stage == "ps") return shaderc_fragment_shader;
    if(stage == "cs") return shaderc_compute_shader;
    if(stage == "gs") return shaderc_geometry_shader;
    if(stage == "hs") return shaderc_tess_control_shader;
    if(stage == "ds") return shaderc_tess_evaluation_shader;
    if(stage == "vert") return shaderc_vertex_shader;
    if(stage == "frag") return shaderc_fragment_shader;
    if(stage == "comp") return shaderc_compute_shader;
    if(stage == "geom") return shaderc_geometry_shader;
    if(stage == "tesc") return shaderc_tess_control_shader;
    if(stage == "tese") return shaderc_tess_evaluation_shader;
    if(stage == "task") return shaderc_task_shader;
    if(stage == "mesh") return shaderc_mesh_shader;
    if(stage == "rgen") return shaderc_raygen_shader;
    if(stage == "rint") return shaderc_intersection_shader;
    if(stage == "rahit") return shaderc_anyhit_shader;
    if(stage == "rchit") return shaderc_closesthit_shader;
    if(stage == "rmiss") return shaderc_miss_shader;
    if(stage == "rcall") return shaderc_callable_shader;
    return shaderc_glsl_infer_from_source;
}


static shaderc_source_language MapCompilerToSourceLanguage(const AStringView compiler){
    if(compiler == "dxc" || compiler == "hlsl")
        return shaderc_source_language_hlsl;
    return shaderc_source_language_glsl;
}

static IShaderCompiler* CreateVulkanShaderCompiler(){
    return new VulkanShaderCompiler();
}

static ShaderCompilerFactory s_VulkanShaderCompilerFactory = {
    "vulkan",
    &CreateVulkanShaderCompiler
};
static AutoShaderCompilerFactoryRegistration s_VulkanShaderCompilerFactoryRegistration(s_VulkanShaderCompilerFactory);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderFileIncluder final : public shaderc::CompileOptions::IncluderInterface{
private:
    using IncludePayload = Pair<std::string*, std::string*>;


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
        if(resolvedPath.empty()){
            auto* errorMessage = new std::string(
                StringFormat("Include '{}' not found", requestedSource)
            );
            result->source_name = "";
            result->source_name_length = 0;
            result->content = errorMessage->c_str();
            result->content_length = errorMessage->size();
            result->user_data = errorMessage;
            return result;
        }

        AString fileContent;
        if(!ReadTextFile(resolvedPath, fileContent)){
            auto* errorMessage = new std::string(
                StringFormat("Failed to read include '{}'", PathToString(resolvedPath))
            );
            result->source_name = "";
            result->source_name_length = 0;
            result->content = errorMessage->c_str();
            result->content_length = errorMessage->size();
            result->user_data = errorMessage;
            return result;
        }

        auto* resolvedName = new std::string(PathToString(resolvedPath));
        auto* content = new std::string(Move(fileContent));
        result->source_name = resolvedName->c_str();
        result->source_name_length = resolvedName->size();
        result->content = content->c_str();
        result->content_length = content->size();
        result->user_data = new IncludePayload(resolvedName, content);
        return result;
    }

    void ReleaseInclude(shaderc_include_result* data) override{
        if(!data)
            return;

        if(data->source_name_length == 0){
            delete static_cast<std::string*>(data->user_data);
        }
        else{
            auto* payload = static_cast<IncludePayload*>(data->user_data);
            delete payload->first();
            delete payload->second();
            delete payload;
        }

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

    const auto* spirvBegin = reinterpret_cast<const u8*>(result.cbegin());
    const auto* spirvEnd = reinterpret_cast<const u8*>(result.cend());
    const usize spirvSize = static_cast<usize>(spirvEnd - spirvBegin);

    std::ofstream stream(request.cachePath, std::ofstream::binary | std::ofstream::trunc);
    if(!stream.is_open()){
        outError = StringFormat("Failed to open output '{}' for writing", PathToString(request.cachePath));
        return false;
    }

    stream.write(reinterpret_cast<const char*>(spirvBegin), static_cast<std::streamsize>(spirvSize));
    if(!stream.good()){
        outError = StringFormat("Failed to write SPIR-V output '{}'", PathToString(request.cachePath));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

