// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
class DirectoryIteratorBase{
public:
    using Entry = DirectoryEntry<ArenaT>;
    using EntryVector = std::vector<Entry, ContainerDetail::ArenaAllocatorFor_T<Entry, ArenaT>>;
    using iterator = typename EntryVector::const_iterator;


protected:
    explicit DirectoryIteratorBase(const Path<ArenaT>& path)
        : m_entries(path.arena())
    {}


public:
    [[nodiscard]] iterator begin()const noexcept{ return m_entries.begin(); }
    [[nodiscard]] iterator end()const noexcept{ return m_entries.end(); }


protected:
    EntryVector m_entries;
};

template<typename ArenaT>
class DirectoryIterator : public DirectoryIteratorBase<ArenaT>{
    using BaseType = DirectoryIteratorBase<ArenaT>;


public:
    using Entry = typename BaseType::Entry;
    using iterator = typename BaseType::iterator;
    using BaseType::begin;
    using BaseType::end;


public:
    DirectoryIterator(const Path<ArenaT>& path, ErrorCode& outError)
        : BaseType(path)
    {
        collect(path, outError);
    }


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
            this->m_entries.emplace_back(path / fileName);
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
            this->m_entries.emplace_back(path / fileName);
        }

        closedir(directory);
        GlobalFilesystemDetail::ClearError(outError);
#endif
    }
};

template<typename ArenaT>
class RecursiveDirectoryIterator : public DirectoryIteratorBase<ArenaT>{
    using BaseType = DirectoryIteratorBase<ArenaT>;


public:
    using Entry = typename BaseType::Entry;
    using iterator = typename BaseType::iterator;
    using BaseType::begin;
    using BaseType::end;


public:
    RecursiveDirectoryIterator(const Path<ArenaT>& path, ErrorCode& outError)
        : BaseType(path)
    {
        collect(path, outError);
    }


private:
    void collect(const Path<ArenaT>& path, ErrorCode& outError){
        DirectoryIterator directory(path, outError);
        if(outError)
            return;

        for(const Entry& entry : directory){
            this->m_entries.push_back(entry);
            ErrorCode directoryError;
            if(IsDirectory(entry.path(), directoryError)){
                collect(entry.path(), outError);
                if(outError)
                    return;
            }
        }
        GlobalFilesystemDetail::ClearError(outError);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

