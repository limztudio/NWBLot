// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_driver.h"

#include <core/common/log.h>
#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_adhd_compiler_driver{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    [[nodiscard]] bool resolveInclude(
        const NWB::Core::Metascript::MStringView includePath,
        const bool isAngleBracket,
        NWB::Core::Metascript::MetaArena& arena,
        NWB::Core::Metascript::MString& outResolvedKey,
        NWB::Core::Metascript::MString& outContent
    )override{
        using namespace NWB::Core::Metascript;

        if(includePath.size() <= 5u || includePath.substr(includePath.size() - 5u) != ".adhd")
            return false;
        const MString relPath(includePath.data(), includePath.size(), arena);

        if(!isAngleBracket){
            for(usize i = m_resolvedParentDirs.size(); i > 0u; --i){
                const auto candidate = ResolveAbsolutePath(arena, m_resolvedParentDirs[i - 1u], MStringView(relPath.data(), relPath.size()));
                if(candidate && ReadTextFile(*candidate, outContent)){
                    outResolvedKey = MString(includePath.data(), includePath.size(), arena);
                    m_resolvedParentDirs.push_back(candidate->parentPath());
                    return true;
                }
            }
        }

        for(const MString& dirText : m_includeDirs){
            const Path<MetaArena> baseDir(arena, AStringView(dirText.data(), dirText.size()));
            const auto candidate = ResolveAbsolutePath(arena, baseDir, MStringView(relPath.data(), relPath.size()));
            if(candidate && ReadTextFile(*candidate, outContent)){
                outResolvedKey = MString(includePath.data(), includePath.size(), arena);
                m_resolvedParentDirs.push_back(candidate->parentPath());
                return true;
            }
        }

        return false;
    }


private:
    NWB::Core::Metascript::MVector<Path<NWB::Core::Metascript::MetaArena>> m_resolvedParentDirs;
    const NWB::Core::Metascript::MVector<NWB::Core::Metascript::MString>& m_includeDirs;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RunAdhdCompilerDriver(
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

        Adhd::CompileResult result(arena);
        if(!Adhd::Compiler::compile(
            MStringView(sourceContent.data(), sourceContent.size()),
            inputView,
            arena,
            result,
            &resolver,
            customNs
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
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

