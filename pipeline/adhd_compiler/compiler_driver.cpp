// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_driver.h"

#include <core/common/log.h>
#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_driver{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendDepfileEscapedPath(NWB::Core::Metascript::MString& out, const NWB::Core::Metascript::MStringView pathText){
    for(const NWB::Core::Metascript::MChar c : pathText){
        if(c == ' ' || c == '#')
            out.push_back('\\');
        else if(c == '$')
            out.push_back('$');
        out.push_back(c);
    }
}

[[nodiscard]] NWB::Core::Metascript::MString DeriveVirtualCompilePath(
    NWB::Core::Metascript::MetaArena& arena,
    const Path<NWB::Core::Metascript::MetaArena>& inputPath,
    const NWB::Core::Metascript::MStringView rawInputView,
    const NWB::Core::Metascript::MVector<NWB::Core::Metascript::MString>& includeDirs
){
    using namespace NWB::Core::Metascript;

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
        if(genericView.substr(0u, genericInc.size()) == MStringView(genericInc.data(), genericInc.size())
            && genericView[genericInc.size()] == '/'
            && genericInc.size() > bestPrefixLen)
        {
            bestPrefixLen = genericInc.size();
            bestRelView = genericView.substr(genericInc.size() + 1u);
        }
    }

    if(!bestRelView.empty())
        return MString(bestRelView.data(), bestRelView.size(), arena);
    return MString(rawInputView.data(), rawInputView.size(), arena);
}


class FilesystemIncludeResolver final : public NWB::Core::Metascript::IIncludeResolver{
public:
    FilesystemIncludeResolver(
        NWB::Core::Metascript::MetaArena& arena,
        const Path<NWB::Core::Metascript::MetaArena>& currentFileDir,
        const NWB::Core::Metascript::MVector<NWB::Core::Metascript::MString>& includeDirs
    )
        : m_resolvedParentDirs(arena)
        , m_includeDirs(includeDirs)
    {
        m_resolvedParentDirs.push_back(currentFileDir);
    }


public:
    [[nodiscard]] b8 resolveInclude(
        const NWB::Core::Metascript::MStringView includePath,
        const b8 isAngleBracket,
        NWB::Core::Metascript::MetaArena& arena,
        NWB::Core::Metascript::MString& outResolvedKey,
        NWB::Core::Metascript::MString& outContent
    )override{
        using namespace NWB::Core::Metascript;

        const b8 hasAdhdExt = includePath.size() > 5u && includePath.substr(includePath.size() - 5u) == ".adhd";
        if(!hasAdhdExt)
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

    void popIncludeScope()noexcept override{
        if(m_resolvedParentDirs.size() > 1u)
            m_resolvedParentDirs.pop_back();
    }


private:
    NWB::Core::Metascript::MVector<Path<NWB::Core::Metascript::MetaArena>> m_resolvedParentDirs;
    const NWB::Core::Metascript::MVector<NWB::Core::Metascript::MString>& m_includeDirs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RunAdhdCompilerDriver(
    const AdhdCompilerOptions& options,
    NWB::Core::Metascript::MetaArena& arena
){
    using namespace NWB::Core::Metascript;

    const Path<MetaArena> outDir(arena, AStringView(options.outputDirectory.data(), options.outputDirectory.size()));
    if(!options.validateOnly && !EnsureDirectories(outDir)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("adhd_compiler: failed to create output directory '{}'"),
            ConvertString(AStringView(options.outputDirectory.data(), options.outputDirectory.size()))
        );
        return false;
    }

    const MStringView customNs(options.customNamespace.data(), options.customNamespace.size());
    MString combinedDepContent(arena);

    for(const MString& inputPathText : options.inputFiles){
        const MStringView inputView(inputPathText.data(), inputPathText.size());
        const Path<MetaArena> inputPath(arena, AStringView(inputView.data(), inputView.size()));

        MString sourceContent(arena);
        if(!ReadTextFile(inputPath, sourceContent)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("adhd_compiler: failed to read input file '{}'"),
                ConvertString(AStringView(inputView.data(), inputView.size()))
            );
            return false;
        }

        const Path<MetaArena> parentDir = inputPath.parentPath();
        __hidden_adhd_compiler_driver::FilesystemIncludeResolver resolver(arena, parentDir, options.includeDirectories);
        const MString virtualPath = __hidden_adhd_compiler_driver::DeriveVirtualCompilePath(
            arena,
            inputPath,
            inputView,
            options.includeDirectories
        );

        Adhd::CompileResult result(arena);
        if(!Adhd::Compiler::compile(
            MStringView(sourceContent.data(), sourceContent.size()),
            MStringView(virtualPath.data(), virtualPath.size()),
            arena,
            result,
            &resolver,
            customNs,
            &options.macroDefinitions
        )){
            for(const ParseError& err : result.errors){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("{}:{}:{}: error: {}"),
                    ConvertString(AStringView(inputView.data(), inputView.size())),
                    err.line,
                    err.column,
                    ConvertString(AStringView(err.message.data(), err.message.size()))
                );
            }
            return false;
        }

        if(options.validateOnly)
            continue;

        const MStringView stem = Adhd::ExtractAdhdFileStem(inputView);

        MString headerRel(stem.data(), stem.size(), arena);
        headerRel.append(".adhd.h");
        MString sourceRel(stem.data(), stem.size(), arena);
        sourceRel.append(".adhd.cpp");

        const Path<MetaArena> headerOutPath = outDir / Path<MetaArena>(arena, AStringView(headerRel.data(), headerRel.size()));
        const Path<MetaArena> sourceOutPath = outDir / Path<MetaArena>(arena, AStringView(sourceRel.data(), sourceRel.size()));

        if(!WriteTextFile(headerOutPath, AStringView(result.generatedHeader.data(), result.generatedHeader.size()))
            || !WriteTextFile(sourceOutPath, AStringView(result.generatedSource.data(), result.generatedSource.size())))
        {
            NWB_LOGGER_ERROR(
                NWB_TEXT("adhd_compiler: failed to write generated output for '{}'"),
                ConvertString(AStringView(inputView.data(), inputView.size()))
            );
            return false;
        }

        if(!options.depfilePath.empty()){
            const MString headerGeneric = PathToGenericString<MChar>(arena, headerOutPath);
            const MString sourceGeneric = PathToGenericString<MChar>(arena, sourceOutPath);
            const MString inputGeneric = PathToGenericString<MChar>(arena, inputPath);

            __hidden_adhd_compiler_driver::AppendDepfileEscapedPath(combinedDepContent, MStringView(headerGeneric.data(), headerGeneric.size()));
            combinedDepContent.push_back(' ');
            __hidden_adhd_compiler_driver::AppendDepfileEscapedPath(combinedDepContent, MStringView(sourceGeneric.data(), sourceGeneric.size()));
            combinedDepContent.append(": ");
            __hidden_adhd_compiler_driver::AppendDepfileEscapedPath(combinedDepContent, MStringView(inputGeneric.data(), inputGeneric.size()));
            for(const MString& depPath : result.unit.resolvedIncludedFiles){
                combinedDepContent.push_back(' ');
                __hidden_adhd_compiler_driver::AppendDepfileEscapedPath(combinedDepContent, MStringView(depPath.data(), depPath.size()));
            }
            combinedDepContent.push_back('\n');
        }
    }

    if(!options.validateOnly && !options.depfilePath.empty()){
        const Path<MetaArena> depfileOutPath(arena, AStringView(options.depfilePath.data(), options.depfilePath.size()));
        if(!EnsureDirectories(depfileOutPath.parentPath())
            || !WriteTextFile(depfileOutPath, AStringView(combinedDepContent.data(), combinedDepContent.size())))
        {
            NWB_LOGGER_ERROR(
                NWB_TEXT("adhd_compiler: failed to write depfile '{}'"),
                ConvertString(AStringView(options.depfilePath.data(), options.depfilePath.size()))
            );
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

