#pragma once


#include <vector>

#include "../container/adaptor.h"
#include "operations.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
class DirectoryEntry{
public:
    explicit DirectoryEntry(const Path<ArenaT>& path)
        : m_path(path)
    {}


public:
    [[nodiscard]] const Path<ArenaT>& path()const{ return m_path; }
    [[nodiscard]] bool is_regular_file(ErrorCode& outError)const{ return IsRegularFile(m_path, outError); }


private:
    Path<ArenaT> m_path;
};

template<typename ArenaT>
class DirectoryIterator{
public:
    using Entry = DirectoryEntry<ArenaT>;
    using EntryVector = std::vector<Entry, ContainerDetail::ArenaAllocatorFor_T<Entry, ArenaT>>;
    using iterator = typename EntryVector::const_iterator;


public:
    DirectoryIterator(const Path<ArenaT>& path, ErrorCode& outError)
        : m_entries(path.arena())
    {
        collect(path, outError);
    }


public:
    [[nodiscard]] iterator begin()const noexcept{ return m_entries.begin(); }
    [[nodiscard]] iterator end()const noexcept{ return m_entries.end(); }


private:
    void collect(const Path<ArenaT>& path, ErrorCode& outError){
#if defined(NWB_PLATFORM_WINDOWS)
        Path<ArenaT> pattern = path / NWB_TEXT("*");
        WIN32_FIND_DATA data = {};
        HANDLE findHandle = FindFirstFile(pattern.c_str(), &data);
        if(findHandle == INVALID_HANDLE_VALUE){
            GlobalFilesystemDetail::SetLastSystemError(outError);
            return;
        }

        do{
            const TStringView fileName(data.cFileName);
            if(fileName == NWB_TEXT(".") || fileName == NWB_TEXT(".."))
                continue;
            m_entries.emplace_back(path / fileName);
        }while(FindNextFile(findHandle, &data));

        FindClose(findHandle);
        GlobalFilesystemDetail::ClearError(outError);
#else
        DIR* directory = opendir(path.c_str());
        if(directory == nullptr){
            GlobalFilesystemDetail::SetLastSystemError(outError);
            return;
        }

        for(dirent* entry = readdir(directory); entry != nullptr; entry = readdir(directory)){
            const AStringView fileName(entry->d_name);
            if(fileName == "." || fileName == "..")
                continue;
            m_entries.emplace_back(path / fileName);
        }

        closedir(directory);
        GlobalFilesystemDetail::ClearError(outError);
#endif
    }


private:
    EntryVector m_entries;
};

template<typename ArenaT>
class RecursiveDirectoryIterator{
public:
    using Entry = DirectoryEntry<ArenaT>;
    using EntryVector = std::vector<Entry, ContainerDetail::ArenaAllocatorFor_T<Entry, ArenaT>>;
    using iterator = typename EntryVector::const_iterator;


public:
    RecursiveDirectoryIterator(const Path<ArenaT>& path, ErrorCode& outError)
        : m_entries(path.arena())
    {
        collect(path, outError);
    }


public:
    [[nodiscard]] iterator begin()const noexcept{ return m_entries.begin(); }
    [[nodiscard]] iterator end()const noexcept{ return m_entries.end(); }


private:
    void collect(const Path<ArenaT>& path, ErrorCode& outError){
        DirectoryIterator directory(path, outError);
        if(outError)
            return;

        for(const Entry& entry : directory){
            m_entries.push_back(entry);
            ErrorCode directoryError;
            if(IsDirectory(entry.path(), directoryError)){
                collect(entry.path(), outError);
                if(outError)
                    return;
            }
        }
        GlobalFilesystemDetail::ClearError(outError);
    }


private:
    EntryVector m_entries;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

