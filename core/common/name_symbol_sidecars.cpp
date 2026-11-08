// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "name_symbol_sidecars.h"
#include "arena_names.h"

#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/executable_paths.h>
#include <global/filesystem/staged_file_writer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<::Path<Alloc::ScratchArena>> NameSymbolSidecars::DefaultPath(Alloc::ScratchArena& scratchArena){
    const auto executableDirectory = ::ExecutablePaths(scratchArena).directory();
    if(!executableDirectory)
        return MakeUnexpected(Failure{});
    const auto executableName = ::ExecutablePaths(scratchArena).name();
    if(!executableName)
        return MakeUnexpected(Failure{});
    ::Path<Alloc::ScratchArena> outputPath = *executableDirectory / *executableName;
    outputPath.replaceExtension(NameSymbols::s_FileExtension);
    return outputPath;
}

b8 NameSymbolSidecars::Write(const ::Path<Alloc::ScratchArena>& path, const AStringView text){
    return ::StagedFileWriter(path).writeTextAtomic(text);
}

Expected<b8, NameSymbols::DefaultFileLoadFailure> NameSymbolSidecars::LoadDefault(){
    Alloc::ScratchArena scratchArena(CommonArenaScope::s_NameSymbolsScratchArena);
    const auto executableDirectory = ::ExecutablePaths(scratchArena).directory();
    if(!executableDirectory)
        return MakeUnexpected(NameSymbols::DefaultFileLoadFailure{ executableDirectory.error(), false });

    b8 loadedAny = false;
    ErrorCode firstError;

    const auto directory = ::DirectoryIterator<Alloc::ScratchArena>::Create(*executableDirectory);
    if(!directory)
        return MakeUnexpected(NameSymbols::DefaultFileLoadFailure{ directory.error(), loadedAny });

    for(const ::DirectoryEntry<Alloc::ScratchArena>& entry : *directory){
        if(entry.path().extensionView() != NameSymbols::s_FileExtension)
            continue;

        const auto isRegularFile = entry.isRegularFile();
        if(!isRegularFile){
            if(!firstError)
                firstError = isRegularFile.error();
            continue;
        }
        if(!*isRegularFile)
            continue;

        const auto loaded = NameSymbols::LoadFile(entry.path(), scratchArena);
        if(!loaded){
            if(!firstError)
                firstError = loaded.error();
            continue;
        }
        loadedAny = *loaded || loadedAny;
    }

    if(firstError)
        return MakeUnexpected(NameSymbols::DefaultFileLoadFailure{ firstError, loadedAny });
    return loadedAny;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

