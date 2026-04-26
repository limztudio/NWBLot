// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_shader_compiler.h"

#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <shaderc/shaderc.hpp>

#include <logger/client/logger.h>


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


static bool TryMapCompilerToSourceLanguage(const AStringView compiler, shaderc_source_language& outLanguage){
    if(compiler.empty() || compiler == "glslang" || compiler == "glsl"){
        outLanguage = shaderc_source_language_glsl;
        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderFileIncluder final : public shaderc::CompileOptions::IncluderInterface{
private:
    struct IncludePayload{
        AString sourceName;
        AString content;
    };


public:
    explicit ShaderFileIncluder(const Vector<Path, Alloc::CustomAllocator<Path>>& includeDirectories)
        : m_includeDirectories(includeDirectories)
    {}


    virtual shaderc_include_result* GetInclude(const char* requestedSource, const shaderc_include_type type, const char* requestingSource, [[maybe_unused]] const size_t includeDepth)override{
        ErrorCode errorCode;

        const Path requestingDirectory = Path(requestingSource).parent_path();
        Path resolvedPath;
        AString lookupError;

        if(type == shaderc_include_type_relative){
            const Path localCandidate = (requestingDirectory / Path(requestedSource)).lexically_normal();
            errorCode.clear();
            if(IsRegularFile(localCandidate, errorCode))
                resolvedPath = localCandidate;
            else if(errorCode && !IsMissingPathError(errorCode))
                lookupError = StringFormat("Failed to query include candidate '{}' : {}", PathToString(localCandidate), errorCode.message());
        }

        if(resolvedPath.empty() && lookupError.empty()){
            for(const Path& includeDirectory : m_includeDirectories){
                const Path candidate = (includeDirectory / Path(requestedSource)).lexically_normal();
                errorCode.clear();
                if(IsRegularFile(candidate, errorCode)){
                    resolvedPath = candidate;
                    break;
                }
                if(errorCode && !IsMissingPathError(errorCode)){
                    lookupError = StringFormat("Failed to query include candidate '{}' : {}", PathToString(candidate), errorCode.message());
                    break;
                }
            }
        }

        auto result = MakeUnique<shaderc_include_result>();
        auto payload = MakeUnique<IncludePayload>();

        const auto releaseResult = [&]() -> shaderc_include_result*{
            result->user_data = payload.get();
            payload.release();
            return result.release();
        };

        const auto makeErrorResult = [&](AString message) -> shaderc_include_result*{
            payload->content = Move(message);
            result->source_name = "";
            result->source_name_length = 0;
            result->content = payload->content.c_str();
            result->content_length = payload->content.size();
            return releaseResult();
        };

        if(!lookupError.empty())
            return makeErrorResult(Move(lookupError));

        if(resolvedPath.empty())
            return makeErrorResult(StringFormat("Include '{}' not found", requestedSource));

        AString fileContent;
        if(!ReadTextFile(resolvedPath, fileContent))
            return makeErrorResult(StringFormat("Failed to read include '{}'", PathToString(resolvedPath)));
        StripUtf8Bom(fileContent);

        payload->sourceName = PathToString(resolvedPath);
        payload->content = Move(fileContent);
        result->source_name = payload->sourceName.c_str();
        result->source_name_length = payload->sourceName.size();
        result->content = payload->content.c_str();
        result->content_length = payload->content.size();
        return releaseResult();
    }

    virtual void ReleaseInclude(shaderc_include_result* data)override{
        if(!data)
            return;

        [[maybe_unused]] UniquePtr<IncludePayload> payload(static_cast<IncludePayload*>(data->user_data));
        [[maybe_unused]] UniquePtr<shaderc_include_result> result(data);
    }


private:
    const Vector<Path, Alloc::CustomAllocator<Path>>& m_includeDirectories;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VulkanShaderCompiler::VulkanShaderCompiler(Alloc::CustomArena& memoryArena)
    : IShaderCompiler(memoryArena)
{}


bool VulkanShaderCompiler::compileVariant(const ShaderCompilerRequest& request, Vector<u8>& outBytecode){
    outBytecode.clear();

    if(request.sourcePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : source path is empty"), StringConvert(request.shaderName));
        return false;
    }

    if(request.entryPoint.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : entry point is empty"), StringConvert(request.shaderName));
        return false;
    }

    AString sourceText;
    if(!ReadTextFile(request.sourcePath, sourceText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to read shader source '{}'"), PathToString<tchar>(request.sourcePath));
        return false;
    }
    StripUtf8Bom(sourceText);

    const shaderc_shader_kind shaderKind = __hidden_vulkan_shader::MapStageToShaderKind(request.stage);
    if(shaderKind == shaderc_glsl_infer_from_source){
        NWB_LOGGER_ERROR(NWB_TEXT("Unknown shader stage '{}' in entry '{}'"), StringConvert(request.stage), StringConvert(request.shaderName));
        return false;
    }

    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    shaderc_source_language sourceLanguage = shaderc_source_language_glsl;
    if(!__hidden_vulkan_shader::TryMapCompilerToSourceLanguage(request.compiler, sourceLanguage)){
        NWB_LOGGER_ERROR(NWB_TEXT("Unknown shader compiler '{}' in entry '{}'"), StringConvert(request.compiler), StringConvert(request.shaderName));
        return false;
    }
    options.SetSourceLanguage(sourceLanguage);

    for(u32 i = 0; i < request.defineCount; ++i){
        const ShaderMacroDefinition& define = request.defines[i];
        options.AddMacroDefinition(
            define.name.data(),
            define.name.size(),
            define.value.data(),
            define.value.size()
        );
    }

    options.SetIncluder(MakeStdUnique<__hidden_vulkan_shader::ShaderFileIncluder>(request.includeDirectories));

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
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') :\n{}"),
            StringConvert(request.shaderName),
            StringConvert(request.variantName),
            StringConvert(result.GetErrorMessage())
        );
        return false;
    }

    const usize spirvWordCount = static_cast<usize>(result.cend() - result.cbegin());
    if(spirvWordCount > Limit<usize>::s_Max / sizeof(u32)){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : bytecode size overflows"),
            StringConvert(request.shaderName),
            StringConvert(request.variantName)
        );
        return false;
    }
    const usize spirvSize = spirvWordCount * sizeof(u32);
    if(spirvSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : compiled bytecode is empty"),
            StringConvert(request.shaderName),
            StringConvert(request.variantName)
        );
        return false;
    }

    outBytecode.resize(spirvSize);
    NWB_MEMCPY(outBytecode.data(), spirvSize, result.cbegin(), spirvSize);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

