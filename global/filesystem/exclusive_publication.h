// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/binary.h>
#include <global/containers.h>
#include <global/diagnostics.h>
#include <global/scope_exit.h>
#include <global/thread.h>
#include <global/filesystem/operations_file_io.h>
#include <global/filesystem/operations_remove.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FilePublicationResult{
    b8 published = false;
    b8 stagingRetained = false;
    ErrorCode error;
};


template<typename ArenaT>
[[nodiscard]] FilePublicationResult PublishNewFileDurable(const ::Path<ArenaT>& destination, const BinaryByteView bytes){
    ::Path<ArenaT> staging(destination.arena(), destination);
    staging += ".partial";

    b8 created = false;
    const auto staged = StageNewBinaryFileDurable(staging, bytes.data(), bytes.size(), created);
    if(!staged)
        return { false, created, staged.error() };

#if defined(NWB_PLATFORM_WINDOWS)
    if(!MoveFileEx(staging.c_str(), destination.c_str(), 0u))
        return { false, true, LastSystemError() };
#else
    if(::link(staging.c_str(), destination.c_str()) != 0)
        return { false, true, LastSystemError() };
    const auto removed = RemoveFile(staging);
    if(!removed)
        return { true, true, removed.error() };
    if(!*removed)
        return { true, false, MakeErrorCode(ErrorCondition::no_such_file_or_directory) };
#endif
    return { true, false, {} };
}


// Returns true when published and false when identical bytes already existed.
template<typename ArenaT>
[[nodiscard]] Expected<b8, ErrorCode> PublishUniqueFileIfAbsentOrIdentical(const ::Path<ArenaT>& destination, const Span<const u8> bytes){
    Vector<u8, ArenaT> scratch(destination.arena());
    const auto readWithRetry = [&](const b8 retryMissing){
        auto result = ReadBinaryFile(destination, scratch);
        constexpr u32 s_MaxReadAttempts = 32u;
        for(u32 attempt = 1u; !result && attempt < s_MaxReadAttempts; ++attempt){
            const ErrorCode readError = result.error();
            b8 retryable = readError == ErrorCondition::io_error || (retryMissing && IsMissingPathError(readError));
#if defined(NWB_PLATFORM_WINDOWS)
            retryable = retryable || (readError.category() == SystemCategory() && (readError.value() == ERROR_SHARING_VIOLATION || readError.value() == ERROR_LOCK_VIOLATION || readError.value() == ERROR_ACCESS_DENIED));
#endif
            if(!retryable)
                break;
            SleepMS(1u);
            result = ReadBinaryFile(destination, scratch);
        }
        return result;
    };
    const auto existing = readWithRetry(false);
    if(existing){
        if(BytesMatch(Span<const u8>(scratch.data(), scratch.size()), bytes))
            return false;
        return MakeUnexpected(MakeErrorCode(ErrorCondition::file_exists));
    }
    if(!IsMissingPathError(existing.error()))
        return MakeUnexpected(existing.error());

    auto staged = StageUniqueBinaryFileDurable(destination, bytes.data(), bytes.size());
    if(!staged)
        return MakeUnexpected(staged.error());
    ::Path<ArenaT> temporary = Move(*staged);
    b8 owned = true;
    ScopeExit cleanupOnExit([&]()noexcept{
        if(!owned)
            return;
        const auto removed = RemoveFile(temporary);
        if(!removed || !*removed)
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Warning, .category = "staged_file_cleanup", .message = "failed to remove staged file", });
    });
    const auto finish = [&](Expected<b8, ErrorCode> result) -> Expected<b8, ErrorCode>{
        if(owned){
            const auto removed = RemoveFile(temporary);
            owned = false;
            if(!removed)
                return MakeUnexpected(removed.error());
            if(!*removed)
                return MakeUnexpected(MakeErrorCode(ErrorCondition::no_such_file_or_directory));
        }
        return result;
    };

    const auto stagedRead = ReadBinaryFile(temporary, scratch);
    if(!stagedRead)
        return finish(MakeUnexpected(stagedRead.error()));
    if(!BytesMatch(Span<const u8>(scratch.data(), scratch.size()), bytes))
        return finish(MakeUnexpected(MakeErrorCode(ErrorCondition::io_error)));

#if defined(NWB_PLATFORM_WINDOWS)
    if(MoveFileEx(temporary.c_str(), destination.c_str(), 0u)){
        owned = false;
        return true;
    }
#else
    if(::link(temporary.c_str(), destination.c_str()) == 0)
        return finish(true);
#endif
    const ErrorCode publicationError = LastSystemError();
    const b8 collision = GlobalFilesystemDetail::IsFileExistsError(publicationError);
#if defined(NWB_PLATFORM_WINDOWS)
    const b8 mayHaveWinner = collision || (publicationError.category() == SystemCategory() && publicationError.value() == ERROR_ACCESS_DENIED);
#else
    const b8 mayHaveWinner = collision;
#endif
    if(!mayHaveWinner)
        return finish(MakeUnexpected(publicationError));
    const auto winner = readWithRetry(true);
    if(winner){
        if(!BytesMatch(Span<const u8>(scratch.data(), scratch.size()), bytes))
            return finish(MakeUnexpected(MakeErrorCode(ErrorCondition::file_exists)));
        return finish(false);
    }
    if(collision)
        return finish(MakeUnexpected(winner.error()));
    return finish(MakeUnexpected(publicationError));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

