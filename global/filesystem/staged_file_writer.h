// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations_file_io.h"

#include <global/atomic.h>
#include <global/diagnostics.h>
#include <global/numeric_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MaxStageLeafPrefixCharacters = 180u;
inline constexpr u32 s_MaxUniqueStageAttempts = 256u;
inline constexpr u32 s_DecimalDigitCount = 10u;
inline constexpr usize s_StageNameExtraReserveBytes = 40u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
inline void AppendLowerHexU64(TString<ArenaT>& outText, u64 value){
    Array<tchar, sizeof(u64) * 2u> digits = {};
    usize count = 0u;
    if(value == 0u){
        outText.push_back(FOUNDATION_TEXT('0'));
        return;
    }
    while(value != 0u && count < digits.size()){
        const u32 nibble = static_cast<u32>(value & ::s_HexNibbleMask);
        digits[count++] = static_cast<tchar>(nibble < s_DecimalDigitCount ? (FOUNDATION_TEXT('0') + nibble) : (FOUNDATION_TEXT('a') + (nibble - s_DecimalDigitCount)));
        value >>= ::s_HexNibbleBits;
    }
    while(count > 0u)
        outText.push_back(digits[--count]);
}

[[nodiscard]] inline u64 NextUniqueStageSequence()noexcept{
    static Atomic<u64> s_Sequence{ 0u };
    return s_Sequence.fetch_add(1u, MemoryOrder::relaxed) + 1u;
}

[[nodiscard]] inline u64 CurrentStageProcessId()noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return static_cast<u64>(GetCurrentProcessId());
#else
    return static_cast<u64>(getpid());
#endif
}

[[nodiscard]] inline b8 IsFileExistsError(const ErrorCode& error)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return error.category() == SystemCategory() && (error.value() == ERROR_FILE_EXISTS || error.value() == ERROR_ALREADY_EXISTS);
#else
    return error == ErrorCondition::file_exists;
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> BuildUniqueSiblingStagePath(const Path<ArenaT>& targetPath, const TStringView suffixTag){
    const TStringView leafView = targetPath.filenameView();
    if(leafView.empty() || IsDot(leafView) || IsDotDot(leafView))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    const TStringView boundedLeaf = leafView.size() > s_MaxStageLeafPrefixCharacters
        ? leafView.substr(0u, s_MaxStageLeafPrefixCharacters)
        : leafView;

    ArenaT& arena = targetPath.arena();
    TString<ArenaT> candidateName(arena);
    candidateName.reserve(boundedLeaf.size() + suffixTag.size() + s_StageNameExtraReserveBytes);
    candidateName.push_back(FOUNDATION_TEXT('.'));
    candidateName.append(boundedLeaf.data(), boundedLeaf.size());
    candidateName.push_back(FOUNDATION_TEXT('.'));
    AppendLowerHexU64(candidateName, CurrentStageProcessId());
    candidateName.push_back(FOUNDATION_TEXT('.'));
    AppendLowerHexU64(candidateName, NextUniqueStageSequence());
    candidateName.push_back(FOUNDATION_TEXT('.'));
    candidateName.append(suffixTag.data(), suffixTag.size());

    const Path<ArenaT> parentDir = targetPath.parentPath();
    if(parentDir.empty())
        return Path<ArenaT>(arena, TStringView(candidateName));
    return parentDir / TStringView(candidateName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
class StagedFileWriter final{
public:
    explicit StagedFileWriter(const Path<ArenaT>& path)noexcept
        : m_path(path)
    {}

    [[nodiscard]] Expected<void, ErrorCode> create(const void* const data, const usize count, b8& outCreated)const noexcept{
        outCreated = false;
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        if(static_cast<u64>(count) > static_cast<u64>(Limit<i64>::s_Max))
            return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
        const HANDLE handle = CreateFile(m_path.c_str(), GENERIC_WRITE, 0u, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(handle == INVALID_HANDLE_VALUE)
            return MakeUnexpected(LastSystemError());
        outCreated = true;
        ErrorCode error;
        const u8* cursor = static_cast<const u8*>(data);
        usize remaining = count;
        while(remaining > 0u){
            const DWORD requestSize = remaining > static_cast<usize>(Limit<DWORD>::s_Max) ? Limit<DWORD>::s_Max : static_cast<DWORD>(remaining);
            DWORD transferred = 0u;
            if(!WriteFile(handle, cursor, requestSize, &transferred, nullptr)){
                error = LastSystemError();
                break;
            }
            if(transferred == 0u){
                error = MakeErrorCode(ErrorCondition::io_error);
                break;
            }
            cursor += transferred;
            remaining -= transferred;
        }
        if(!error && !FlushFileBuffers(handle))
            error = LastSystemError();
        if(!CloseHandle(handle) && !error)
            error = LastSystemError();
#else
        if(static_cast<u64>(count) > static_cast<u64>(Limit<off_t>::s_Max))
            return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
        constexpr u32 s_NewFileMode = 0666u;
        const int descriptor = open(m_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, s_NewFileMode);
        if(descriptor < 0)
            return MakeUnexpected(LastSystemError());
        outCreated = true;
        ErrorCode error;
        const u8* cursor = static_cast<const u8*>(data);
        usize remaining = count;
        while(remaining > 0u){
            const usize requestSize = remaining > static_cast<usize>(Limit<ssize_t>::s_Max) ? static_cast<usize>(Limit<ssize_t>::s_Max) : remaining;
            const ssize_t transferred = write(descriptor, cursor, requestSize);
            if(transferred < 0){
                if(errno == EINTR)
                    continue;
                error = LastSystemError();
                break;
            }
            if(transferred == 0){
                error = MakeErrorCode(ErrorCondition::io_error);
                break;
            }
            cursor += static_cast<usize>(transferred);
            remaining -= static_cast<usize>(transferred);
        }
        if(!error && fsync(descriptor) != 0)
            error = LastSystemError();
        if(close(descriptor) != 0 && !error)
            error = LastSystemError();
#endif
        if(error)
            return MakeUnexpected(error);
        return {};
    }

    [[nodiscard]] Expected<Path<ArenaT>, ErrorCode> stageUnique(
        const void* const data,
        const usize count,
        const TStringView suffixTag = FOUNDATION_TEXT("stage")
    )const{
        static_assert(IsNothrowMoveConstructible_V<Path<ArenaT>>);
        static_assert(noexcept(Expected<Path<ArenaT>, ErrorCode>(DeclVal<Path<ArenaT>&&>())));
        for(u32 attempt = 0u; attempt < GlobalFilesystemDetail::s_MaxUniqueStageAttempts; ++attempt){
            auto stagedPath = GlobalFilesystemDetail::BuildUniqueSiblingStagePath(m_path, suffixTag);
            if(!stagedPath)
                return MakeUnexpected(stagedPath.error());
            b8 created = false;
            const auto staged = StagedFileWriter(*stagedPath).create(data, count, created);
            if(staged)
                return Move(*stagedPath);
            if(created){
                const auto removed = RemoveFile(*stagedPath);
                if(!removed || !*removed){
                    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = "staged_file_cleanup", .message = "failed to remove staged file", });
                    if(!removed)
                        return MakeUnexpected(staged.error() ? staged.error() : removed.error());
                }
                return MakeUnexpected(staged.error());
            }
            if(GlobalFilesystemDetail::IsFileExistsError(staged.error()))
                continue;
            return MakeUnexpected(staged.error());
        }
        return MakeUnexpected(MakeErrorCode(ErrorCondition::file_exists));
    }

    [[nodiscard]] Expected<void, ErrorCode> writeAtomicChecked(const void* const data, const usize count)const{
        const auto existing = FileExistsNoFollow(m_path);
        if(!existing)
            return MakeUnexpected(existing.error());
        if(*existing){
            const auto regular = IsRegularFile(m_path);
            if(!regular)
                return MakeUnexpected(regular.error());
            if(!*regular)
                return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
        }
        const auto stagedPath = stageUnique(data, count);
        if(!stagedPath)
            return MakeUnexpected(stagedPath.error());
        const auto renamed = RenamePath(*stagedPath, m_path);
        if(!renamed){
            const auto removed = RemoveFile(*stagedPath);
            if(!removed)
                return MakeUnexpected(renamed.error() ? renamed.error() : removed.error());
            return MakeUnexpected(renamed.error());
        }
        return {};
    }

    [[nodiscard]] b8 writeTextAtomic(const AStringView content)const{
        if(!CanRepresentStreamSize(static_cast<u64>(content.size())))
            return false;
        return writeAtomicChecked(content.data(), content.size()).has_value();
    }

    template<typename Container>
    [[nodiscard]] b8 writeBinaryAtomic(const Container& bytes)const{
        if(!CanRepresentStreamSize(static_cast<u64>(bytes.size())))
            return false;
        return writeAtomicChecked(bytes.data(), bytes.size()).has_value();
    }


private:
    const Path<ArenaT>& m_path;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

