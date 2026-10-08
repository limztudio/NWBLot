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
    [[nodiscard]] const Path<ArenaT>& path()const noexcept{ return m_path; }
    [[nodiscard]] Expected<bool, ErrorCode> isRegularFile()const noexcept{ return IsRegularFile(m_path); }


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
    [[nodiscard]] static Expected<DirectoryIterator, ErrorCode> Create(const Path<ArenaT>& path){
        DirectoryIterator result(path);
        const auto collected = result.collect(path);
        if(!collected)
            return MakeUnexpected(collected.error());
        return result;
    }


private:
    explicit DirectoryIterator(const Path<ArenaT>& path)
        : BaseType(path)
    {}

    [[nodiscard]] Expected<void, ErrorCode> collect(const Path<ArenaT>& path){
        ErrorCode iterationError;
#if defined(NWB_PLATFORM_WINDOWS)
        const Path<ArenaT> pattern = path / NWB_TEXT("*");
        WIN32_FIND_DATA data = {};
        const HANDLE findHandle = FindFirstFile(pattern.c_str(), &data);
        if(findHandle == INVALID_HANDLE_VALUE)
            return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
        ScopeExit close([&]()noexcept{ GlobalFilesystemDetail::CloseDirectory(findHandle, iterationError); });
        for(;;){
            const TStringView fileName(data.cFileName);
            if(fileName != NWB_TEXT(".") && fileName != NWB_TEXT(".."))
                this->m_entries.emplace_back(path / fileName);
            if(FindNextFile(findHandle, &data))
                continue;
            GlobalFilesystemDetail::CaptureDirectoryIterationError(iterationError);
            break;
        }
        GlobalFilesystemDetail::CloseDirectory(findHandle, iterationError);
#else
        DIR* const directory = opendir(path.c_str());
        if(directory == nullptr)
            return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
        ScopeExit close([&]()noexcept{ GlobalFilesystemDetail::CloseDirectory(directory, iterationError); });
        for(;;){
            errno = 0;
            dirent* const entry = readdir(directory);
            if(entry == nullptr){
                GlobalFilesystemDetail::CaptureDirectoryIterationError(iterationError);
                break;
            }
            const AStringView fileName(entry->d_name);
            if(GlobalFilesystemPathDetail::IsDot(fileName) || GlobalFilesystemPathDetail::IsDotDot(fileName))
                continue;
            this->m_entries.emplace_back(path / fileName);
        }
        GlobalFilesystemDetail::CloseDirectory(directory, iterationError);
#endif
        close.release();
        if(iterationError)
            return MakeUnexpected(iterationError);
        return {};
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
    [[nodiscard]] static Expected<RecursiveDirectoryIterator, ErrorCode> Create(const Path<ArenaT>& path){
        RecursiveDirectoryIterator result(path);
        const auto collected = result.collect(path);
        if(!collected)
            return MakeUnexpected(collected.error());
        return result;
    }


private:
    explicit RecursiveDirectoryIterator(const Path<ArenaT>& path)
        : BaseType(path)
    {}

    [[nodiscard]] Expected<void, ErrorCode> collect(const Path<ArenaT>& path){
        const auto directory = DirectoryIterator<ArenaT>::Create(path);
        if(!directory)
            return MakeUnexpected(directory.error());
        for(const Entry& entry : *directory){
            this->m_entries.push_back(entry);
            const auto isDirectory = IsDirectoryNoFollow(entry.path());
            if(!isDirectory)
                return MakeUnexpected(isDirectory.error());
            if(*isDirectory){
                const auto collected = collect(entry.path());
                if(!collected)
                    return MakeUnexpected(collected.error());
            }
        }
        return {};
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

