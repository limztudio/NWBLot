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
    [[nodiscard]] Expected<bool, ErrorCode> operator()(const PathT&)const noexcept{
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename PredicateT>
[[nodiscard]] inline Expected<void, ErrorCode> ApplyDirectoryRetention(ArenaT& arena, const Path<ArenaT>& directory, const usize maxEntries, PredicateT&& shouldRetainPath){
    if(maxEntries == 0u)
        return {};

    const auto exists = IsDirectory(directory);
    if(!exists)
        return MakeUnexpected(exists.error());
    if(!*exists)
        return {};

    Vector<Path<ArenaT>, ArenaT> entries{arena};
    auto directoryIt = DirectoryIterator<ArenaT>::Create(directory);
    if(!directoryIt)
        return MakeUnexpected(directoryIt.error());

    entries.reserve(static_cast<usize>(directoryIt->end() - directoryIt->begin()));
    for(const auto& entry : *directoryIt){
        const auto retained = shouldRetainPath(entry.path());
        if(!retained)
            return MakeUnexpected(retained.error());
        if(*retained)
            entries.emplace_back(arena, entry.path());
    }

    if(entries.size() <= maxEntries)
        return {};

    Sort(entries.begin(), entries.end());

    ErrorCode firstError;
    const usize removeCount = entries.size() - maxEntries;
    for(usize i = 0u; i < removeCount; ++i){
        const auto removed = RemoveAllIfExists(entries[i]);
        if(!removed && !firstError)
            firstError = removed.error();
    }
    if(firstError)
        return MakeUnexpected(firstError);
    return {};
}

template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> ApplyDirectoryRetention(ArenaT& arena, const Path<ArenaT>& directory, const usize maxEntries){
    return ApplyDirectoryRetention(arena, directory, maxEntries, GlobalFilesystemRetentionDetail::KeepAllEntries{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

