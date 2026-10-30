// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/metascript/adhd/compiler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AdhdCompilerOptions{
    NWB::Core::Metascript::MVector<NWB::Core::Metascript::MString> inputFiles;
    NWB::Core::Metascript::MVector<NWB::Core::Metascript::MString> includeDirectories;
    NWB::Core::Metascript::MVector<NWB::Core::Metascript::MString> macroDefinitions;
    NWB::Core::Metascript::MString outputDirectory;
    NWB::Core::Metascript::MString customNamespace;
    NWB::Core::Metascript::MString depfilePath;
    b8 validateOnly = false;
    b8 showHelp = false;


public:
    explicit AdhdCompilerOptions(NWB::Core::Metascript::MetaArena& arena)noexcept
        : inputFiles(arena)
        , includeDirectories(arena)
        , macroDefinitions(arena)
        , outputDirectory(arena)
        , customNamespace(arena)
        , depfilePath(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 ParseAdhdCompilerOptions(int argc, char** argv, NWB::Core::Metascript::MetaArena& arena, AdhdCompilerOptions& outOptions);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

