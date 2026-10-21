// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bake.h"
#include "asset_metadata.h"
#include "prepared_font.h"

#include <core/common/log.h>

#include <global/diagnostics.h>
#include <global/error_code.h>
#include <global/scope_exit.h>

#if !defined(NWB_PLATFORM_WINDOWS)
#include <fcntl.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_builder_writer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TemporarySuffix = ".tmp";
static constexpr AStringView s_BackupSuffix = ".old";
static constexpr AStringView s_PublicationDiagnosticTag = "font_builder_publication";
static constexpr u32 s_OutputFileCount = 2u;


struct OutputPaths{
    ScratchPath metadata;
    ScratchPath font;
    ScratchPath metadataTemporary;
    ScratchPath fontTemporary;
    ScratchPath metadataBackup;
    ScratchPath fontBackup;


private:
    [[nodiscard]] static ScratchPath MakeFontPath(Core::Alloc::ScratchArena& scratch, const Path& output){
        ScratchPath path(scratch, output);
        path.replaceExtension(s_PreparedFontExtension);
        return path;
    }
    [[nodiscard]] static ScratchPath MakeWorkPath(Core::Alloc::ScratchArena& scratch, const ScratchPath& base, const AStringView suffix){
        ScratchPath path(scratch, base);
        path += suffix;
        return path;
    }


public:
    explicit OutputPaths(Core::Alloc::ScratchArena& scratch, const Path& output)
        : metadata(scratch, output)
        , font(MakeFontPath(scratch, output))
        , metadataTemporary(MakeWorkPath(scratch, metadata, s_TemporarySuffix))
        , fontTemporary(MakeWorkPath(scratch, font, s_TemporarySuffix))
        , metadataBackup(MakeWorkPath(scratch, metadata, s_BackupSuffix))
        , fontBackup(MakeWorkPath(scratch, font, s_BackupSuffix))
    {}
};

class TemporaryFile final : NoCopy{
public:
    explicit TemporaryFile(const ScratchPath& path)noexcept
        : m_path(path)
    {}
    ~TemporaryFile()noexcept{
        if(m_owned){
            const auto removed = RemoveFile(m_path);
            if(!removed){
                const DiagnosticEventText diagnostic = MakeDiagnosticEventText("font_builder: cannot remove unpublished temporary '{}', error {}", m_path.c_str(), removed.error().value());
                CaptureDiagnosticEvent(s_PublicationDiagnosticTag, diagnostic.view(), __FILE__, __LINE__);
            }
        }
    }


public:
    [[nodiscard]] Expected<void, ErrorCode> stage(const u8* data, const usize count)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
        if(static_cast<u64>(count) > static_cast<u64>(Limit<i64>::s_Max))
            return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
        const HANDLE handle = CreateFile(m_path.c_str(), GENERIC_WRITE, 0u, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if(handle == INVALID_HANDLE_VALUE)
            return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
        m_owned = true;
        ErrorCode error;
        const u8* cursor = static_cast<const u8*>(data);
        usize remaining = count;
        while(remaining > 0u){
            const DWORD requestSize = remaining > static_cast<usize>(Limit<DWORD>::s_Max) ? Limit<DWORD>::s_Max : static_cast<DWORD>(remaining);
            DWORD transferred = 0u;
            if(!WriteFile(handle, cursor, requestSize, &transferred, nullptr)){
                error = GlobalFilesystemDetail::LastSystemError();
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
            error = GlobalFilesystemDetail::LastSystemError();
        if(!CloseHandle(handle) && !error)
            error = GlobalFilesystemDetail::LastSystemError();
#else
        if(static_cast<u64>(count) > static_cast<u64>(Limit<off_t>::s_Max))
            return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
        constexpr u32 s_NewFileMode = 0666u;
        const int descriptor = open(m_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, s_NewFileMode);
        if(descriptor < 0)
            return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
        m_owned = true;
        ErrorCode error;
        const u8* cursor = static_cast<const u8*>(data);
        usize remaining = count;
        while(remaining > 0u){
            const usize requestSize = remaining > static_cast<usize>(Limit<ssize_t>::s_Max) ? static_cast<usize>(Limit<ssize_t>::s_Max) : remaining;
            const ssize_t transferred = write(descriptor, cursor, requestSize);
            if(transferred < 0){
                if(errno == EINTR)
                    continue;
                error = GlobalFilesystemDetail::LastSystemError();
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
            error = GlobalFilesystemDetail::LastSystemError();
        if(close(descriptor) != 0 && !error)
            error = GlobalFilesystemDetail::LastSystemError();
#endif
        if(error)
            return MakeUnexpected(error);
        return {};
    }
    [[nodiscard]] bool verify(const u8* expected, const usize count)const{
        InputFileStream stream(m_path, InputFileStream::binary);
        if(!stream.is_open())
            return false;
        constexpr usize s_VerifyChunkSize = 4096u;
        u8 buffer[s_VerifyChunkSize];
        for(usize offset = 0u; offset < count;){
            const usize remaining = count - offset;
            const usize chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            stream.read(reinterpret_cast<char*>(buffer), static_cast<StreamSize>(chunk));
            if(stream.gcount() != static_cast<StreamSize>(chunk) || NWB_MEMCMP(buffer, expected + offset, chunk) != 0)
                return false;
            offset += chunk;
        }
        char extra = 0;
        stream.read(&extra, 1);
        return stream.gcount() == 0 && stream.eof();
    }
    void published()noexcept{ m_owned = false; }


private:
    const ScratchPath& m_path;
    bool m_owned = false;
};

[[nodiscard]] static Expected<bool, ErrorCode> CheckOutputs(const OutputPaths& paths, const bool overwrite)noexcept{
    const NotNull<const ScratchPath*> files[] = { MakeNotNull(&paths.metadata), MakeNotNull(&paths.font) };
    u32 presentCount = 0u;
    for(const auto file : files){
        const auto exists = FileExistsNoFollow(*file);
        if(!exists)
            return MakeUnexpected(exists.error());
        if(*exists){
            const auto regular = IsRegularFile(*file);
            if(!regular)
                return MakeUnexpected(regular.error());
            if(!*regular)
                return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
            ++presentCount;
        }
    }
    if(presentCount != 0u && (presentCount != s_OutputFileCount || !overwrite))
        return MakeUnexpected(MakeErrorCode(presentCount != s_OutputFileCount ? ErrorCondition::invalid_argument : ErrorCondition::file_exists));
    return presentCount == s_OutputFileCount;
}

[[nodiscard]] static Expected<void, ErrorCode> CheckWorkPaths(const OutputPaths& paths)noexcept{
    const NotNull<const ScratchPath*> files[] = { MakeNotNull(&paths.metadataTemporary), MakeNotNull(&paths.fontTemporary), MakeNotNull(&paths.metadataBackup), MakeNotNull(&paths.fontBackup), };
    for(const auto file : files){
        const auto exists = FileExistsNoFollow(*file);
        if(!exists)
            return MakeUnexpected(exists.error());
        if(*exists)
            return MakeUnexpected(MakeErrorCode(ErrorCondition::file_exists));
    }
    return {};
}

[[nodiscard]] static Expected<void, ErrorCode> Publish(const OutputPaths& paths, TemporaryFile& font, TemporaryFile& metadata, const bool overwrite){
    const auto present = CheckOutputs(paths, overwrite);
    if(!present)
        return MakeUnexpected(present.error());
    const NotNull<const ScratchPath*> outputs[] = { MakeNotNull(&paths.font), MakeNotNull(&paths.metadata) };
    const NotNull<const ScratchPath*> backups[] = { MakeNotNull(&paths.fontBackup), MakeNotNull(&paths.metadataBackup) };
    const NotNull<const ScratchPath*> temporaries[] = { MakeNotNull(&paths.fontTemporary), MakeNotNull(&paths.metadataTemporary) };
    const NotNull<TemporaryFile*> staged[] = { MakeNotNull(&font), MakeNotNull(&metadata) };
    ErrorCode removalErrors[s_OutputFileCount];
    ErrorCode restorationErrors[s_OutputFileCount];
    u32 backedUp = 0u;
    u32 published = 0u;

    // Restore the previous pair before any allocating failure observer can run or an unexpected exception leaves publication.
    const auto rollback = [&]()noexcept{
        for(u32 index = s_OutputFileCount; index > 0u; --index){
            const u32 current = index - 1u;
            if(current < published){
                const auto removed = RemoveFile(*outputs[current]);
                if(!removed){
                    removalErrors[current] = removed.error();
                    const DiagnosticEventText diagnostic = MakeDiagnosticEventText("font_builder: could not remove failed new '{}', error {}", outputs[current]->c_str(), removalErrors[current].value());
                    CaptureDiagnosticEvent(s_PublicationDiagnosticTag, diagnostic.view(), __FILE__, __LINE__);
                }
            }
            if(current < backedUp && !removalErrors[current]){
                const auto restored = RenamePath(*backups[current], *outputs[current]);
                if(!restored){
                    restorationErrors[current] = restored.error();
                    const DiagnosticEventText diagnostic = MakeDiagnosticEventText("font_builder: could not restore previous '{}', error {}", outputs[current]->c_str(), restorationErrors[current].value());
                    CaptureDiagnosticEvent(s_PublicationDiagnosticTag, diagnostic.view(), __FILE__, __LINE__);
                }
            }
        }
    };
    ScopeExit restoreOnFailure(rollback);

    const auto failPublication = [&](const ErrorCode& error) -> Expected<void, ErrorCode>{
        rollback();
        restoreOnFailure.release();
        for(u32 index = 0u; index < s_OutputFileCount; ++index){
            if(removalErrors[index])
                NWB_LOGGER_ERROR(NWB_TEXT("font_builder: could not remove failed new '{}': {}"), PathToString<tchar>(*outputs[index]), StringConvert(removalErrors[index].message()));
            if(restorationErrors[index])
                NWB_LOGGER_ERROR(NWB_TEXT("font_builder: could not restore previous '{}': {}"), PathToString<tchar>(*outputs[index]), StringConvert(restorationErrors[index].message()));
        }
        return MakeUnexpected(error);
    };

    if(*present){
        for(; backedUp < s_OutputFileCount; ++backedUp){
            const auto preserved = RenamePath(*outputs[backedUp], *backups[backedUp]);
            if(!preserved)
                return failPublication(preserved.error());
        }
    }
    for(; published < s_OutputFileCount; ++published){
        const auto finalized = RenamePath(*temporaries[published], *outputs[published]);
        if(!finalized)
            return failPublication(finalized.error());
        staged[published]->published();
    }

    restoreOnFailure.release();
    for(u32 index = 0u; index < backedUp; ++index){
        const auto removed = RemoveFile(*backups[index]);
        if(!removed)
            NWB_LOGGER_WARNING(NWB_TEXT("font_builder: published pair but could not remove backup '{}': {}"), PathToString<tchar>(*backups[index]), StringConvert(removed.error().message()));
        else if(!*removed)
            NWB_LOGGER_WARNING(NWB_TEXT("font_builder: published pair but previous backup was already absent: '{}'"), PathToString<tchar>(*backups[index]));
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WriteOutputs(const BakeOptions& options, const Impl::FontAtlasPayload& payload, Core::Alloc::ScratchArena& scratch){
    auto fontBinary = BuildPreparedFont(options, payload, scratch);
    MetadataString metadata(scratch);
    if(!fontBinary){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: prepared font failed: {}"), StringConvert(fontBinary.error()));
        return false;
    }
    BuildFontMetadata(payload, metadata);

    const __hidden_font_builder_writer::OutputPaths paths(scratch, options.output);
    const auto outputs = __hidden_font_builder_writer::CheckOutputs(paths, options.overwrite);
    if(!outputs){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: output pair unavailable; use --overwrite for complete regular .nwb and .font files: {}"), StringConvert(outputs.error().message()));
        return false;
    }
    const auto workPaths = __hidden_font_builder_writer::CheckWorkPaths(paths);
    if(!workPaths){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: publication work paths unavailable: {}"), StringConvert(workPaths.error().message()));
        return false;
    }
    const ScratchPath directory = paths.metadata.parentPath();
    if(!directory.empty()){
        const auto ensured = EnsureDirectories(directory);
        if(!ensured){
            NWB_LOGGER_ERROR(NWB_TEXT("font_builder: failed to prepare output directory '{}': {}"), PathToString<tchar>(directory), StringConvert(ensured.error().message()));
            return false;
        }
    }

    __hidden_font_builder_writer::TemporaryFile stagedFont(paths.fontTemporary);
    __hidden_font_builder_writer::TemporaryFile stagedMetadata(paths.metadataTemporary);
    const auto metadataBytes = MakeNotNull(reinterpret_cast<const u8*>(metadata.data()));
    const auto fontStaged = stagedFont.stage(fontBinary->data(), fontBinary->size());
    if(!fontStaged){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: failed to stage font temporary: {}"), StringConvert(fontStaged.error().message()));
        return false;
    }
    const auto metadataStaged = stagedMetadata.stage(metadataBytes.get(), metadata.size());
    if(!metadataStaged){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: failed to stage metadata temporary: {}"), StringConvert(metadataStaged.error().message()));
        return false;
    }
    if(!stagedFont.verify(fontBinary->data(), fontBinary->size()) || !stagedMetadata.verify(metadataBytes.get(), metadata.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: staging or verification failed; previous pair remains in place"));
        return false;
    }
    const auto published = __hidden_font_builder_writer::Publish(paths, stagedFont, stagedMetadata, options.overwrite);
    if(!published){
        NWB_LOGGER_ERROR(NWB_TEXT("font_builder: pair publication failed: {}"), StringConvert(published.error().message()));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FONT_BUILDER_UTILITY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

