// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_file_publisher.h"

#include <core/common/log.h>

#include <global/binary.h>
#include <global/diagnostics.h>
#include <global/filesystem/operations_file_io.h>
#include <global/filesystem/staged_file_writer.h>
#include <global/numeric_text.h>
#include <global/process.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_file_publisher{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchBytes = Vector<u8, ScratchArena>;
using ScratchPath = ::Path<ScratchArena>;

inline constexpr AStringView s_TemporaryNamePrefix = ".nwb_";
inline constexpr char s_TemporaryNameSeparator = '_';
inline constexpr AStringView s_TemporaryNameExtension = ".tmp";
inline constexpr u64 s_TemporarySequenceStep = 1u;
inline constexpr u32 s_MaxRenameAttempts = 16u;
inline constexpr StringView s_WarningEventName = "warning";
Atomic<u64> g_TemporarySequence{0u};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AssetFilePublisher::Write(const __hidden_asset_file_publisher::ScratchPath& path, const Span<const u8> bytes, const b8 replaceDifferent, __hidden_asset_file_publisher::ScratchBytes& existing){
    const auto read = ReadBinaryFile(path, existing);
    if(read && BytesMatch(existing, bytes))
        return true;
    if((!read && !IsMissingPathError(read.error())) || (!replaceDifferent && read)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: cannot publish built asset without replacing existing data '{}'"), PathToString<tchar>(path));
        return false;
    }

    ScratchString temporaryName(path.arena());
    __hidden_asset_file_publisher::ScratchPath temporary(path.arena());
    b8 ownsTemporary = false;
    ScopeExit removeTemporary([&temporary, &ownsTemporary]()noexcept{
        if(ownsTemporary && !RemoveFile(temporary)){
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = __hidden_asset_file_publisher::s_WarningEventName, .category = "built_asset_temporary_cleanup", .message = "AssetVolume: failed to remove temporary output", });
        }
    });
    b8 staged = false;
    for(u32 attempt = 0u; attempt < __hidden_asset_file_publisher::s_MaxRenameAttempts; ++attempt){
        temporaryName = __hidden_asset_file_publisher::s_TemporaryNamePrefix;
        AppendHexU64(CurrentProcessId(), temporaryName);
        temporaryName += __hidden_asset_file_publisher::s_TemporaryNameSeparator;
        AppendHexU64(__hidden_asset_file_publisher::g_TemporarySequence.fetch_add(__hidden_asset_file_publisher::s_TemporarySequenceStep, MemoryOrder::relaxed), temporaryName);
        temporaryName += __hidden_asset_file_publisher::s_TemporaryNameExtension;
        temporary = path.parentPath() / temporaryName;
        const auto result = StagedFileWriter(temporary).create(bytes.data(), bytes.size(), ownsTemporary);
        if(result){
            staged = true;
            break;
        }
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        const b8 collision = result.error().value() == ERROR_FILE_EXISTS || result.error().value() == ERROR_ALREADY_EXISTS;
#else
        const b8 collision = result.error() == ErrorCondition::file_exists;
#endif
        if(ownsTemporary || !collision)
            break;
    }
    if(!staged || !ReadBinaryFile(temporary, existing) || !BytesMatch(existing, bytes)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: failed to publish built asset '{}'"), PathToString<tchar>(path));
        return false;
    }
    for(u32 attempt = 0u; attempt < __hidden_asset_file_publisher::s_MaxRenameAttempts; ++attempt){
        if(replaceDifferent){
            if(RenamePath(temporary, path)){
                ownsTemporary = false;
                return true;
            }
        }
        else{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
            if(MoveFileEx(temporary.c_str(), path.c_str(), 0u)){
                ownsTemporary = false;
                return true;
            }
#else
            if(::link(temporary.c_str(), path.c_str()) == 0)
                return true;
#endif
        }
        if(ReadBinaryFile(path, existing) && BytesMatch(existing, bytes))
            return true;
    }
    NWB_LOGGER_ERROR(FOUNDATION_TEXT("AssetVolume: failed to publish built asset '{}'"), PathToString<tchar>(path));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

