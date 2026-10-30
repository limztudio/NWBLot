// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compile.h"

#include <core/common/log.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compile{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendDepfileEscapedPath(const Core::Metascript::MStringView pathText, Core::Metascript::MString& out){
    for(const Core::Metascript::MChar c : pathText){
        if(c == ' ' || c == '#')
            out.push_back('\\');
        else if(c == '$')
            out.push_back('$');
        out.push_back(c);
    }
}

[[nodiscard]] static Core::Metascript::MString DeriveVirtualCompilePath(const Path<Core::Metascript::MetaArena>& inputPath, const Core::Metascript::MStringView rawInputView, const Span<const Core::Metascript::MString> includeDirs, Core::Metascript::MetaArena& arena){
    using namespace Core::Metascript;

    const MString genericInput = PathToGenericString<MChar>(arena, inputPath);
    const MStringView genericView(genericInput.data(), genericInput.size());

    MStringView bestRelView;
    usize bestPrefixLen = 0u;
    for(const MString& dirText : includeDirs){
        const Path<MetaArena> incPath(arena, AStringView(dirText.data(), dirText.size()));
        MString genericInc = PathToGenericString<MChar>(arena, incPath);
        if(!genericInc.empty() && genericInc.back() == '/')
            genericInc.pop_back();
        if(genericInc.empty() || genericView.size() <= genericInc.size() + 1u)
            continue;
        if(genericView.substr(0u, genericInc.size()) == MStringView(genericInc.data(), genericInc.size()) && genericView[genericInc.size()] == '/' && genericInc.size() > bestPrefixLen){
            bestPrefixLen = genericInc.size();
            bestRelView = genericView.substr(genericInc.size() + 1u);
        }
    }

    if(!bestRelView.empty())
        return MString(bestRelView.data(), bestRelView.size(), arena);
    return MString(rawInputView.data(), rawInputView.size(), arena);
}


class FilesystemIncludeResolver final : public Core::Metascript::IIncludeResolver{
public:
    FilesystemIncludeResolver(const Path<Core::Metascript::MetaArena>& currentFileDir, const Span<const Core::Metascript::MString> includeDirs, Core::Metascript::MetaArena& arena)
        : m_resolvedParentDirs(arena)
        , m_includeDirs(includeDirs)
    {
        m_resolvedParentDirs.push_back(currentFileDir);
    }


public:
    [[nodiscard]] virtual b8 resolveInclude(const Core::Metascript::MStringView includePath, const b8 isAngleBracket, Core::Metascript::MetaArena& arena, Core::Metascript::MString& outResolvedKey, Core::Metascript::MString& outContent)override{
        using namespace Core::Metascript;

        if(!Adhd::HasAdhdExtension(includePath))
            return false;
        const MString relPath(includePath.data(), includePath.size(), arena);

        const auto tryResolveFromBase = [&](const Path<MetaArena>& baseDir) -> b8{
            const auto candidate = ResolveAbsolutePath(arena, baseDir, MStringView(relPath.data(), relPath.size()));
            if(!candidate || !ReadTextFile(*candidate, outContent))
                return false;
            outResolvedKey = PathToGenericString<MChar>(arena, *candidate);
            m_resolvedParentDirs.push_back(candidate->parentPath());
            return true;
        };

        if(!isAngleBracket){
            for(usize i = m_resolvedParentDirs.size(); i > 0u; --i){
                if(tryResolveFromBase(m_resolvedParentDirs[i - 1u]))
                    return true;
            }
        }

        for(const MString& dirText : m_includeDirs){
            const Path<MetaArena> baseDir(arena, AStringView(dirText.data(), dirText.size()));
            if(tryResolveFromBase(baseDir))
                return true;
        }

        return false;
    }

    virtual void popIncludeScope()noexcept override{
        if(m_resolvedParentDirs.size() > 1u)
            m_resolvedParentDirs.pop_back();
    }


private:
    Core::Metascript::MVector<Path<Core::Metascript::MetaArena>> m_resolvedParentDirs;
    Span<const Core::Metascript::MString> m_includeDirs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CompileAdhd(const AdhdCompileOptions& options){
    using namespace Core::Metascript;

    MetaArena& arena = options.inputs.get_allocator().arena();
    const Path<MetaArena> outDir(arena, AStringView(options.outputDirectory.data(), options.outputDirectory.size()));
    if(!options.validateOnly && !EnsureDirectories(outDir)){
        NWB_LOGGER_ERROR(NWB_TEXT("AdhdCompiler: failed to create output directory '{}'"), ConvertString(AStringView(options.outputDirectory.data(), options.outputDirectory.size())));
        return false;
    }

    const MStringView customNs(options.customNamespace.data(), options.customNamespace.size());
    MString combinedDepContent(arena);

    for(const MString& inputPathText : options.inputs){
        const MStringView inputView(inputPathText.data(), inputPathText.size());
        const Path<MetaArena> inputPath(arena, AStringView(inputView.data(), inputView.size()));

        MString sourceContent(arena);
        if(!ReadTextFile(inputPath, sourceContent)){
            NWB_LOGGER_ERROR(NWB_TEXT("AdhdCompiler: failed to read input file '{}'"), ConvertString(AStringView(inputView.data(), inputView.size())));
            return false;
        }

        const Path<MetaArena> parentDir = inputPath.parentPath();
        __hidden_compile::FilesystemIncludeResolver resolver(parentDir, options.includeDirectories, arena);
        const MString virtualPath = __hidden_compile::DeriveVirtualCompilePath(inputPath, inputView, options.includeDirectories, arena);

        Adhd::CompileResult result(arena);
        if(!Adhd::Compiler::Compile(MStringView(sourceContent.data(), sourceContent.size()), MStringView(virtualPath.data(), virtualPath.size()), arena, result, &resolver, customNs, options.macroDefinitions)){
            for(const ParseError& err : result.errors)
                NWB_LOGGER_ERROR(NWB_TEXT("{}:{}:{}: error: {}"), ConvertString(AStringView(inputView.data(), inputView.size())), err.line, err.column, ConvertString(AStringView(err.message.data(), err.message.size())));
            return false;
        }

        if(options.validateOnly)
            continue;

        const MString headerRel = Adhd::Compiler::DeriveHeaderFilename(inputView, arena);
        const MString sourceRel = Adhd::Compiler::DeriveSourceFilename(inputView, arena);

        const Path<MetaArena> headerOutPath = outDir / Path<MetaArena>(arena, AStringView(headerRel.data(), headerRel.size()));
        const Path<MetaArena> sourceOutPath = outDir / Path<MetaArena>(arena, AStringView(sourceRel.data(), sourceRel.size()));

        if(!WriteTextFile(headerOutPath, AStringView(result.generatedHeader.data(), result.generatedHeader.size())) || !WriteTextFile(sourceOutPath, AStringView(result.generatedSource.data(), result.generatedSource.size()))){
            NWB_LOGGER_ERROR(NWB_TEXT("AdhdCompiler: failed to write generated output for '{}'"), ConvertString(AStringView(inputView.data(), inputView.size())));
            return false;
        }

        if(!options.depfilePath.empty()){
            const MString headerGeneric = PathToGenericString<MChar>(arena, headerOutPath);
            const MString sourceGeneric = PathToGenericString<MChar>(arena, sourceOutPath);
            const MString inputGeneric = PathToGenericString<MChar>(arena, inputPath);

            __hidden_compile::AppendDepfileEscapedPath(MStringView(headerGeneric.data(), headerGeneric.size()), combinedDepContent);
            combinedDepContent.push_back(' ');
            __hidden_compile::AppendDepfileEscapedPath(MStringView(sourceGeneric.data(), sourceGeneric.size()), combinedDepContent);
            combinedDepContent.append(": ");
            __hidden_compile::AppendDepfileEscapedPath(MStringView(inputGeneric.data(), inputGeneric.size()), combinedDepContent);
            for(const MString& depPath : result.unit.resolvedIncludedFiles){
                combinedDepContent.push_back(' ');
                __hidden_compile::AppendDepfileEscapedPath(MStringView(depPath.data(), depPath.size()), combinedDepContent);
            }
            combinedDepContent.push_back('\n');
        }
    }

    if(!options.validateOnly && !options.depfilePath.empty()){
        const Path<MetaArena> depfileOutPath(arena, AStringView(options.depfilePath.data(), options.depfilePath.size()));
        if(!EnsureDirectories(depfileOutPath.parentPath()) || !WriteTextFile(depfileOutPath, AStringView(combinedDepContent.data(), combinedDepContent.size()))){
            NWB_LOGGER_ERROR(NWB_TEXT("AdhdCompiler: failed to write depfile '{}'"), ConvertString(AStringView(options.depfilePath.data(), options.depfilePath.size())));
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

