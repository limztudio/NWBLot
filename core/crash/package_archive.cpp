// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_archive.h"

#include <global/error_code.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
static void AppendArchiveText(CrashBytesT<ArenaT>& out, const AStringView text){
    out.insert(out.end(), text.begin(), text.end());
}

template<typename ArenaT, typename PathArenaT>
Expected<CrashBytesT<ArenaT>, ErrorCode> CrashPackageArchive::Build(ArenaT& arena, Alloc::ScratchArena& scratchArena, const ::Path<PathArenaT>& packageDirectory){
    CrashBytesT<ArenaT> archive(arena);
    AppendArchiveText(archive, PackageNames::s_ArchiveHeaderText);

    const ::Path<Alloc::ScratchArena> sourceDirectory(scratchArena, packageDirectory);
    const auto directory = RecursiveDirectoryIterator<Alloc::ScratchArena>::Create(sourceDirectory);
    if(!directory)
        return MakeUnexpected(directory.error());

    CrashBytesT<Alloc::ScratchArena> fileBytes(scratchArena);
    b8 wroteFile = false;
    for(const auto& entry : *directory){
        const auto regularFile = entry.isRegularFile();
        if(!regularFile)
            return MakeUnexpected(regularFile.error());
        if(!*regularFile)
            continue;

        const auto read = ReadBinaryFile(entry.path(), fileBytes);
        if(!read)
            return MakeUnexpected(read.error());

        const ::Path<Alloc::ScratchArena> relativePath = entry.path().lexicallyRelative(sourceDirectory);
        CrashStringT<Alloc::ScratchArena> pathStorage(scratchArena);
        const AStringView pathText = GenericNativeTextView(relativePath.native(), pathStorage);
        AppendArchiveText(archive, PackageNames::s_ArchiveFileHeaderPrefix);
        AppendArchiveText(archive, AStringView(pathText.data(), pathText.size()));
        AppendArchiveText(archive, " ");
        AppendUnsignedU64Bytes(archive, fileBytes.size());
        AppendArchiveText(archive, "\n");
        archive.insert(archive.end(), fileBytes.begin(), fileBytes.end());
        AppendArchiveText(archive, PackageNames::s_ArchiveEntryEndText);
        wroteFile = true;
    }

    if(!wroteFile)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    return archive;
}

template Expected<CrashBytesT<Alloc::GlobalArena>, ErrorCode> CrashPackageArchive::Build(Alloc::GlobalArena& arena, Alloc::ScratchArena& scratchArena, const ::Path<Alloc::GlobalArena>& packageDirectory);
template Expected<CrashBytesT<Alloc::GlobalArena>, ErrorCode> CrashPackageArchive::Build(Alloc::GlobalArena& arena, Alloc::ScratchArena& scratchArena, const ::Path<Alloc::ScratchArena>& packageDirectory);
template Expected<CrashBytesT<Alloc::GlobalArena>, ErrorCode> CrashPackageArchive::Build(Alloc::GlobalArena& arena, Alloc::ScratchArena& scratchArena, const ::Path<::ArenaBase>& packageDirectory);

template Expected<CrashBytesT<Alloc::PersistentArena>, ErrorCode> CrashPackageArchive::Build(Alloc::PersistentArena& arena, Alloc::ScratchArena& scratchArena, const ::Path<Alloc::PersistentArena>& packageDirectory);
template Expected<CrashBytesT<Alloc::PersistentArena>, ErrorCode> CrashPackageArchive::Build(Alloc::PersistentArena& arena, Alloc::ScratchArena& scratchArena, const ::Path<Alloc::ScratchArena>& packageDirectory);
template Expected<CrashBytesT<Alloc::PersistentArena>, ErrorCode> CrashPackageArchive::Build(Alloc::PersistentArena& arena, Alloc::ScratchArena& scratchArena, const ::Path<::ArenaBase>& packageDirectory);



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

