// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/metascript/adhd/compiler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AdhdCompileOptions{
    Core::Metascript::MetaArena& arena;
    Span<const Core::Metascript::MString> inputs;
    Span<const Core::Metascript::MString> includeDirectories;
    Span<const Core::Metascript::MString> cppHeaders;
    Span<const Core::Metascript::MString> cppIncludeDirectories;
    Span<const Core::Metascript::MString> cppDefines;
    Span<const Core::Metascript::MString> cppCompilerArgs;
    Core::Metascript::MStringView outputDirectory;
    Core::Metascript::MStringView customNamespace;
    Core::Metascript::MStringView namespaceMapPath;
    Core::Metascript::MStringView depfilePath;
    Core::Metascript::MStringView emitCppExternsPath;


public:
    explicit AdhdCompileOptions(Core::Metascript::MetaArena& inArena)noexcept
        : arena(inArena)
    {}
};


[[nodiscard]] b8 CompileAdhd(const AdhdCompileOptions& options);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

