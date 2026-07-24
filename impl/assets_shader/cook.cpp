// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/paths.h>
#include <core/metascript/parser.h>

#include <core/common/log.h>
#include <global/hash_utils.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_SLANGC_EXECUTABLE
#error "NWB_SLANGC_EXECUTABLE must be defined by the build configuration"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = Core::Assets;
namespace Alloc = Core::Alloc;
namespace Metascript = Core::Metascript;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// common cook aliases and asset type keywords


using CookString = ShaderCook::CookString;
template<typename T>
using CookVector = ShaderCook::CookVector<T>;
template<typename T, typename V>
using CookMap = ShaderCook::CookMap<T, V>;
template<typename T>
using CookHashSet = ShaderCook::CookHashSet<T>;
using ScratchString = AString<Alloc::ScratchArena>;
template<typename T>
using ScratchVector = Vector<T, Alloc::ScratchArena>;
template<typename T>
using ScratchHashSet = HashSet<T, Hasher<T>, EqualTo<T>, Alloc::ScratchArena>;

static constexpr AStringView s_AssetTypeShader = "shader";
static constexpr AStringView s_AssetTypeInclude = "include";
static constexpr AStringView s_SlangSourceExtension = ".slang";
static constexpr AStringView s_SlangIncludeExtension = ".slangi";
// Material defaults deliberately use decimal literals inside half constructors. Slang also reports profile upgrades
// for capabilities it adds to valid emitted SPIR-V; asset metadata normalizes profile text and cannot preserve Slang's
// case-sensitive capability names. Binding aliases are handled at their declarations so new ABI issues remain visible.
static constexpr AStringView s_SuppressedCompilerWarnings = "30081,41012";
static constexpr usize s_BaseCompilerArgumentCount = 17u;

struct NormalizedDependencyRootAlias{
    Path root;
    CookString key;
    usize depth = 0u;

    explicit NormalizedDependencyRootAlias(ShaderCook::CookArena& arena)
        : root(arena)
        , key(arena)
    {}
};

static usize PathDepth(const Path& path){
    usize depth = 0u;
    for(auto it = path.begin(); it != path.end(); ++it)
        ++depth;
    return depth;
}

static Path NormalizeDependencyRootAliasPath(Path path){
    path = path.lexically_normal();
    while(!path.empty() && !path.has_filename()){
        const Path parentPath = path.parent_path();
        if(parentPath.empty() || parentPath == path)
            break;
        path = parentPath;
    }
    return path;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Slang source compiler


static bool TryMapStageToSlangStage(const AStringView stage, AStringView& outStage){
    struct StageMapping{
        AStringView name;
        AStringView slangStage;
    };

    static constexpr StageMapping s_StageMappings[] = {
        { "vs", "vertex" },
        { "ps", "fragment" },
        { "cs", "compute" },
        { "mesh", "mesh" },
        { "rgen", "raygeneration" },
        { "rmiss", "miss" },
        { "rchit", "closesthit" },
        { "rahit", "anyhit" },
        { "rint", "intersection" },
        { "rcall", "callable" },
    };

    for(const StageMapping& mapping : s_StageMappings){
        if(stage == mapping.name){
            outStage = mapping.slangStage;
            return true;
        }
    }

    outStage = {};
    return false;
}

#if defined(NWB_PLATFORM_WINDOWS)
template<typename StringT>
static void AppendWindowsCommandLineArgument(StringT& inOutCommand, const AStringView value){
    if(!inOutCommand.empty())
        inOutCommand += ' ';

    bool needsQuotes = value.empty();
    for(const char ch : value){
        if(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '"'){
            needsQuotes = true;
            break;
        }
    }

    if(!needsQuotes){
        inOutCommand.append(value.data(), value.size());
        return;
    }

    inOutCommand += '"';
    usize backslashCount = 0u;
    for(const char ch : value){
        if(ch == '\\'){
            ++backslashCount;
            continue;
        }

        if(ch == '"'){
            for(usize i = 0u; i < backslashCount * 2u + 1u; ++i)
                inOutCommand += '\\';
            inOutCommand += '"';
            backslashCount = 0u;
            continue;
        }

        for(usize i = 0u; i < backslashCount; ++i)
            inOutCommand += '\\';
        inOutCommand += ch;
        backslashCount = 0u;
    }
    for(usize i = 0u; i < backslashCount * 2u; ++i)
        inOutCommand += '\\';
    inOutCommand += '"';
}
#endif

template<typename ArenaT, typename ArgumentVectorT>
static void PushCompilerArgument(ArenaT& arena, ArgumentVectorT& inOutArguments, const AStringView value){
    using StringT = typename ArgumentVectorT::value_type;
    inOutArguments.push_back(StringT(value, arena));
}

template<typename StringT>
static bool ReadDiagnostics(const Path& diagnosticsPath, StringT& outDiagnostics){
    outDiagnostics.clear();

    ErrorCode errorCode;
    if(!FileExists(diagnosticsPath, errorCode) || errorCode)
        return false;

    return ReadTextFile(diagnosticsPath, outDiagnostics);
}

static void RemoveFileBestEffort(const Path& path){
    ErrorCode errorCode;
    const bool exists = FileExists(path, errorCode);
    if(errorCode){
        if(!IsMissingPathError(errorCode)){
            NWB_LOGGER_WARNING(NWB_TEXT("ShaderCook: failed to query stale compiler output '{}' before cleanup: {}")
                , PathToString<tchar>(path)
                , StringConvert(errorCode.message())
            );
        }
        return;
    }

    if(exists){
        errorCode.clear();
        if(!RemoveFile(path, errorCode)){
            if(errorCode){
                NWB_LOGGER_WARNING(NWB_TEXT("ShaderCook: failed to remove stale compiler output '{}': {}")
                    , PathToString<tchar>(path)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_WARNING(NWB_TEXT("ShaderCook: failed to remove stale compiler output '{}'")
                    , PathToString<tchar>(path)
                );
            }
        }
    }
}

class ScopedFileCleanupGuard final : NoCopy{
public:
    explicit ScopedFileCleanupGuard(const Path& path)noexcept
        : m_path(path)
    {}
    ~ScopedFileCleanupGuard(){
        if(m_active)
            RemoveFileBestEffort(m_path);
    }


public:
    void dismiss()noexcept{ m_active = false; }


private:
    const Path& m_path;
    bool m_active = true;
};

#if defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
static constexpr int s_SignalExitCodeBase = 128;
static constexpr int s_ExecFailureExitCode = 127;
static constexpr mode_t s_DiagnosticsFileMode = 0666;

static int WaitForCompilerProcess(const pid_t childPid)noexcept{
    int status = 0;
    pid_t waitedPid = -1;
    do{
        waitedPid = ::waitpid(childPid, &status, 0);
    }while(waitedPid < 0 && errno == EINTR);

    if(waitedPid != childPid)
        return -1;
    if(WIFEXITED(status))
        return WEXITSTATUS(status);
    if(WIFSIGNALED(status))
        return s_SignalExitCodeBase + WTERMSIG(status);
    return -1;
}
#endif

template<typename ArenaT, typename ArgumentVectorT>
static int RunCompilerProcessRedirected(
    ArenaT& arena,
    const ArgumentVectorT& arguments,
    const Path& diagnosticsPath
){
    if(arguments.empty())
        return -1;

    Vector<const char*, ArenaT> argv(arena);
    argv.reserve(arguments.size() + 1u);
    for(const auto& argument : arguments)
        argv.push_back(argument.c_str());
    argv.push_back(nullptr);

#if defined(NWB_PLATFORM_WINDOWS)
    const AString<ArenaT> diagnosticsPathText = PathToString<char>(arena, diagnosticsPath);
    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE diagnosticsHandle = CreateFileA(
        diagnosticsPathText.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &securityAttributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if(diagnosticsHandle == INVALID_HANDLE_VALUE)
        return -1;

    AString<ArenaT> commandLine{arena};
    for(const auto& argument : arguments)
        AppendWindowsCommandLineArgument(commandLine, AStringView(argument));

    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = diagnosticsHandle;
    startupInfo.hStdError = diagnosticsHandle;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION processInfo = {};
    const BOOL created = CreateProcessA(
        arguments.front().c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo
    );
    CloseHandle(diagnosticsHandle);
    if(!created)
        return -1;

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1u;
    if(!GetExitCodeProcess(processInfo.hProcess, &exitCode))
        NWB_LOGGER_WARNING(NWB_TEXT("ShaderCook: failed to query compiler process exit code"));
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
    const AString<ArenaT> diagnosticsPathText = PathToString<char>(arena, diagnosticsPath);
    const int diagnosticsFd = ::open(diagnosticsPathText.c_str(), O_CREAT | O_TRUNC | O_WRONLY, s_DiagnosticsFileMode);
    if(diagnosticsFd < 0)
        return -1;

    const pid_t childPid = ::fork();
    if(childPid < 0){
        const int closeResult = ::close(diagnosticsFd);
        if(closeResult != 0)
            return -1;
        return -1;
    }

    if(childPid == 0){
        if(::dup2(diagnosticsFd, STDOUT_FILENO) < 0)
            _exit(s_ExecFailureExitCode);
        if(::dup2(diagnosticsFd, STDERR_FILENO) < 0)
            _exit(s_ExecFailureExitCode);
        if(::close(diagnosticsFd) != 0)
            _exit(s_ExecFailureExitCode);
        ::execvp(argv.front(), const_cast<char* const*>(argv.data()));
        _exit(s_ExecFailureExitCode);
    }

    const int closeParentResult = ::close(diagnosticsFd);
    const int compilerExitCode = WaitForCompilerProcess(childPid);
    if(closeParentResult != 0)
        return -1;
    return compilerExitCode;
#else
    static_cast<void>(diagnosticsPath);
    return -1;
#endif
}

class SlangShaderCompiler final : public ShaderCook::IShaderCompiler{
public:
    explicit SlangShaderCompiler(ShaderCook::CookArena& memoryArena)
        : ShaderCook::IShaderCompiler(memoryArena)
    {}


public:
    virtual bool compileVariant(const ShaderCook::ShaderCompilerRequest& request, ShaderCook::CookVector<u8>& outBytecode)override{
        outBytecode.clear();

        if(request.sourcePath.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : source path is empty"), StringConvert(request.shaderName));
            return false;
        }

        if(request.outputPath.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : output path is empty"), StringConvert(request.shaderName));
            return false;
        }

        if(request.entryPoint.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : entry point is empty"), StringConvert(request.shaderName));
            return false;
        }

        if(request.targetProfile.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to compile shader '{}' : target profile is empty"), StringConvert(request.shaderName));
            return false;
        }

        AStringView slangStage;
        if(!TryMapStageToSlangStage(request.stage, slangStage)){
            NWB_LOGGER_ERROR(NWB_TEXT("Unknown shader stage '{}' in entry '{}'"), StringConvert(request.stage), StringConvert(request.shaderName));
            return false;
        }

        Path diagnosticsPath = request.outputPath;
        diagnosticsPath += ".diag";
        RemoveFileBestEffort(request.outputPath);
        RemoveFileBestEffort(diagnosticsPath);
        ScopedFileCleanupGuard outputCleanup(request.outputPath);
        ScopedFileCleanupGuard diagnosticsCleanup(diagnosticsPath);

        Alloc::ScratchArena argumentArena(AssetsShaderArenaScope::s_CompilerArgumentsArena);
        ScratchVector<ScratchString> arguments(argumentArena);
        arguments.reserve(s_BaseCompilerArgumentCount + static_cast<usize>(request.includeDirectories.size()) * 2u + static_cast<usize>(request.defineCount));
        PushCompilerArgument(argumentArena, arguments, AStringView(NWB_SLANGC_EXECUTABLE));

        ScratchString sourcePathText = PathToString<char>(argumentArena, request.sourcePath);
        arguments.push_back(Move(sourcePathText));
        PushCompilerArgument(argumentArena, arguments, "-target");
        PushCompilerArgument(argumentArena, arguments, "spirv");
        PushCompilerArgument(argumentArena, arguments, "-emit-spirv-directly");
        PushCompilerArgument(argumentArena, arguments, "-profile");
        PushCompilerArgument(argumentArena, arguments, request.targetProfile);
        PushCompilerArgument(argumentArena, arguments, "-entry");
        PushCompilerArgument(argumentArena, arguments, request.entryPoint);
        PushCompilerArgument(argumentArena, arguments, "-stage");
        PushCompilerArgument(argumentArena, arguments, slangStage);
        PushCompilerArgument(argumentArena, arguments, "-warnings-disable");
        PushCompilerArgument(argumentArena, arguments, s_SuppressedCompilerWarnings);

        for(const Path& includeDirectory : request.includeDirectories){
            PushCompilerArgument(argumentArena, arguments, "-I");
            ScratchString includeDirectoryText = PathToString<char>(argumentArena, includeDirectory);
            arguments.push_back(Move(includeDirectoryText));
        }

        for(u32 i = 0; i < request.defineCount; ++i){
            const ShaderCook::ShaderMacroDefinition& define = request.defines[i];
            ScratchString defineArgument("-D", argumentArena);
            defineArgument += define.name;
            if(!define.value.empty()){
                defineArgument += '=';
                defineArgument += define.value;
            }
            arguments.push_back(Move(defineArgument));
        }

        PushCompilerArgument(argumentArena, arguments, "-o");
        ScratchString outputPathText = PathToString<char>(argumentArena, request.outputPath);
        arguments.push_back(Move(outputPathText));

        const int exitCode = RunCompilerProcessRedirected(argumentArena, arguments, diagnosticsPath);
        if(exitCode != 0){
            ScratchString diagnostics{argumentArena};
            if(ReadDiagnostics(diagnosticsPath, diagnostics) && !diagnostics.empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') :\n{}")
                    , StringConvert(request.shaderName)
                    , StringConvert(request.variantName)
                    , StringConvert(diagnostics)
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') with exit code {}")
                    , StringConvert(request.shaderName)
                    , StringConvert(request.variantName)
                    , exitCode
                );
            }
            return false;
        }

        ScratchString diagnostics{argumentArena};
        if(ReadDiagnostics(diagnosticsPath, diagnostics) && !diagnostics.empty()){
            NWB_LOGGER_WARNING(NWB_TEXT("Shader compiler emitted diagnostics for '{}' (variant '{}') :\n{}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , StringConvert(diagnostics)
            );
        }

        ErrorCode errorCode;
        if(!ReadBinaryFile(request.outputPath, outBytecode, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}' : {}")
                    , StringConvert(request.shaderName)
                    , StringConvert(request.variantName)
                    , PathToString<tchar>(request.outputPath)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : failed to read output '{}'")
                    , StringConvert(request.shaderName)
                    , StringConvert(request.variantName)
                    , PathToString<tchar>(request.outputPath)
                );
            }
            return false;
        }

        switch(ShaderBinaryPayload::ValidateBytecode(outBytecode)){
        case ShaderBinaryPayload::BytecodeValidationFailure::None:
            break;
        case ShaderBinaryPayload::BytecodeValidationFailure::InvalidSize:
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : compiled bytecode has invalid size {}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , outBytecode.size()
            );
            outBytecode.clear();
            return false;
        case ShaderBinaryPayload::BytecodeValidationFailure::InvalidMagic:
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : compiled bytecode has invalid SPIR-V magic")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
            );
            outBytecode.clear();
            return false;
        }

        outputCleanup.dismiss();
        return true;
    }
};

static Core::GlobalUniquePtr<ShaderCook::IShaderCompiler> CreateDefaultShaderCompiler(ShaderCook::CookArena& memoryArena){
    return Core::MakeGlobalUnique<SlangShaderCompiler>(memoryArena, memoryArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// metadata helpers


static ACompactString CanonicalAssetType(const Metascript::Document& doc){
    const auto assetType = doc.assetType();
    return ACompactString(AStringView(assetType.data(), assetType.size()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader source parsing - #include extraction and resolution


static constexpr AStringView s_ShaderIncludeDirective = "include";

namespace IncludeDirectiveKind{
    enum Enum : u8{
        Relative,
        Standard
    };
};

static bool IsIncludeDirectiveBoundary(const AStringView line, const usize cursor){
    return cursor >= line.size() || IsAsciiSpace(line[cursor]) || line[cursor] == '"' || line[cursor] == '<';
}

static bool ExtractIncludeDirective(const AStringView line, AStringView& outIncludeName, IncludeDirectiveKind::Enum& outKind){
    outIncludeName = {};
    outKind = IncludeDirectiveKind::Relative;

    usize cursor = 0;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;

    if(cursor >= line.size() || line[cursor] != '#')
        return false;

    ++cursor;
    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;

    if(line.substr(cursor, s_ShaderIncludeDirective.size()) != s_ShaderIncludeDirective)
        return false;
    cursor += s_ShaderIncludeDirective.size();
    if(!IsIncludeDirectiveBoundary(line, cursor))
        return false;

    while(cursor < line.size() && IsAsciiSpace(line[cursor]))
        ++cursor;

    if(cursor >= line.size())
        return false;

    char closingDelimiter = '"';
    if(line[cursor] == '"'){
        outKind = IncludeDirectiveKind::Relative;
        closingDelimiter = '"';
    }
    else if(line[cursor] == '<'){
        outKind = IncludeDirectiveKind::Standard;
        closingDelimiter = '>';
    }
    else{
        return false;
    }
    ++cursor;

    const usize closingDelimiterPos = line.find(closingDelimiter, cursor);
    if(closingDelimiterPos == AStringView::npos || closingDelimiterPos <= cursor)
        return false;

    outIncludeName = line.substr(cursor, closingDelimiterPos - cursor);
    return !outIncludeName.empty();
}

static bool ResolveIncludeFile(const AStringView includeName, const IncludeDirectiveKind::Enum kind, const Path& sourceDirectory, const ShaderCook::CookVector<Path>& includeDirectories, Path& outPath){
    ErrorCode errorCode;

    if(kind == IncludeDirectiveKind::Relative){
        const Path localCandidate = (sourceDirectory / includeName).lexically_normal();
        errorCode.clear();
        if(IsRegularFile(localCandidate, errorCode)){
            outPath = localCandidate;
            return true;
        }
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query include candidate '{}': {}")
                , PathToString<tchar>(localCandidate)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    for(const Path& includeDirectory : includeDirectories){
        const Path includeCandidate = (includeDirectory / includeName).lexically_normal();
        errorCode.clear();
        if(IsRegularFile(includeCandidate, errorCode)){
            outPath = includeCandidate;
            return true;
        }
        if(errorCode && !IsMissingPathError(errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to query include candidate '{}': {}")
                , PathToString<tchar>(includeCandidate)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Slang dependency collection


template <typename VisitedSet>
static bool CollectDependencies(const Path& startPath, const ShaderCook::CookVector<Path>& includeDirectories, VisitedSet& inOutVisitedPaths, ShaderCook::CookVector<Path>& inOutDependencies, Alloc::ScratchArena& scratchArena){
    ErrorCode errorCode;

    Deque<Path, Alloc::ScratchArena> pending{scratchArena};
    pending.push_back(startPath);
    ScratchString sourceText{scratchArena};
    Path includePath(inOutDependencies.get_allocator().arena());

    while(!pending.empty()){
        Path dependencyPath = Move(pending.back());
        pending.pop_back();

        const Path absolutePath = AbsolutePath(dependencyPath, errorCode).lexically_normal();
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency path '{}' : {}")
                , PathToString<tchar>(dependencyPath)
                , StringConvert(errorCode.message())
            );
            return false;
        }

        ScratchString canonicalPathKey = PathToString(scratchArena, absolutePath);
        CanonicalizeTextInPlace(canonicalPathKey);
        if(!inOutVisitedPaths.insert(Move(canonicalPathKey)).second)
            continue;

        sourceText.clear();
        if(!ReadTextFile(absolutePath, sourceText)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency '{}'"), PathToString<tchar>(absolutePath));
            return false;
        }
        StripUtf8Bom(sourceText);

        inOutDependencies.push_back(absolutePath);

        const AStringView sourceView(sourceText.data(), sourceText.size());
        usize lineBegin = 0;
        while(lineBegin < sourceView.size()){
            usize lineEnd = lineBegin;
            while(lineEnd < sourceView.size() && sourceView[lineEnd] != '\n')
                ++lineEnd;

            AStringView line = sourceView.substr(lineBegin, lineEnd - lineBegin);
            if(!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            AStringView includeName;
            IncludeDirectiveKind::Enum includeKind = IncludeDirectiveKind::Relative;
            if(ExtractIncludeDirective(line, includeName, includeKind)){
                if(!ResolveIncludeFile(includeName, includeKind, absolutePath.parent_path(), includeDirectories, includePath)){
                    NWB_LOGGER_ERROR(NWB_TEXT("Unable to resolve include '{}' from '{}'")
                        , StringConvert(includeName)
                        , PathToString<tchar>(absolutePath)
                    );
                    return false;
                }

                pending.push_back(Move(includePath));
            }

            lineBegin = lineEnd + 1;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// shader entry validation


template <typename DefineMap>
static bool ValidateVariantSignature(const AStringView contextLabel, const AStringView variantSignature, const DefineMap& defineValues, Alloc::ScratchArena& scratchArena){
    if(variantSignature.empty())
        return true;

    if(variantSignature == "default"){
        if(defineValues.empty())
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant 'default' is only valid when no defines are specified"), StringConvert(contextLabel));
        return false;
    }

    if(defineValues.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' requires defines to be specified")
            , StringConvert(contextLabel)
            , StringConvert(variantSignature)
        );
        return false;
    }

    using ScratchStringSet = ScratchHashSet<ScratchString>;
    ScratchStringSet seenDefines{0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena};
    seenDefines.reserve(defineValues.size());
    usize begin = 0;
    const auto logInvalidAssignment = [&](const AStringView segment){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' has invalid assignment '{}'")
            , StringConvert(contextLabel)
            , StringConvert(variantSignature)
            , StringConvert(segment)
        );
    };
    while(begin < variantSignature.size()){
        usize segmentEnd = variantSignature.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = variantSignature.size();

        const AStringView segment = TrimView(variantSignature.substr(begin, segmentEnd - begin));
        if(segment.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' has invalid empty segment")
                , StringConvert(contextLabel)
                , StringConvert(variantSignature)
            );
            return false;
        }

        const usize equalPos = segment.find('=');
        if(equalPos == AStringView::npos || equalPos == 0 || equalPos + 1 >= segment.size()){
            logInvalidAssignment(segment);
            return false;
        }

        ScratchString defineName(TrimView(segment.substr(0, equalPos)), scratchArena);
        ScratchString defineValue(TrimView(segment.substr(equalPos + 1)), scratchArena);
        if(defineName.empty() || defineValue.empty()){
            logInvalidAssignment(segment);
            return false;
        }

        CookString lookupDefineName(defineName, defineValues.get_allocator().arena());
        const auto defineIt = defineValues.find(lookupDefineName);
        if(defineIt == defineValues.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' references unknown define '{}'")
                , StringConvert(contextLabel)
                , StringConvert(variantSignature)
                , StringConvert(defineName)
            );
            return false;
        }

        bool valueFound = false;
        for(const CookString& allowedValue : defineIt.value().values){
            if(AStringView(allowedValue) == AStringView(defineValue)){
                valueFound = true;
                break;
            }
        }
        if(!valueFound){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' has unsupported value '{}' for define '{}'")
                , StringConvert(contextLabel)
                , StringConvert(variantSignature)
                , StringConvert(defineValue)
                , StringConvert(defineName)
            );
            return false;
        }

        if(!seenDefines.insert(Move(defineName)).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' assigns define '{}' more than once")
                , StringConvert(contextLabel)
                , StringConvert(variantSignature)
                , StringConvert(defineName)
            );
            return false;
        }

        begin = segmentEnd + 1;
    }

    if(seenDefines.size() != defineValues.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': variant '{}' must assign all defines")
            , StringConvert(contextLabel)
            , StringConvert(variantSignature)
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// metascript metadata parsing helpers


static bool ParseMetascriptDocument(const Path& sourceFilePath, const AStringView sourceKind, ShaderCook::CookArena& arena, Metascript::Document& outDoc){
    CookString metaText{arena};
    if(!ReadTextFile(sourceFilePath, metaText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to read {} '{}'"), StringConvert(sourceKind), PathToString<tchar>(sourceFilePath));
        return false;
    }
    StripUtf8Bom(metaText);

    if(!outDoc.parse(AStringView(metaText))){
        for(const Metascript::ParseError& err : outDoc.errors()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}' parse error at {}:{}: {}")
                , StringConvert(sourceKind)
                , PathToString<tchar>(sourceFilePath)
                , err.line
                , err.column
                , StringConvert(AStringView(err.message.data(), err.message.size()))
            );
        }
        return false;
    }
    return true;
}

static const Metascript::Value* FindAssetMapValue(const Path& nwbFilePath, const Metascript::Document& doc, const AStringView metaKind){
    const Metascript::MStringView assetVariable = doc.assetVariable();
    const Metascript::Value* asset = doc.findVariable(assetVariable);
    if(!asset){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': asset variable '{}' has no assignments")
            , StringConvert(metaKind)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(AStringView(assetVariable.data(), assetVariable.size()))
        );
        return nullptr;
    }

    if(!asset->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': asset is not a map")
            , StringConvert(metaKind)
            , PathToString<tchar>(nwbFilePath)
        );
        return nullptr;
    }

    return asset;
}

static bool ValidatePairedSourceExtension(
    const Path& nwbFilePath,
    const CookString& sourcePath,
    const AStringView expectedExtension,
    const AStringView metaKind,
    Alloc::ScratchArena& scratchArena
){
    const Path sourcePathValue(nwbFilePath.arena(), sourcePath);
    ScratchString extension = PathToString(scratchArena, sourcePathValue.extension());
    CanonicalizeTextInPlace(extension);
    if(AStringView(extension) == expectedExtension)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': paired source '{}' must use '{}' extension")
        , StringConvert(metaKind)
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(sourcePath)
        , StringConvert(expectedExtension)
    );
    return false;
}

static bool FindOptionalStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    const Metascript::Value*& outFieldValue
){
    outFieldValue = asset.findField(fieldName);
    if(!outFieldValue)
        return true;

    if(!outFieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': field '{}' must be a string")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    return true;
}

static bool ParseStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    CookString& outValue
){
    const Metascript::Value* fieldValue = nullptr;
    if(!FindOptionalStringField(nwbFilePath, asset, fieldName, fieldValue))
        return false;
    if(!fieldValue)
        return true;

    const Metascript::MStringView text = fieldValue->asString();
    const AStringView textView(text.data(), text.size());
    outValue.assign(textView.data(), textView.size());
    return true;
}

static bool ParseCompactStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    ACompactString& outValue
){
    const Metascript::Value* fieldValue = nullptr;
    if(!FindOptionalStringField(nwbFilePath, asset, fieldName, fieldValue))
        return false;
    if(!fieldValue)
        return true;

    const AStringView fieldText(fieldValue->asString().data(), fieldValue->asString().size());
    if(!outValue.assign(fieldText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': field '{}' exceeds ACompactString capacity")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    return true;
}

static bool ParseOptionalIntegerFlagField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    bool& inOutValue
){
    const Metascript::Value* fieldValue = asset.findField(fieldName);
    if(!fieldValue)
        return true;

    if(!fieldValue->isInteger()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': field '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const i64 value = fieldValue->asInteger();
    if(value != 0 && value != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': field '{}' must be 0 or 1")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    inOutValue = value != 0;
    return true;
}

static bool ParseDefines(const Path& nwbFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena, ShaderCook::CookMap<CookString, ShaderCook::DefineEntry>& outDefineValues){
    outDefineValues.clear();

    const auto* definesVal = asset.findField("defines");
    if(!definesVal)
        return true;

    if(!definesVal->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': defines must be a map"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    const auto& definesMap = definesVal->asMap();
    outDefineValues.reserve(definesMap.size());
    for(const auto& [key, val] : definesMap){
        CookString defineName(key.data(), key.size(), arena);
        if(defineName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define names must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }

        ShaderCook::CookVector<CookString> defineValues(arena);
        if(!val.copyStringList(defineValues)){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' values must be a list of strings"), PathToString<tchar>(nwbFilePath), StringConvert(defineName));
            return false;
        }
        if(defineValues.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' must provide at least one value")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(defineName)
            );
            return false;
        }
        for(const CookString& defineValue : defineValues){
            if(!defineValue.empty())
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': define '{}' values must not be empty")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(defineName)
            );
            return false;
        }

        ShaderCook::DefineEntry defineEntry(Move(defineValues));
        outDefineValues.insert_or_assign(Move(defineName), Move(defineEntry));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderCook::ShaderCook(CookArena& memoryArena, ShaderCompilerFactory compilerFactory)
    : m_memoryArena(memoryArena)
{
    const ShaderCompilerFactory createCompiler = compilerFactory ? compilerFactory : &__hidden_cook::CreateDefaultShaderCompiler;
    m_compiler = createCompiler(m_memoryArena);
}


bool ShaderCook::parseDocument(const Path& nwbFilePath, Metascript::Document& outDoc){
    return __hidden_cook::ParseMetascriptDocument(nwbFilePath, "Meta", m_memoryArena, outDoc);
}


bool ShaderCook::validateVariantSignature(
    const AStringView contextLabel,
    const AStringView variantSignature,
    const CookMap<CookString, DefineEntry>& defineValues,
    Alloc::ScratchArena& scratchArena
){
    return __hidden_cook::ValidateVariantSignature(contextLabel, variantSignature, defineValues, scratchArena);
}

bool ShaderCook::parseShaderMeta(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    ShaderEntry& outEntry,
    Alloc::ScratchArena& scratchArena
){
    outEntry = ShaderEntry(m_memoryArena);

    if(__hidden_cook::CanonicalAssetType(doc).view() != __hidden_cook::s_AssetTypeShader)
        return true;

    const Metascript::Value* assetValue = __hidden_cook::FindAssetMapValue(nwbFilePath, doc, "Shader");
    if(!assetValue)
        return false;
    const Metascript::Value& asset = *assetValue;

    if(!Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, outEntry.source))
        return false;
    if(!__hidden_cook::ValidatePairedSourceExtension(
        nwbFilePath,
        outEntry.source,
        __hidden_cook::s_SlangSourceExtension,
        "Shader",
        scratchArena
    ))
        return false;

    if(!Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        "Shader meta",
        { "stage", "target_profile", "entry_point", "include_roots", "defines", "emit_mesh_compute_shadow" }
    ))
        return false;
    if(!__hidden_cook::ParseCompactStringField(nwbFilePath, asset, "stage", outEntry.stage))
        return false;
    outEntry.archiveStage = outEntry.stage;
    if(!__hidden_cook::ParseCompactStringField(nwbFilePath, asset, "target_profile", outEntry.targetProfile))
        return false;
    if(!__hidden_cook::ParseStringField(nwbFilePath, asset, "entry_point", outEntry.entryPoint))
        return false;
    if(outEntry.entryPoint.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': entry_point must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(!__hidden_cook::ParseOptionalIntegerFlagField(nwbFilePath, asset, "emit_mesh_compute_shadow", outEntry.emitMeshComputeShadow))
        return false;

    if(const auto* includeRootsVal = asset.findField("include_roots")){
        if(!includeRootsVal->copyStringList(outEntry.includeRoots)){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': include_roots must be a list of strings"), PathToString<tchar>(nwbFilePath));
            return false;
        }
        for(const CookString& includeRoot : outEntry.includeRoots){
            if(!includeRoot.empty())
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': include_roots entries must not be empty"), PathToString<tchar>(nwbFilePath));
            return false;
        }
    }

    if(!__hidden_cook::ParseDefines(nwbFilePath, asset, m_memoryArena, outEntry.defineValues))
        return false;

    if(outEntry.stage.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': stage is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(outEntry.targetProfile.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': target_profile is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    return true;
}

bool ShaderCook::parseShaderMeta(const Path& nwbFilePath, ShaderEntry& outEntry, Alloc::ScratchArena& scratchArena){
    Metascript::Document doc(m_memoryArena);
    if(!parseDocument(nwbFilePath, doc))
        return false;

    return parseShaderMeta(nwbFilePath, doc, outEntry, scratchArena);
}

bool ShaderCook::parseIncludeMeta(
    const Path& nwbFilePath,
    const Metascript::Document& doc,
    IncludeEntry& outEntry,
    Alloc::ScratchArena& scratchArena
){
    outEntry = IncludeEntry(m_memoryArena);

    if(__hidden_cook::CanonicalAssetType(doc).view() != __hidden_cook::s_AssetTypeInclude)
        return true;

    if(!Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, outEntry.source))
        return false;
    if(!__hidden_cook::ValidatePairedSourceExtension(
        nwbFilePath,
        outEntry.source,
        __hidden_cook::s_SlangIncludeExtension,
        "Include",
        scratchArena
    ))
        return false;

    const Metascript::Value* assetValue = __hidden_cook::FindAssetMapValue(nwbFilePath, doc, "Include");
    if(!assetValue)
        return false;
    const Metascript::Value& asset = *assetValue;

    if(!Assets::ValidateMetadataAssetFields(nwbFilePath, asset, "Include meta", { "defines" }))
        return false;
    if(!__hidden_cook::ParseDefines(nwbFilePath, asset, m_memoryArena, outEntry.defineValues))
        return false;

    return true;
}

bool ShaderCook::parseIncludeMeta(const Path& nwbFilePath, IncludeEntry& outEntry, Alloc::ScratchArena& scratchArena){
    Metascript::Document doc(m_memoryArena);
    if(!parseDocument(nwbFilePath, doc))
        return false;

    return parseIncludeMeta(nwbFilePath, doc, outEntry, scratchArena);
}

void ShaderCook::mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata){
    Alloc::ScratchArena scratchArena(AssetsShaderArenaScope::s_MergeInheritedDefinesArena);
    __hidden_cook::ScratchVector<const IncludeEntry*> inheritedEntries{scratchArena};
    inheritedEntries.reserve(dependencies.size());

    usize defineReserveCount = inOutEntry.defineValues.size();
    bool canReserveDefineValues = true;
    for(const Path& dep : dependencies){
        CookString depKey = PathToString(m_memoryArena, dep);
        CanonicalizeTextInPlace(depKey);
        const auto found = includeMetadata.find(depKey);
        if(found == includeMetadata.end())
            continue;

        const IncludeEntry& includeEntry = found.value();
        inheritedEntries.push_back(&includeEntry);

        if(includeEntry.defineValues.size() > Limit<usize>::s_Max - defineReserveCount)
            canReserveDefineValues = false;
        else
            defineReserveCount += includeEntry.defineValues.size();
    }

    if(canReserveDefineValues && defineReserveCount > inOutEntry.defineValues.size())
        inOutEntry.defineValues.reserve(defineReserveCount);

    for(const IncludeEntry* includeEntry : inheritedEntries){
        for(const auto& [defineName, defineEntry] : includeEntry->defineValues){
            auto [defineIt, inserted] = inOutEntry.defineValues.try_emplace(defineName, m_memoryArena);
            if(inserted)
                defineIt.value().values.assign(defineEntry.values.begin(), defineEntry.values.end());
        }
    }
}

bool ShaderCook::gatherShaderDependencies(
    const Path& sourcePath,
    const CookVector<Path>& includeDirectories,
    CookVector<Path>& outDependencies,
    Alloc::ScratchArena& scratchArena
){
    outDependencies.clear();

    __hidden_cook::ScratchHashSet<__hidden_cook::ScratchString> visited{
        0,
        Hasher<__hidden_cook::ScratchString>(),
        EqualTo<__hidden_cook::ScratchString>(),
        scratchArena
    };
    return __hidden_cook::CollectDependencies(sourcePath, includeDirectories, visited, outDependencies, scratchArena);
}

bool ShaderCook::expandDefineCombinations(
    const CookMap<CookString, DefineEntry>& defineValues,
    CookVector<DefineCombo>& outCombinations,
    Alloc::ScratchArena& scratchArena
){
    outCombinations.clear();
    DefineCombo initialCombo{0, Hasher<CookString>(), EqualTo<CookString>(), m_memoryArena};
    initialCombo.reserve(defineValues.size());
    outCombinations.push_back(Move(initialCombo));

    auto cloneComboWithEntry = [&](const DefineCombo& source, const CookString& defineName, const CookString& defineValue){
        DefineCombo copy{0, Hasher<CookString>(), EqualTo<CookString>(), m_memoryArena};
        copy.reserve(defineValues.size());
        for(const auto& [sourceName, sourceValue] : source)
            copy.try_emplace(sourceName, sourceValue);
        copy.try_emplace(defineName, defineValue);
        return copy;
    };

    for(const auto& entry : sortedDefineEntries(defineValues, scratchArena)){
        const CookString& defineName = *entry.key;
        const CookVector<CookString>& values = entry.value->values;
        if(values.empty()){
            outCombinations.clear();
            return true;
        }

        if(values.size() == 1u){
            const CookString& value = values.front();
            for(DefineCombo& combo : outCombinations){
                combo.reserve(defineValues.size());
                combo.try_emplace(defineName, value);
            }
            continue;
        }

        if(outCombinations.size() > Limit<usize>::s_Max / values.size()){
            outCombinations.clear();
            return false;
        }

        CookVector<DefineCombo> expanded{m_memoryArena};
        expanded.reserve(outCombinations.size() * values.size());

        const usize copiedValueCount = values.size() - 1u;
        for(DefineCombo& combo : outCombinations){
            for(usize valueIndex = 0u; valueIndex < copiedValueCount; ++valueIndex)
                expanded.push_back(cloneComboWithEntry(combo, defineName, values[valueIndex]));

            combo.reserve(defineValues.size());
            combo.try_emplace(defineName, values.back());
            expanded.push_back(Move(combo));
        }

        outCombinations = Move(expanded);
    }

    return true;
}

ShaderCook::CookString ShaderCook::buildVariantName(const DefineCombo& combo, Alloc::ScratchArena& scratchArena){
    if(combo.empty())
        return CookString("default", m_memoryArena);

    if(combo.size() == 1u){
        const auto& [defineName, defineValue] = *combo.begin();
        CookString variantName{m_memoryArena};
        variantName.reserve(defineName.size() + defineValue.size() + 1u);
        variantName += defineName;
        variantName += '=';
        variantName += defineValue;
        return variantName;
    }

    const auto entries = sortedDefineEntries(combo, scratchArena);
    usize variantNameSize = entries.size() - 1u;
    for(const auto& entry : entries)
        variantNameSize += entry.key->size() + entry.value->size() + 1u;

    CookString variantName{m_memoryArena};
    variantName.reserve(variantNameSize);
    bool first = true;
    for(const auto& entry : entries){
        if(!first)
            variantName += ';';
        first = false;

        variantName += *entry.key;
        variantName += '=';
        variantName += *entry.value;
    }

    return variantName;
}

bool ShaderCook::computeDependencyChecksum(
    const CookVector<Path>& dependencies,
    const InitializerList<DependencyRootAlias> dependencyRootAliases,
    u64& outChecksum,
    Alloc::ScratchArena& scratchArena
){
    ErrorCode errorCode;
    static constexpr u8 s_NewlineByte = '\n';
    static constexpr u8 s_ZeroByte = 0;

    outChecksum = FNV64_OFFSET_BASIS;

    if(dependencyRootAliases.size() == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires at least one dependency root alias"));
        return false;
    }

    CookVector<__hidden_cook::NormalizedDependencyRootAlias> normalizedRootAliases{m_memoryArena};
    normalizedRootAliases.reserve(dependencyRootAliases.size());
    for(const DependencyRootAlias& rootAlias : dependencyRootAliases){
        if(rootAlias.root.empty() || rootAlias.key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires non-empty dependency root aliases"));
            return false;
        }

        __hidden_cook::NormalizedDependencyRootAlias normalizedAlias{m_memoryArena};
        errorCode.clear();
        normalizedAlias.root = __hidden_cook::NormalizeDependencyRootAliasPath(AbsolutePath(rootAlias.root, errorCode));
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency root alias '{}' : {}")
                , PathToString<tchar>(rootAlias.root)
                , StringConvert(errorCode.message())
            );
            return false;
        }

        normalizedAlias.key = rootAlias.key;
        CanonicalizeTextInPlace(normalizedAlias.key);
        if(normalizedAlias.key.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum requires non-empty dependency root alias keys"));
            return false;
        }
        normalizedAlias.depth = __hidden_cook::PathDepth(normalizedAlias.root);
        normalizedRootAliases.push_back(Move(normalizedAlias));
    }

    CookVector<SortedDependencyItem> sortedDependencies{m_memoryArena};
    sortedDependencies.reserve(dependencies.size());
    for(const Path& dependency : dependencies){
        SortedDependencyItem item(m_memoryArena);
        errorCode.clear();
        Path normalizedDependency = AbsolutePath(dependency, errorCode).lexically_normal();
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to resolve dependency path '{}' : {}")
                , PathToString<tchar>(dependency)
                , StringConvert(errorCode.message())
            );
            return false;
        }

        const __hidden_cook::NormalizedDependencyRootAlias* bestRootAlias = nullptr;
        for(const __hidden_cook::NormalizedDependencyRootAlias& rootAlias : normalizedRootAliases){
            if(normalizedDependency != rootAlias.root && !PathHasDirectoryAncestor(normalizedDependency, rootAlias.root))
                continue;
            if(!bestRootAlias || rootAlias.depth > bestRootAlias->depth)
                bestRootAlias = &rootAlias;
        }
        if(!bestRootAlias){
            NWB_LOGGER_ERROR(NWB_TEXT("Dependency checksum path '{}' is outside the declared dependency root aliases")
                , PathToString<tchar>(dependency)
            );
            return false;
        }

        __hidden_cook::ScratchString relativePathText = PathToString(scratchArena, normalizedDependency.lexically_relative(bestRootAlias->root));
        CanonicalizeTextInPlace(relativePathText);

        item.canonicalPath = bestRootAlias->key;
        if(!relativePathText.empty()){
            item.canonicalPath += '/';
            item.canonicalPath += relativePathText;
        }
        item.path = dependency;
        sortedDependencies.push_back(Move(item));
    }

    Sort(sortedDependencies.begin(), sortedDependencies.end(), [](const SortedDependencyItem& lhs, const SortedDependencyItem& rhs){ return lhs.canonicalPath < rhs.canonicalPath; });

    Vector<u8, Alloc::ScratchArena> dependencyBytes{scratchArena};
    for(const SortedDependencyItem& item : sortedDependencies){
        outChecksum = UpdateFnv64TextExact(outChecksum, AStringView(item.canonicalPath));
        outChecksum = UpdateFnv64(outChecksum, &s_NewlineByte, 1);

        dependencyBytes.clear();
        errorCode.clear();
        if(!ReadBinaryFile(item.path, dependencyBytes, errorCode)){
            if(errorCode){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}' : {}")
                    , PathToString<tchar>(item.path)
                    , StringConvert(errorCode.message())
                );
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to read dependency file '{}'"), PathToString<tchar>(item.path));
            }
            return false;
        }
        if(!dependencyBytes.empty()){
            outChecksum = UpdateFnv64(
                outChecksum,
                dependencyBytes.data(),
                dependencyBytes.size()
            );
        }

        outChecksum = UpdateFnv64(outChecksum, &s_ZeroByte, 1);
    }

    return true;
}

bool ShaderCook::computeSourceChecksum(
    const ShaderEntry& entry,
    const AStringView variantSignature,
    const u64 dependencyChecksum,
    u64& outChecksum,
    Alloc::ScratchArena& scratchArena
){
    static constexpr AStringView s_ChecksumVersionTag = "shader-source-v1";
    const u8 newlineByte = '\n';

    outChecksum = FNV64_OFFSET_BASIS;

    const auto appendChecksumLine = [&outChecksum, &newlineByte](const AStringView text){
        outChecksum = UpdateFnv64TextExact(outChecksum, text);
        outChecksum = UpdateFnv64(outChecksum, &newlineByte, 1);
    };

    appendChecksumLine(s_ChecksumVersionTag);
    appendChecksumLine(AStringView(entry.name));
    appendChecksumLine(entry.stage.view());
    appendChecksumLine(entry.archiveStage.view());
    appendChecksumLine(entry.targetProfile.view());
    appendChecksumLine(AStringView(entry.entryPoint));
    appendChecksumLine(variantSignature);
    if(entry.implicitDefines.size() <= 1u){
        for(const auto& [defineName, defineValue] : entry.implicitDefines){
            appendChecksumLine(defineName);
            appendChecksumLine(defineValue);
        }
    }
    else{
        for(const auto& entryDefine : sortedDefineEntries(entry.implicitDefines, scratchArena)){
            appendChecksumLine(*entryDefine.key);
            appendChecksumLine(*entryDefine.value);
        }
    }
    outChecksum = UpdateFnv64(outChecksum, reinterpret_cast<const u8*>(&dependencyChecksum), sizeof(dependencyChecksum));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

