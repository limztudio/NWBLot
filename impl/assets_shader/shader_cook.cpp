// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_cook.h"

#include <cstdlib>

#include <core/assets/asset_paths.h>
#include <core/metascript/parser.h>

#include <core/common/log.h>
#include <global/hash_utils.h>


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


namespace __hidden_shader_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// common cook aliases and asset type keywords


using CookString = ShaderCook::CookString;
template<typename T>
using CookVector = ShaderCook::CookVector<T>;
template<typename T, typename V>
using CookMap = ShaderCook::CookMap<T, V>;
template<typename T>
using CookHashSet = ShaderCook::CookHashSet<T>;
using ScratchString = AString<Alloc::ScratchArena<>>;
template<typename T>
using ScratchVector = Vector<T, Alloc::ScratchArena<>>;
template<typename T>
using ScratchHashSet = HashSet<T, Hasher<T>, EqualTo<T>, Alloc::ScratchArena<>>;

static constexpr AStringView s_AssetTypeShader = "shader";
static constexpr AStringView s_AssetTypeInclude = "include";
static constexpr AStringView s_AssetTypeMaterialBind = "material_bind";
static constexpr AStringView s_SlangSourceExtension = ".slang";
static constexpr AStringView s_SlangIncludeExtension = ".slangi";
static constexpr AStringView s_MaterialBindExtension = ".bind";


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
        { "mesh", "mesh" }
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

static void AppendShellQuoted(CookString& inOutCommand, const AStringView value){
#if defined(_WIN32)
    inOutCommand += '"';
    for(const char ch : value){
        if(ch == '"' || ch == '\\')
            inOutCommand += '\\';
        inOutCommand += ch;
    }
    inOutCommand += '"';
#else
    inOutCommand += '\'';
    for(const char ch : value){
        if(ch == '\'')
            inOutCommand += "'\\''";
        else
            inOutCommand += ch;
    }
    inOutCommand += '\'';
#endif
}

static void AppendCommandArgument(CookString& inOutCommand, const AStringView value){
    inOutCommand += ' ';
    AppendShellQuoted(inOutCommand, value);
}

static void AppendCommandPathArgument(ShaderCook::CookArena& arena, CookString& inOutCommand, const Path& path){
    const CookString pathText = PathToString(arena, path);
    AppendCommandArgument(inOutCommand, pathText);
}

static bool ReadDiagnostics(const Path& diagnosticsPath, CookString& outDiagnostics){
    outDiagnostics.clear();

    ErrorCode errorCode;
    if(!FileExists(diagnosticsPath, errorCode) || errorCode)
        return false;

    return ReadTextFile(diagnosticsPath, outDiagnostics);
}

static void RemoveFileBestEffort(const Path& path){
    ErrorCode errorCode;
    if(FileExists(path, errorCode) && !errorCode)
        static_cast<void>(RemoveFile(path, errorCode));
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

        CookString command{m_memoryArena};
        AppendShellQuoted(command, AStringView(NWB_SLANGC_EXECUTABLE));
        AppendCommandPathArgument(m_memoryArena, command, request.sourcePath);
        command += " -target spirv";
        command += " -emit-spirv-directly";
        command += " -profile";
        AppendCommandArgument(command, request.targetProfile);
        command += " -entry";
        AppendCommandArgument(command, request.entryPoint);
        command += " -stage";
        AppendCommandArgument(command, slangStage);

        for(const Path& includeDirectory : request.includeDirectories){
            command += " -I";
            AppendCommandPathArgument(m_memoryArena, command, includeDirectory);
        }

        for(u32 i = 0; i < request.defineCount; ++i){
            const ShaderCook::ShaderMacroDefinition& define = request.defines[i];
            CookString defineArgument("-D", m_memoryArena);
            defineArgument += define.name;
            if(!define.value.empty()){
                defineArgument += '=';
                defineArgument += define.value;
            }
            AppendCommandArgument(command, defineArgument);
        }

        command += " -o";
        AppendCommandPathArgument(m_memoryArena, command, request.outputPath);
        command += " >";
        AppendCommandPathArgument(m_memoryArena, command, diagnosticsPath);
        command += " 2>&1";

        const int exitCode = std::system(command.c_str());
        if(exitCode != 0){
            CookString diagnostics{m_memoryArena};
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

        if(outBytecode.empty() || (outBytecode.size() & 3u) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Shader compile failed for '{}' (variant '{}') : compiled bytecode has invalid size {}")
                , StringConvert(request.shaderName)
                , StringConvert(request.variantName)
                , outBytecode.size()
            );
            outBytecode.clear();
            return false;
        }

        RemoveFileBestEffort(diagnosticsPath);
        return true;
    }
};

static Core::GlobalUniquePtr<ShaderCook::IShaderCompiler> CreateDefaultShaderCompiler(ShaderCook::CookArena& memoryArena){
    return Core::MakeGlobalUnique<SlangShaderCompiler>(memoryArena, memoryArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// metadata helpers


static CompactString CanonicalAssetType(const Metascript::Document& doc){
    const auto assetType = doc.assetType();
    return CompactString(AStringView(assetType.data(), assetType.size()));
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
    return
        cursor >= line.size()
        || IsAsciiSpace(line[cursor])
        || line[cursor] == '"'
        || line[cursor] == '<'
    ;
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
        const Path localCandidate = (sourceDirectory / Path(includeName)).lexically_normal();
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
        const Path includeCandidate = (includeDirectory / Path(includeName)).lexically_normal();
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


template <typename VisitedSet, typename ScratchArena>
static bool CollectDependencies(const Path& startPath, const ShaderCook::CookVector<Path>& includeDirectories, VisitedSet& inOutVisitedPaths, ShaderCook::CookVector<Path>& inOutDependencies, ScratchArena& scratchArena){
    ErrorCode errorCode;

    Deque<Path, Alloc::ScratchArena<>> pending{scratchArena};
    pending.push_back(startPath);
    ScratchString sourceText{scratchArena};
    Path includePath;

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


template <typename DefineMap, typename ScratchArena>
static bool ValidateDefaultVariant(const AStringView contextLabel, const AStringView defaultVariant, const DefineMap& defineValues, ScratchArena& scratchArena){
    if(defaultVariant.empty())
        return true;

    if(defaultVariant == "default"){
        if(defineValues.empty())
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default variant 'default' is only valid when no defines are specified"), StringConvert(contextLabel));
        return false;
    }

    if(defineValues.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default variant '{}' requires defines to be specified")
            , StringConvert(contextLabel)
            , StringConvert(defaultVariant)
        );
        return false;
    }

    using ScratchStringSet = ScratchHashSet<ScratchString>;
    ScratchStringSet seenDefines{0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena};
    seenDefines.reserve(defineValues.size());
    usize begin = 0;
    const auto logInvalidAssignment = [&](const AStringView segment){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default variant '{}' has invalid assignment '{}'")
            , StringConvert(contextLabel)
            , StringConvert(defaultVariant)
            , StringConvert(segment)
        );
    };
    while(begin < defaultVariant.size()){
        usize segmentEnd = defaultVariant.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = defaultVariant.size();

        const AStringView segment = TrimView(defaultVariant.substr(begin, segmentEnd - begin));
        if(segment.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default variant '{}' has invalid empty segment")
                , StringConvert(contextLabel)
                , StringConvert(defaultVariant)
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
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default variant '{}' references unknown define '{}'")
                , StringConvert(contextLabel)
                , StringConvert(defaultVariant)
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
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default variant '{}' has unsupported value '{}' for define '{}'")
                , StringConvert(contextLabel)
                , StringConvert(defaultVariant)
                , StringConvert(defineValue)
                , StringConvert(defineName)
            );
            return false;
        }

        if(!seenDefines.insert(Move(defineName)).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default variant '{}' assigns define '{}' more than once")
                , StringConvert(contextLabel)
                , StringConvert(defaultVariant)
                , StringConvert(defineName)
            );
            return false;
        }

        begin = segmentEnd + 1;
    }

    if(seenDefines.size() != defineValues.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default variant '{}' must assign all defines")
            , StringConvert(contextLabel)
            , StringConvert(defaultVariant)
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

static bool ParseNwbDocument(const Path& nwbFilePath, ShaderCook::CookArena& arena, Metascript::Document& outDoc){
    return ParseMetascriptDocument(nwbFilePath, "Meta", arena, outDoc);
}

static bool ParseMaterialBindDocument(const Path& bindFilePath, ShaderCook::CookArena& arena, Metascript::Document& outDoc){
    return ParseMetascriptDocument(bindFilePath, "Bind", arena, outDoc);
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
    const AStringView metaKind
){
    Alloc::ScratchArena<> scratchArena;
    ScratchString extension = PathToString(scratchArena, Path(sourcePath).extension());
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

static bool ParseDefaultVariant(const Path& nwbFilePath, const Metascript::Value& asset, CookString& outDefaultVariant){
    outDefaultVariant.clear();

    const auto* defaultVariantVal = asset.findField("default_variant");
    if(!defaultVariantVal)
        return true;

    if(defaultVariantVal->isList()){
        const auto& list = defaultVariantVal->asList();
        usize defaultVariantSize = list.empty() ? 0u : list.size() - 1u;
        for(usize i = 0; i < list.size(); ++i){
            if(!list[i].isString()){
                NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default_variant list elements must be strings"), PathToString<tchar>(nwbFilePath));
                return false;
            }
            defaultVariantSize += list[i].asString().size();
        }

        outDefaultVariant.reserve(defaultVariantSize);
        for(usize i = 0; i < list.size(); ++i){
            if(i > 0)
                outDefaultVariant += ';';
            const Metascript::MStringView variantText = list[i].asString();
            outDefaultVariant.append(variantText.data(), variantText.size());
        }
    }
    else if(defaultVariantVal->isString()){
        const Metascript::MStringView variantText = defaultVariantVal->asString();
        outDefaultVariant.assign(variantText.data(), variantText.size());
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': default_variant must be a string or list of strings"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    return true;
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
    CookString& outValue,
    const bool canonicalize
){
    const Metascript::Value* fieldValue = nullptr;
    if(!FindOptionalStringField(nwbFilePath, asset, fieldName, fieldValue))
        return false;
    if(!fieldValue)
        return true;

    const Metascript::MStringView text = fieldValue->asString();
    const AStringView textView(text.data(), text.size());
    outValue.assign(textView.data(), textView.size());
    if(canonicalize)
        CanonicalizeTextInPlace(outValue);
    return true;
}

static bool ParseCompactStringField(
    const Path& nwbFilePath,
    const Metascript::Value& asset,
    const AStringView fieldName,
    CompactString& outValue
){
    const Metascript::Value* fieldValue = nullptr;
    if(!FindOptionalStringField(nwbFilePath, asset, fieldName, fieldValue))
        return false;
    if(!fieldValue)
        return true;

    const AStringView fieldText(fieldValue->asString().data(), fieldValue->asString().size());
    if(!outValue.assign(fieldText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': field '{}' exceeds CompactString capacity")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

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
// material bind parsing helpers


static bool IsMaterialBindIdentifier(const AStringView text){
    if(text.empty())
        return false;

    const auto isAlpha = [](const char ch){
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    };
    const auto isDigit = [](const char ch){
        return ch >= '0' && ch <= '9';
    };
    const auto isIdentifierChar = [&](const char ch){
        return isAlpha(ch) || isDigit(ch) || ch == '_';
    };

    if(!isAlpha(text[0]) && text[0] != '_')
        return false;

    for(usize i = 1u; i < text.size(); ++i){
        if(!isIdentifierChar(text[i]))
            return false;
    }
    return true;
}

struct MaterialBindFieldTypeInfo{
    AStringView lookupFunctionName;
};

static bool TryParseMaterialBindFieldType(
    const AStringView typeName,
    MaterialBindFieldTypeInfo& outInfo
){
    outInfo = {};
    u32 componentCount = 0u;

    const auto matchesScalarOrVector = [typeName, &componentCount](const AStringView scalarName){
        if(typeName == scalarName){
            componentCount = 1u;
            return true;
        }
        if(typeName.size() != scalarName.size() + 1u)
            return false;
        if(typeName.substr(0u, scalarName.size()) != scalarName)
            return false;

        const char suffix = typeName[scalarName.size()];
        if(suffix < '2' || suffix > '4')
            return false;

        componentCount = static_cast<u32>(suffix - '0');
        return true;
    };

    static constexpr AStringView s_FloatLookupNames[] = {
        "nwbMaterialFindFloat",
        "nwbMaterialFindFloat2",
        "nwbMaterialFindFloat3",
        "nwbMaterialFindFloat4"
    };
    static constexpr AStringView s_IntLookupNames[] = {
        "nwbMaterialFindInt",
        "nwbMaterialFindInt2",
        "nwbMaterialFindInt3",
        "nwbMaterialFindInt4"
    };
    static constexpr AStringView s_UIntLookupNames[] = {
        "nwbMaterialFindUInt",
        "nwbMaterialFindUInt2",
        "nwbMaterialFindUInt3",
        "nwbMaterialFindUInt4"
    };
    static constexpr AStringView s_BoolLookupNames[] = {
        "nwbMaterialFindBool",
        "nwbMaterialFindBool2",
        "nwbMaterialFindBool3",
        "nwbMaterialFindBool4"
    };

    const auto matchWithLookupNames = [&](const AStringView scalarName, const AStringView* lookupNames){
        if(!matchesScalarOrVector(scalarName))
            return false;
        outInfo.lookupFunctionName = lookupNames[componentCount - 1u];
        return true;
    };

    return
        matchWithLookupNames("float", s_FloatLookupNames)
        || matchWithLookupNames("int", s_IntLookupNames)
        || matchWithLookupNames("uint", s_UIntLookupNames)
        || matchWithLookupNames("bool", s_BoolLookupNames)
    ;
}

static bool IsMaterialBindBlockClassAttribute(const AStringView attributeName){
    return attributeName == "material_constant" || attributeName == "material_mutable";
}

static bool ParseMaterialBindStringField(
    const Path& bindFilePath,
    const Metascript::Value& map,
    const AStringView fieldName,
    const AStringView contextLabel,
    CookString& outValue
){
    outValue.clear();

    const Metascript::Value* value = map.findField(fieldName);
    if(!value || !value->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} field '{}' must be a string")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(contextLabel)
            , StringConvert(fieldName)
        );
        return false;
    }

    const Metascript::MStringView text = value->asString();
    outValue.assign(text.data(), text.size());
    return true;
}

static bool ParseMaterialBindAttributeList(
    const Path& bindFilePath,
    const Metascript::Value* attributesValue,
    const AStringView contextLabel,
    ShaderCook::CookArena& arena,
    ShaderCook::CookVector<ShaderCook::MaterialBindAttribute>& outAttributes
){
    outAttributes.clear();
    if(!attributesValue)
        return true;

    if(!attributesValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attributes must be a list")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(contextLabel)
        );
        return false;
    }

    const auto& attributeList = attributesValue->asList();
    outAttributes.reserve(attributeList.size());
    for(const Metascript::Value& attributeValue : attributeList){
        if(!attributeValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': {} attribute entries must be maps")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(contextLabel)
            );
            return false;
        }

        ShaderCook::MaterialBindAttribute attribute(arena);
        if(!ParseMaterialBindStringField(bindFilePath, attributeValue, "name", contextLabel, attribute.name))
            return false;
        if(!IsMaterialBindIdentifier(attribute.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid attribute name '{}' in {}")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(attribute.name)
                , StringConvert(contextLabel)
            );
            return false;
        }

        const Metascript::Value* argumentsValue = attributeValue.findField("arguments");
        if(argumentsValue){
            if(!argumentsValue->isList()){
                NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be a list")
                    , PathToString<tchar>(bindFilePath)
                    , StringConvert(attribute.name)
                );
                return false;
            }

            const auto& argumentList = argumentsValue->asList();
            attribute.arguments.reserve(argumentList.size());
            for(const Metascript::Value& argumentValue : argumentList){
                if(!argumentValue.isString()){
                    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': attribute '{}' arguments must be strings")
                        , PathToString<tchar>(bindFilePath)
                        , StringConvert(attribute.name)
                    );
                    return false;
                }

                const Metascript::MStringView argumentText = argumentValue.asString();
                attribute.arguments.emplace_back(argumentText.data(), argumentText.size(), arena);
            }
        }

        outAttributes.push_back(Move(attribute));
    }

    return true;
}

static bool ValidateMaterialBindStructAttributes(
    const Path& bindFilePath,
    const ShaderCook::MaterialBindStruct& bindStruct
){
    bool foundBlockClass = false;

    for(const ShaderCook::MaterialBindAttribute& attribute : bindStruct.attributes){
        if(!IsMaterialBindBlockClassAttribute(attribute.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' has unsupported attribute '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(!attribute.arguments.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' block class attribute '{}' must not have arguments")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(foundBlockClass){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' declares more than one block class attribute")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
            );
            return false;
        }

        foundBlockClass = true;
    }

    if(foundBlockClass)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must declare a material block class attribute")
        , PathToString<tchar>(bindFilePath)
        , StringConvert(bindStruct.name)
    );
    return false;
}

static bool ValidateMaterialBindFieldAttributes(const Path& bindFilePath, const ShaderCook::MaterialBindStruct& bindStruct, const ShaderCook::MaterialBindField& field){
    bool foundDefault = false;

    for(const ShaderCook::MaterialBindAttribute& attribute : field.attributes){
        if(attribute.name != "default"){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has unsupported attribute '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(attribute.name)
            );
            return false;
        }
        if(foundDefault){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' declares default more than once")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }
        if(attribute.arguments.size() != 1u || attribute.arguments[0].empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' default attribute requires one non-empty string argument")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }

        foundDefault = true;
    }

    if(foundDefault)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' must declare a default attribute")
        , PathToString<tchar>(bindFilePath)
        , StringConvert(bindStruct.name)
        , StringConvert(field.name)
    );
    return false;
}

static bool ParseMaterialBindField(
    const Path& bindFilePath,
    const Metascript::Value& fieldValue,
    const ShaderCook::MaterialBindStruct& bindStruct,
    ShaderCook::CookArena& arena,
    ShaderCook::MaterialBindField& outField
){
    if(!fieldValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' field entries must be maps")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
        );
        return false;
    }

    if(!ParseMaterialBindStringField(bindFilePath, fieldValue, "type", bindStruct.name, outField.type))
        return false;
    if(!ParseMaterialBindStringField(bindFilePath, fieldValue, "name", bindStruct.name, outField.name))
        return false;
    MaterialBindFieldTypeInfo fieldType;
    if(!IsMaterialBindIdentifier(outField.type) || !TryParseMaterialBindFieldType(outField.type, fieldType)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has unsupported type '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(outField.name)
            , StringConvert(outField.type)
        );
        return false;
    }
    if(!IsMaterialBindIdentifier(outField.name)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': field '{}.{}' has invalid name")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(bindStruct.name)
            , StringConvert(outField.name)
        );
        return false;
    }
    if(!ParseMaterialBindAttributeList(bindFilePath, fieldValue.findField("attributes"), outField.name, arena, outField.attributes))
        return false;
    if(!ValidateMaterialBindFieldAttributes(bindFilePath, bindStruct, outField))
        return false;

    return true;
}

static bool ParseMaterialBindStruct(
    const Path& bindFilePath,
    const Metascript::MStringView structName,
    const Metascript::Value& structValue,
    ShaderCook::CookArena& arena,
    ShaderCook::MaterialBindStruct& outStruct
){
    if(!structValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must be a map")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(AStringView(structName.data(), structName.size()))
        );
        return false;
    }

    outStruct.name.assign(structName.data(), structName.size());
    if(!IsMaterialBindIdentifier(outStruct.name)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid struct name '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(outStruct.name)
        );
        return false;
    }
    if(!ParseMaterialBindAttributeList(bindFilePath, structValue.findField("attributes"), outStruct.name, arena, outStruct.attributes))
        return false;
    if(!ValidateMaterialBindStructAttributes(bindFilePath, outStruct))
        return false;

    const Metascript::Value* fieldsValue = structValue.findField("fields");
    if(!fieldsValue || !fieldsValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' fields must be a list")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(outStruct.name)
        );
        return false;
    }
    if(fieldsValue->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': struct '{}' must declare at least one field")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(outStruct.name)
        );
        return false;
    }

    outStruct.fields.reserve(fieldsValue->asList().size());
    for(const Metascript::Value& fieldValue : fieldsValue->asList()){
        ShaderCook::MaterialBindField field(arena);
        if(!ParseMaterialBindField(bindFilePath, fieldValue, outStruct, arena, field))
            return false;

        if(outStruct.findField(AStringView(field.name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': duplicate field '{}.{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(outStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }

        outStruct.fields.push_back(Move(field));
    }

    return true;
}

static bool ParseMaterialBindStructs(const Path& bindFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena, ShaderCook::CookVector<ShaderCook::MaterialBindStruct>& outStructs){
    outStructs.clear();

    const Metascript::Value* structsValue = asset.findField("structs");
    if(!structsValue || !structsValue->isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.structs must be a map"), PathToString<tchar>(bindFilePath));
        return false;
    }

    const auto& structsMap = structsValue->asMap();
    if(structsMap.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.structs must not be empty"), PathToString<tchar>(bindFilePath));
        return false;
    }

    outStructs.reserve(structsMap.size());
    for(const auto& [structName, structValue] : structsMap){
        ShaderCook::MaterialBindStruct bindStruct(arena);
        if(!ParseMaterialBindStruct(bindFilePath, Metascript::MStringView(structName.data(), structName.size()), structValue, arena, bindStruct))
            return false;
        outStructs.push_back(Move(bindStruct));
    }

    Sort(outStructs.begin(), outStructs.end(), [](const ShaderCook::MaterialBindStruct& lhs, const ShaderCook::MaterialBindStruct& rhs){
        return lhs.name < rhs.name;
    });
    return true;
}

static bool ParseMaterialBindInstances(const Path& bindFilePath, const Metascript::Value& asset, ShaderCook::CookArena& arena, ShaderCook::MaterialBindEntry& outEntry){
    outEntry.instances.clear();

    const Metascript::Value* instancesValue = asset.findField("instances");
    if(!instancesValue || !instancesValue->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.instances must be a list"), PathToString<tchar>(bindFilePath));
        return false;
    }
    if(instancesValue->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.instances must not be empty"), PathToString<tchar>(bindFilePath));
        return false;
    }

    outEntry.instances.reserve(instancesValue->asList().size());
    for(const Metascript::Value& instanceValue : instancesValue->asList()){
        if(!instanceValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset.instances entries must be maps"), PathToString<tchar>(bindFilePath));
            return false;
        }

        ShaderCook::MaterialBindInstance instance(arena);
        if(!ParseMaterialBindStringField(bindFilePath, instanceValue, "type", "instance", instance.type))
            return false;
        if(!ParseMaterialBindStringField(bindFilePath, instanceValue, "name", "instance", instance.name))
            return false;
        if(!IsMaterialBindIdentifier(instance.type) || !outEntry.findStruct(AStringView(instance.type))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': instance '{}' references unknown struct type '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
            );
            return false;
        }
        if(!IsMaterialBindIdentifier(instance.name)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': invalid instance name '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(instance.name)
            );
            return false;
        }
        if(outEntry.findInstance(AStringView(instance.name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': duplicate instance name '{}'")
                , PathToString<tchar>(bindFilePath)
                , StringConvert(instance.name)
            );
            return false;
        }

        outEntry.instances.push_back(Move(instance));
    }

    return true;
}

static bool ParseMaterialBindSource(const Path& bindFilePath, const Metascript::Document& doc, ShaderCook::CookArena& arena, ShaderCook::MaterialBindEntry& outEntry){
    outEntry.reset();

    if(CanonicalAssetType(doc).view() != s_AssetTypeMaterialBind){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind '{}': asset type must be '{}'")
            , PathToString<tchar>(bindFilePath)
            , StringConvert(s_AssetTypeMaterialBind)
        );
        return false;
    }

    outEntry.source = PathToString(arena, bindFilePath);
    if(!ValidatePairedSourceExtension(bindFilePath, outEntry.source, s_MaterialBindExtension, "Material bind"))
        return false;

    const Metascript::Value* assetValue = FindAssetMapValue(bindFilePath, doc, "Material bind");
    if(!assetValue)
        return false;

    if(!ParseMaterialBindStructs(bindFilePath, *assetValue, arena, outEntry.structs))
        return false;
    if(!ParseMaterialBindInstances(bindFilePath, *assetValue, arena, outEntry))
        return false;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// material bind generated Slang include helpers


static char ToGeneratedAsciiUpper(const char ch){
    return (ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - ('a' - 'A')) : ch;
}

static bool IsGeneratedAlphaNumeric(const char ch){
    return
        (ch >= 'A' && ch <= 'Z')
        || (ch >= 'a' && ch <= 'z')
        || (ch >= '0' && ch <= '9')
    ;
}

static void AppendGeneratedUpperIdentifier(const AStringView text, CookString& inOutText){
    const usize beginSize = inOutText.size();
    for(const char ch : text)
        inOutText += IsGeneratedAlphaNumeric(ch) ? ToGeneratedAsciiUpper(ch) : '_';
    if(inOutText.size() == beginSize)
        inOutText += "VALUE";
}

static void AppendGeneratedPascalIdentifier(const AStringView text, CookString& inOutText){
    const usize beginSize = inOutText.size();
    bool upperNext = true;
    for(const char ch : text){
        if(ch == '_'){
            upperNext = true;
            continue;
        }

        if(upperNext)
            inOutText += ToGeneratedAsciiUpper(ch);
        else
            inOutText += ch;
        upperNext = false;
    }
    if(inOutText.size() == beginSize)
        inOutText += "Value";
}

static void AppendHexU32Slang(const u32 value, CookString& inOutText){
    static constexpr char s_HexDigits[] = "0123456789abcdef";
    inOutText += "0x";
    for(u32 nibbleIndex = 0u; nibbleIndex < 8u; ++nibbleIndex){
        const u32 shift = (7u - nibbleIndex) * 4u;
        inOutText += s_HexDigits[(value >> shift) & 0xfu];
    }
    inOutText += 'u';
}

static CookString BuildMaterialBindIncludeVirtualPath(ShaderCook::CookArena& arena, const ShaderCook::MaterialBindEntry& entry){
    CookString includePath(entry.virtualPath, arena);
    includePath += s_MaterialBindExtension;
    return includePath;
}

static CookString BuildMaterialBindIncludeGuard(ShaderCook::CookArena& arena, const AStringView includePath){
    CookString guard("NWB_GENERATED_MATERIAL_BIND_", arena);
    AppendGeneratedUpperIdentifier(AStringView(includePath), guard);
    return guard;
}

static CookString BuildMaterialBindFieldSymbol(
    ShaderCook::CookArena& arena,
    const ShaderCook::MaterialBindInstance& instance,
    const ShaderCook::MaterialBindField& field,
    const AStringView suffix
){
    CookString symbol("NWB_MATERIAL_BIND_", arena);
    AppendGeneratedUpperIdentifier(AStringView(instance.name), symbol);
    symbol += '_';
    AppendGeneratedUpperIdentifier(AStringView(field.name), symbol);
    symbol += suffix;
    return symbol;
}

static CookString BuildMaterialBindFieldAccessorName(
    ShaderCook::CookArena& arena,
    const ShaderCook::MaterialBindInstance& instance,
    const ShaderCook::MaterialBindField& field
){
    CookString functionName("nwbMaterialBindLoad", arena);
    AppendGeneratedPascalIdentifier(AStringView(instance.name), functionName);
    AppendGeneratedPascalIdentifier(AStringView(field.name), functionName);
    return functionName;
}

static CookString BuildMaterialBindBlockAccessorName(ShaderCook::CookArena& arena, const ShaderCook::MaterialBindInstance& instance){
    CookString functionName("nwbMaterialBindLoad", arena);
    AppendGeneratedPascalIdentifier(AStringView(instance.name), functionName);
    return functionName;
}

static bool RegisterGeneratedMaterialBindSymbol(
    const AStringView includePath,
    const AStringView symbol,
    ScratchHashSet<ScratchString>& inOutSymbols,
    Alloc::ScratchArena<>& scratchArena
){
    ScratchString scratchSymbol(symbol, scratchArena);
    if(inOutSymbols.insert(Move(scratchSymbol)).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': generated symbol '{}' is ambiguous")
        , StringConvert(includePath)
        , StringConvert(symbol)
    );
    return false;
}

static AStringView MaterialBindFieldDefaultAttribute(const ShaderCook::MaterialBindField& field){
    const ShaderCook::MaterialBindAttribute* attribute = field.findAttribute("default");
    return (attribute && attribute->arguments.size() == 1u) ? AStringView(attribute->arguments[0]) : AStringView();
}

static bool AppendMaterialBindFieldConstants(
    ShaderCook::CookArena& arena,
    const AStringView includePath,
    const ShaderCook::MaterialBindStruct& bindStruct,
    const ShaderCook::MaterialBindInstance& instance,
    const ShaderCook::MaterialBindField& field,
    const CookString& keySymbol,
    const CookString& defaultSymbol,
    CookString& inOutSource
){
    const AStringView defaultAttribute = MaterialBindFieldDefaultAttribute(field);
    if(defaultAttribute.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' must declare a default attribute")
            , StringConvert(includePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
        );
        return false;
    }

    CookString keyText(instance.name, arena);
    keyText += '.';
    keyText += field.name;
    const u64 keyHash = UpdateFnv64TextCanonical(FNV64_OFFSET_BASIS, AStringView(keyText));

    inOutSource += "static const uint2 ";
    inOutSource += keySymbol;
    inOutSource += " = uint2(";
    AppendHexU32Slang(static_cast<u32>(keyHash & 0xffffffffull), inOutSource);
    inOutSource += ", ";
    AppendHexU32Slang(static_cast<u32>(keyHash >> 32u), inOutSource);
    inOutSource += ");\n";

    inOutSource += "static const ";
    inOutSource += field.type;
    inOutSource += ' ';
    inOutSource += defaultSymbol;
    inOutSource += " = ";
    inOutSource += defaultAttribute;
    inOutSource += ";\n";
    return true;
}

static void AppendMaterialBindFieldAccessor(
    const ShaderCook::MaterialBindField& field,
    const CookString& keySymbol,
    const CookString& defaultSymbol,
    const CookString& functionName,
    const AStringView lookupFunctionName,
    CookString& inOutSource
){
    inOutSource += field.type;
    inOutSource += ' ';
    inOutSource += functionName;
    inOutSource += "(const NwbMeshInstanceData instance){\n";
    inOutSource += "    return ";
    inOutSource += lookupFunctionName;
    inOutSource += "(instance, ";
    inOutSource += keySymbol;
    inOutSource += ", ";
    inOutSource += defaultSymbol;
    inOutSource += ");\n";
    inOutSource += "}\n\n";
    inOutSource += field.type;
    inOutSource += ' ';
    inOutSource += functionName;
    inOutSource += "(){\n";
    inOutSource += "    return ";
    inOutSource += functionName;
    inOutSource += "(nwbMeshLoadInstance());\n";
    inOutSource += "}\n";
}

static bool AppendMaterialBindGeneratedInstance(
    ShaderCook::CookArena& arena,
    const AStringView includePath,
    const ShaderCook::MaterialBindInstance& instance,
    const ShaderCook::MaterialBindStruct& bindStruct,
    ScratchHashSet<ScratchString>& inOutSymbols,
    Alloc::ScratchArena<>& scratchArena,
    CookString& inOutSource
){
    inOutSource += "\n";
    inOutSource += "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n\n\n";

    for(const ShaderCook::MaterialBindField& field : bindStruct.fields){
        MaterialBindFieldTypeInfo fieldType;
        if(!TryParseMaterialBindFieldType(field.type, fieldType)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' has unsupported type '{}'")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(field.type)
            );
            return false;
        }

        const CookString keySymbol = BuildMaterialBindFieldSymbol(arena, instance, field, "_KEY");
        const CookString defaultSymbol = BuildMaterialBindFieldSymbol(arena, instance, field, "_DEFAULT");
        const CookString functionName = BuildMaterialBindFieldAccessorName(arena, instance, field);
        if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(keySymbol), inOutSymbols, scratchArena))
            return false;
        if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(defaultSymbol), inOutSymbols, scratchArena))
            return false;
        if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(functionName), inOutSymbols, scratchArena))
            return false;

        if(!AppendMaterialBindFieldConstants(arena, includePath, bindStruct, instance, field, keySymbol, defaultSymbol, inOutSource))
            return false;
        inOutSource += '\n';
        AppendMaterialBindFieldAccessor(field, keySymbol, defaultSymbol, functionName, fieldType.lookupFunctionName, inOutSource);
        inOutSource += '\n';
    }

    const CookString blockFunctionName = BuildMaterialBindBlockAccessorName(arena, instance);
    if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(blockFunctionName), inOutSymbols, scratchArena))
        return false;

    inOutSource += bindStruct.name;
    inOutSource += ' ';
    inOutSource += blockFunctionName;
    inOutSource += "(const NwbMeshInstanceData instance){\n";
    inOutSource += "    ";
    inOutSource += bindStruct.name;
    inOutSource += " value;\n";
    for(const ShaderCook::MaterialBindField& field : bindStruct.fields){
        const CookString functionName = BuildMaterialBindFieldAccessorName(arena, instance, field);
        inOutSource += "    value.";
        inOutSource += field.name;
        inOutSource += " = ";
        inOutSource += functionName;
        inOutSource += "(instance);\n";
    }
    inOutSource += "    return value;\n";
    inOutSource += "}\n\n";
    inOutSource += bindStruct.name;
    inOutSource += ' ';
    inOutSource += blockFunctionName;
    inOutSource += "(){\n";
    inOutSource += "    return ";
    inOutSource += blockFunctionName;
    inOutSource += "(nwbMeshLoadInstance());\n";
    inOutSource += "}\n";

    return true;
}

static bool BuildMaterialBindIncludeSource(ShaderCook::CookArena& arena, const ShaderCook::MaterialBindEntry& entry, CookString& outSource){
    outSource.clear();
    if(entry.virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation failed: virtual path is empty for '{}'"), StringConvert(entry.source));
        return false;
    }

    const CookString includePath = BuildMaterialBindIncludeVirtualPath(arena, entry);
    const CookString includeGuard = BuildMaterialBindIncludeGuard(arena, AStringView(includePath));

    Alloc::ScratchArena<> scratchArena;
    ScratchHashSet<ScratchString> generatedSymbols{
        0,
        Hasher<ScratchString>(),
        EqualTo<ScratchString>(),
        scratchArena
    };

    outSource += "// generated by NWBLot graphics asset cook\n";
    outSource += "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n\n\n";
    outSource += "#ifndef ";
    outSource += includeGuard;
    outSource += "\n#define ";
    outSource += includeGuard;
    outSource += "\n\n\n";
    outSource += "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n\n\n";

    for(const ShaderCook::MaterialBindStruct& bindStruct : entry.structs){
        if(!RegisterGeneratedMaterialBindSymbol(AStringView(includePath), AStringView(bindStruct.name), generatedSymbols, scratchArena))
            return false;

        outSource += "struct ";
        outSource += bindStruct.name;
        outSource += "{\n";
        for(const ShaderCook::MaterialBindField& field : bindStruct.fields){
            outSource += "    ";
            outSource += field.type;
            outSource += ' ';
            outSource += field.name;
            outSource += ";\n";
        }
        outSource += "};\n\n";
    }

    for(const ShaderCook::MaterialBindInstance& instance : entry.instances){
        const ShaderCook::MaterialBindStruct* bindStruct = entry.findStruct(AStringView(instance.type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' references unknown struct type '{}'")
                , StringConvert(includePath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
            );
            return false;
        }

        if(!AppendMaterialBindGeneratedInstance(arena, AStringView(includePath), instance, *bindStruct, generatedSymbols, scratchArena, outSource))
            return false;
    }

    outSource += "\n";
    outSource += "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n\n\n";
    outSource += "#endif\n\n\n";
    outSource += "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\n";
    return true;
}

static bool SplitMaterialBindParameterName(const AStringView parameterName, AStringView& outInstanceName, AStringView& outFieldName){
    const usize separatorIndex = parameterName.find('.');
    if(separatorIndex == AStringView::npos || separatorIndex == 0u || separatorIndex + 1u >= parameterName.size())
        return false;

    outInstanceName = parameterName.substr(0u, separatorIndex);
    outFieldName = parameterName.substr(separatorIndex + 1u);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderCook::ShaderCook(CookArena& memoryArena, ShaderCompilerFactory compilerFactory)
    : m_memoryArena(memoryArena)
{
    const ShaderCompilerFactory createCompiler = compilerFactory ? compilerFactory : &__hidden_shader_cook::CreateDefaultShaderCompiler;
    m_compiler = createCompiler(m_memoryArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const ShaderCook::MaterialBindStruct* ShaderCook::MaterialBindEntry::findStruct(const AStringView typeName)const{
    for(const ShaderCook::MaterialBindStruct& bindStruct : structs){
        if(AStringView(bindStruct.name) == typeName)
            return &bindStruct;
    }
    return nullptr;
}

const ShaderCook::MaterialBindAttribute* ShaderCook::MaterialBindField::findAttribute(const AStringView attributeName)const{
    for(const ShaderCook::MaterialBindAttribute& attribute : attributes){
        if(AStringView(attribute.name) == attributeName)
            return &attribute;
    }
    return nullptr;
}

const ShaderCook::MaterialBindField* ShaderCook::MaterialBindStruct::findField(const AStringView fieldName)const{
    for(const ShaderCook::MaterialBindField& field : fields){
        if(AStringView(field.name) == fieldName)
            return &field;
    }
    return nullptr;
}

const ShaderCook::MaterialBindInstance* ShaderCook::MaterialBindEntry::findInstance(const AStringView instanceName)const{
    for(const ShaderCook::MaterialBindInstance& instance : instances){
        if(AStringView(instance.name) == instanceName)
            return &instance;
    }
    return nullptr;
}

bool ShaderCook::MaterialBindEntry::declaresParameter(const AStringView parameterName)const{
    AStringView instanceName;
    AStringView fieldName;
    if(!__hidden_shader_cook::SplitMaterialBindParameterName(parameterName, instanceName, fieldName))
        return false;

    const ShaderCook::MaterialBindInstance* instance = findInstance(instanceName);
    if(!instance)
        return false;

    const ShaderCook::MaterialBindStruct* bindStruct = findStruct(AStringView(instance->type));
    return bindStruct && bindStruct->findField(fieldName) != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShaderCook::parseDocument(const Path& nwbFilePath, Metascript::Document& outDoc){
    return __hidden_shader_cook::ParseNwbDocument(nwbFilePath, m_memoryArena, outDoc);
}

bool ShaderCook::parseMaterialBindSource(const Path& bindFilePath, MaterialBindEntry& outEntry){
    outEntry.reset();

    Metascript::Document doc(m_memoryArena);
    if(!__hidden_shader_cook::ParseMaterialBindDocument(bindFilePath, m_memoryArena, doc))
        return false;

    return __hidden_shader_cook::ParseMaterialBindSource(bindFilePath, doc, m_memoryArena, outEntry);
}

bool ShaderCook::buildMaterialBindIncludeSource(const MaterialBindEntry& entry, CookString& outSource){
    return __hidden_shader_cook::BuildMaterialBindIncludeSource(m_memoryArena, entry, outSource);
}

bool ShaderCook::validateDefaultVariant(const AStringView contextLabel, const AStringView defaultVariant, const CookMap<CookString, DefineEntry>& defineValues){
    Alloc::ScratchArena<> validationArena;
    return __hidden_shader_cook::ValidateDefaultVariant(contextLabel, defaultVariant, defineValues, validationArena);
}

bool ShaderCook::parseShaderMeta(const Path& nwbFilePath, const Metascript::Document& doc, ShaderEntry& outEntry){
    outEntry = ShaderEntry(m_memoryArena);

    if(__hidden_shader_cook::CanonicalAssetType(doc).view() != __hidden_shader_cook::s_AssetTypeShader)
        return true;

    const Metascript::Value* assetValue = __hidden_shader_cook::FindAssetMapValue(nwbFilePath, doc, "Shader");
    if(!assetValue)
        return false;
    const Metascript::Value& asset = *assetValue;

    if(!Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, outEntry.source))
        return false;
    if(!__hidden_shader_cook::ValidatePairedSourceExtension(nwbFilePath, outEntry.source, __hidden_shader_cook::s_SlangSourceExtension, "Shader"))
        return false;

    if(!Assets::RejectVirtualPathOverrideField(nwbFilePath, asset, "Shader"))
        return false;
    if(!__hidden_shader_cook::ParseCompactStringField(nwbFilePath, asset, "stage", outEntry.stage))
        return false;
    outEntry.archiveStage = outEntry.stage;
    if(!__hidden_shader_cook::ParseCompactStringField(nwbFilePath, asset, "target_profile", outEntry.targetProfile))
        return false;
    if(!__hidden_shader_cook::ParseStringField(nwbFilePath, asset, "entry_point", outEntry.entryPoint, false))
        return false;
    if(outEntry.entryPoint.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': entry_point must not be empty"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    if(!__hidden_shader_cook::ParseDefaultVariant(nwbFilePath, asset, outEntry.defaultVariant))
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

    if(!__hidden_shader_cook::ParseDefines(nwbFilePath, asset, m_memoryArena, outEntry.defineValues))
        return false;

    if(outEntry.stage.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': stage is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }
    if(outEntry.targetProfile.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': target_profile is required"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    const CookString contextLabel = PathToString(m_memoryArena, nwbFilePath);
    if(!validateDefaultVariant(contextLabel, outEntry.defaultVariant, outEntry.defineValues))
        return false;
    if(!canonicalizeVariantSignature(outEntry.defaultVariant, outEntry.defaultVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("Shader meta '{}': failed to canonicalize default_variant"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    return true;
}

bool ShaderCook::parseShaderMeta(const Path& nwbFilePath, ShaderEntry& outEntry){
    Metascript::Document doc(m_memoryArena);
    if(!parseDocument(nwbFilePath, doc))
        return false;

    return parseShaderMeta(nwbFilePath, doc, outEntry);
}

bool ShaderCook::parseIncludeMeta(const Path& nwbFilePath, const Metascript::Document& doc, IncludeEntry& outEntry){
    outEntry = IncludeEntry(m_memoryArena);

    if(__hidden_shader_cook::CanonicalAssetType(doc).view() != __hidden_shader_cook::s_AssetTypeInclude)
        return true;

    if(!Assets::ResolvePairedSourcePathFromMetadata(nwbFilePath, outEntry.source))
        return false;
    if(!__hidden_shader_cook::ValidatePairedSourceExtension(nwbFilePath, outEntry.source, __hidden_shader_cook::s_SlangIncludeExtension, "Include"))
        return false;

    const Metascript::Value* assetValue = __hidden_shader_cook::FindAssetMapValue(nwbFilePath, doc, "Include");
    if(!assetValue)
        return false;
    const Metascript::Value& asset = *assetValue;

    if(!Assets::RejectVirtualPathOverrideField(nwbFilePath, asset, "Include"))
        return false;
    if(!__hidden_shader_cook::ParseDefaultVariant(nwbFilePath, asset, outEntry.defaultVariant))
        return false;
    if(!__hidden_shader_cook::ParseDefines(nwbFilePath, asset, m_memoryArena, outEntry.defineValues))
        return false;

    const CookString contextLabel = PathToString(m_memoryArena, nwbFilePath);
    if(!validateDefaultVariant(contextLabel, outEntry.defaultVariant, outEntry.defineValues))
        return false;
    if(!canonicalizeVariantSignature(outEntry.defaultVariant, outEntry.defaultVariant)){
        NWB_LOGGER_ERROR(NWB_TEXT("Include meta '{}': failed to canonicalize default_variant"), PathToString<tchar>(nwbFilePath));
        return false;
    }

    return true;
}

bool ShaderCook::parseIncludeMeta(const Path& nwbFilePath, IncludeEntry& outEntry){
    Metascript::Document doc(m_memoryArena);
    if(!parseDocument(nwbFilePath, doc))
        return false;

    return parseIncludeMeta(nwbFilePath, doc, outEntry);
}

void ShaderCook::mergeInheritedDefines(ShaderEntry& inOutEntry, const CookVector<Path>& dependencies, const CookMap<CookString, IncludeEntry>& includeMetadata){
    for(const Path& dep : dependencies){
        CookString depKey = PathToString(m_memoryArena, dep);
        CanonicalizeTextInPlace(depKey);
        const auto found = includeMetadata.find(depKey);
        if(found == includeMetadata.end())
            continue;

        const IncludeEntry& includeEntry = found.value();

        if(includeEntry.defineValues.size() <= Limit<usize>::s_Max - inOutEntry.defineValues.size())
            inOutEntry.defineValues.reserve(inOutEntry.defineValues.size() + includeEntry.defineValues.size());

        for(const auto& [defineName, defineEntry] : includeEntry.defineValues){
            auto [defineIt, inserted] = inOutEntry.defineValues.try_emplace(defineName, m_memoryArena);
            if(inserted)
                defineIt.value().values.assign(defineEntry.values.begin(), defineEntry.values.end());
        }

        if(!includeEntry.defaultVariant.empty()){
            if(inOutEntry.defaultVariant.empty())
                inOutEntry.defaultVariant = includeEntry.defaultVariant;
            else{
                const usize includeVariantSize = includeEntry.defaultVariant.size();
                const usize currentVariantSize = inOutEntry.defaultVariant.size();

                CookString mergedDefaultVariant{m_memoryArena};
                if(includeVariantSize < Limit<usize>::s_Max && includeVariantSize + 1u <= Limit<usize>::s_Max - currentVariantSize)
                    mergedDefaultVariant.reserve(includeVariantSize + 1u + currentVariantSize);
                mergedDefaultVariant += includeEntry.defaultVariant;
                mergedDefaultVariant += ';';
                mergedDefaultVariant += inOutEntry.defaultVariant;
                inOutEntry.defaultVariant = Move(mergedDefaultVariant);
            }
        }
    }
}

bool ShaderCook::gatherShaderDependencies(const Path& sourcePath, const CookVector<Path>& includeDirectories, CookVector<Path>& outDependencies){
    Alloc::ScratchArena<> scratchArena;

    outDependencies.clear();

    __hidden_shader_cook::ScratchHashSet<__hidden_shader_cook::ScratchString> visited{
        0,
        Hasher<__hidden_shader_cook::ScratchString>(),
        EqualTo<__hidden_shader_cook::ScratchString>(),
        scratchArena
    };
    return __hidden_shader_cook::CollectDependencies(sourcePath, includeDirectories, visited, outDependencies, scratchArena);
}

bool ShaderCook::expandDefineCombinations(const CookMap<CookString, DefineEntry>& defineValues, CookVector<DefineCombo>& outCombinations){
    Alloc::ScratchArena<> scratchArena;

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

ShaderCook::CookString ShaderCook::buildVariantName(const DefineCombo& combo){
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

    Alloc::ScratchArena<> scratchArena;
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

bool ShaderCook::canonicalizeVariantSignature(const AStringView variantSignature, CookString& outCanonical){
    const AStringView trimmedSignatureView = TrimView(variantSignature);
    if(trimmedSignatureView.empty()){
        outCanonical.clear();
        return true;
    }
    if(trimmedSignatureView == "default"){
        outCanonical = "default";
        return true;
    }

    const auto fail = [&outCanonical](){
        outCanonical.clear();
        return false;
    };

    Alloc::ScratchArena<> scratchArena;

    using ScratchString = __hidden_shader_cook::ScratchString;
    using ScratchDefineCombo = HashMap<ScratchString, ScratchString, Hasher<ScratchString>, EqualTo<ScratchString>, Alloc::ScratchArena<>>;
    ScratchDefineCombo assignments(
        0,
        Hasher<ScratchString>(),
        EqualTo<ScratchString>(),
        scratchArena
    );
    usize assignmentReserve = 1u;
    for(const char ch : trimmedSignatureView){
        if(ch == ';')
            ++assignmentReserve;
    }
    assignments.reserve(assignmentReserve);

    usize begin = 0;
    while(begin < trimmedSignatureView.size()){
        usize segmentEnd = trimmedSignatureView.find(';', begin);
        if(segmentEnd == AStringView::npos)
            segmentEnd = trimmedSignatureView.size();

        const AStringView segment = TrimView(trimmedSignatureView.substr(begin, segmentEnd - begin));
        if(segment.empty())
            return fail();

        const usize equalPos = segment.find('=');
        if(equalPos == AStringView::npos || equalPos == 0 || equalPos + 1 >= segment.size())
            return fail();

        ScratchString defineName(TrimView(segment.substr(0, equalPos)), scratchArena);
        ScratchString defineValue(TrimView(segment.substr(equalPos + 1)), scratchArena);
        if(defineName.empty() || defineValue.empty())
            return fail();
        if(!assignments.emplace(Move(defineName), Move(defineValue)).second)
            return fail();

        begin = segmentEnd + 1;
    }

    if(assignments.size() == 1u){
        const auto& [defineName, defineValue] = *assignments.begin();
        outCanonical.clear();
        outCanonical.reserve(defineName.size() + defineValue.size() + 1u);
        outCanonical += defineName;
        outCanonical += '=';
        outCanonical += defineValue;
        return true;
    }

    const auto entries = sortedDefineEntries(assignments, scratchArena);
    usize variantNameSize = entries.size() - 1u;
    for(const auto& entry : entries)
        variantNameSize += entry.key->size() + entry.value->size() + 1u;

    outCanonical.clear();
    outCanonical.reserve(variantNameSize);
    bool first = true;
    for(const auto& entry : entries){
        if(!first)
            outCanonical += ';';
        first = false;

        outCanonical += *entry.key;
        outCanonical += '=';
        outCanonical += *entry.value;
    }

    return true;
}

bool ShaderCook::computeDependencyChecksum(const CookVector<Path>& dependencies, u64& outChecksum){
    ErrorCode errorCode;
    static constexpr u8 s_NewlineByte = '\n';
    static constexpr u8 s_ZeroByte = 0;
    Alloc::ScratchArena<> scratchArena;

    outChecksum = FNV64_OFFSET_BASIS;

    CookVector<SortedDependencyItem> sortedDependencies{m_memoryArena};
    sortedDependencies.reserve(dependencies.size());
    for(const Path& dependency : dependencies){
        SortedDependencyItem item(m_memoryArena);
        item.canonicalPath = PathToString(m_memoryArena, dependency);
        CanonicalizeTextInPlace(item.canonicalPath);
        item.path = dependency;
        sortedDependencies.push_back(Move(item));
    }

    Sort(sortedDependencies.begin(), sortedDependencies.end(), [](const SortedDependencyItem& lhs, const SortedDependencyItem& rhs){ return lhs.canonicalPath < rhs.canonicalPath; });

    Vector<u8, Alloc::ScratchArena<>> dependencyBytes{scratchArena};
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

bool ShaderCook::computeSourceChecksum(const ShaderEntry& entry, const AStringView variantSignature, const u64 dependencyChecksum, u64& outChecksum){
    static constexpr AStringView s_ChecksumVersionTag = "shader-source-v3";
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
        Alloc::ScratchArena<> scratchArena;
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

