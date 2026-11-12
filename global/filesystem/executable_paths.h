// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
class ExecutablePaths final{
public:
    using uses_leading_arena_t = ArenaT;

    explicit ExecutablePaths(ArenaT& arena)noexcept
        : m_arena(arena)
    {}
    explicit ExecutablePaths(ArenaT& arena, const ExecutablePaths&)noexcept
        : m_arena(arena)
    {}


public:
    [[nodiscard]] Expected<Path<ArenaT>, ErrorCode> path()const{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        constexpr usize s_MaxPathLength = 4096;
        Array<tchar, s_MaxPathLength> executablePathBuffer = {};
        const DWORD copiedLength = GetModuleFileName(nullptr, executablePathBuffer.data(), static_cast<DWORD>(s_MaxPathLength));
        if(copiedLength == 0u)
            return MakeUnexpected(LastSystemError());
        if(copiedLength >= static_cast<DWORD>(s_MaxPathLength))
            return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
        return Path<ArenaT>(m_arena, TStringView(executablePathBuffer.data(), static_cast<usize>(copiedLength)));
#elif defined(FOUNDATION_PLATFORM_LINUX)
        const auto path = ReadSymlink(Path<ArenaT>(m_arena, "/proc/self/exe"));
        if(!path)
            return MakeUnexpected(path.error());
        if(path->empty())
            return MakeUnexpected(MakeErrorCode(ErrorCondition::no_such_file_or_directory));
        return LexicallyNormal(*path);
#else
        return GetCurrentPath(m_arena);
#endif
    }

    [[nodiscard]] Expected<Path<ArenaT>, ErrorCode> directory()const{
        const auto executablePath = path();
        if(!executablePath)
            return MakeUnexpected(executablePath.error());
        Path<ArenaT> result = executablePath->parentPath();
        if(result.empty())
            return MakeUnexpected(MakeErrorCode(ErrorCondition::no_such_file_or_directory));
        return result;
    }

    [[nodiscard]] Path<ArenaT> defaultStorageDirectory(const AStringView defaultDirectoryName)const{
        const auto executableDirectory = directory();
        if(executableDirectory)
            return *executableDirectory / defaultDirectoryName;
        return Path<ArenaT>(m_arena, defaultDirectoryName);
    }

    template<typename SourceArenaT>
    [[nodiscard]] Path<ArenaT> storageDirectory(const Path<SourceArenaT>& configuredStorageDirectory, const AStringView defaultDirectoryName)const{
        if(!configuredStorageDirectory.empty())
            return Path<ArenaT>(m_arena, configuredStorageDirectory);
        return defaultStorageDirectory(defaultDirectoryName);
    }

    [[nodiscard]] Expected<Path<ArenaT>, ErrorCode> name()const{
        const auto executablePath = path();
        if(!executablePath)
            return MakeUnexpected(executablePath.error());
        Path<ArenaT> result = executablePath->stem();
        if(result.empty())
            return MakeUnexpected(MakeErrorCode(ErrorCondition::no_such_file_or_directory));
        return result;
    }


private:
    ArenaT& m_arena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

