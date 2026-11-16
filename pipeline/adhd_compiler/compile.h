// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/metascript/adhd/compiler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AdhdCompileOptions{
    Core::Metascript::MVector<Core::Metascript::MString> inputs;
    Core::Metascript::MVector<Core::Metascript::MString> includeDirectories;
    Core::Metascript::MVector<Core::Metascript::MString> cppHeaders;
    Core::Metascript::MVector<Core::Metascript::MString> cppIncludeDirectories;
    Core::Metascript::MVector<Core::Metascript::MString> cppDefines;
    Core::Metascript::MVector<Core::Metascript::MString> cppCompilerArgs;
    Core::Metascript::MString outputDirectory;
    Core::Metascript::MString customNamespace;
    Core::Metascript::MString namespaceMapPath;
    Core::Metascript::MString depfilePath;
    Core::Metascript::MString emitCppExternsPath;


public:
    explicit AdhdCompileOptions(Core::Metascript::MetaArena& arena)noexcept
        : inputs(arena)
        , includeDirectories(arena)
        , cppHeaders(arena)
        , cppIncludeDirectories(arena)
        , cppDefines(arena)
        , cppCompilerArgs(arena)
        , outputDirectory(arena)
        , customNamespace(arena)
        , namespaceMapPath(arena)
        , depfilePath(arena)
        , emitCppExternsPath(arena)
    {}
};


[[nodiscard]] b8 CompileAdhd(const AdhdCompileOptions& options);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ADHD_COMPILER_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

