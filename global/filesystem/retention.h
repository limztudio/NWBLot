// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../algorithm.h"
#include "../containers.h"
#include "directory_iterator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemRetentionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct KeepAllEntries{
    template<typename PathT>
    [[nodiscard]] bool operator()(const PathT&)const{
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename PredicateT>
[[nodiscard]] inline bool ApplyDirectoryRetention(
    ArenaT& arena,
    const Path<ArenaT>& directory,
    const usize maxEntries,
    PredicateT&& shouldRetainPath
){
    if(maxEntries == 0u)
        return true;

    const auto exists = IsDirectory(directory);
    if(!exists)
        return false;
    if(!*exists)
        return true;

    Vector<Path<ArenaT>, ArenaT> entries{arena};
    auto directoryIt = DirectoryIterator<ArenaT>::Create(directory);
    if(!directoryIt)
        return false;

    for(const auto& entry : *directoryIt){
        if(shouldRetainPath(entry.path()))
            entries.emplace_back(arena, entry.path());
    }

    if(entries.size() <= maxEntries)
        return true;

    Sort(entries.begin(), entries.end());

    bool ok = true;
    const usize removeCount = entries.size() - maxEntries;
    for(usize i = 0u; i < removeCount; ++i){
        if(!RemoveAllIfExists(entries[i]))
            ok = false;
    }
    return ok;
}

template<typename ArenaT>
[[nodiscard]] inline bool ApplyDirectoryRetention(ArenaT& arena, const Path<ArenaT>& directory, const usize maxEntries){
    return ApplyDirectoryRetention(arena, directory, maxEntries, GlobalFilesystemRetentionDetail::KeepAllEntries{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

